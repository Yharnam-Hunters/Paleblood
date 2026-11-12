/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A traced call as a schema-1 case (runtime/include/runtime/capture_trace.h).

   - Bytes inside the executable's image become `memory` entries at their PS4 addresses.
   - Every other byte the call touched belongs to a buffer: bytes closer than MERGE_GAP join one
     buffer (an object and its neighbours), which starts at a 16-byte boundary so aligned vector
     loads stay aligned. A byte read before it was written holds the value read; the rest are 0.
   - An eight-byte input whose value points into a buffer or into the image is a pointer: the
     case gives it the buffer's address (`pointer`) or the image address (`guest`), so it still
     points at the same thing in the verifier's sandbox.
   - An access through a base pointer 256 bytes to 1 MiB away from it puts the pointer and what it
     reached in one buffer, so the replay keeps their distance.
   - Arguments and stack arguments that point into a buffer or the image are written the same way;
     other values are numbers.
   - Imports the call made are scripted in order: return value, and the bytes each changed behind
     its pointer arguments. */
#include "runtime/capture_trace.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint64_t MERGE_GAP = 256;
/* How far before the bytes it reaches a pointer may lie (a table base, then an index into it) and
   still have their buffer widened to start at it. */
constexpr uint64_t POINTER_REACH = 64 * 1024;

/* The image's read-only segments (rt_capture_set_constant): the verifier maps the same executable,
   so their bytes are never case inputs, and presetting them would write into read-only memory. */
std::vector<rt_code_range> constant_ranges;

bool constant(uint64_t a)
{
    for (const rt_code_range &r : constant_ranges)
        if (a >= reinterpret_cast<uintptr_t>(r.lo) && a < reinterpret_cast<uintptr_t>(r.hi)) return true;
    return false;
}

struct Region {
    uint64_t base, end;     /* [base, end), end rounded up to 16 */
    std::string name;
};

std::string number_hex(uint64_t v)
{
    char b[24];
    std::snprintf(b, sizeof b, "0x%llx", static_cast<unsigned long long>(v));
    return b;
}

std::string guest_hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

std::string bytes_hex(const uint8_t *p, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += digits[p[i] >> 4];
        s += digits[p[i] & 15];
    }
    return s;
}

bool valid_returns(const char *r)
{
    static const char *const known[] = {"i8", "i32", "i64", "i128", "f32", "f64", "vec", "void"};
    for (const char *k : known)
        if (r && !std::strcmp(r, k)) return true;
    return false;
}

class Builder {
public:
    /* `pointers`: argument values the code may use as base addresses, as are the eight-byte
       inputs. One that lies before the bytes it reaches (a struct whose first fields go unread,
       or a table base the code indexes far into) widens their buffer down to it, and one just past
       a buffer's end widens it up to it: the pointer is then written as buf:NAME+off rather than
       as this run's address, and the distance to the bytes it reaches is kept. */
    Builder(const rt_trace_log &log, rt_code_range image, const std::vector<uint64_t> &pointers = {})
        : log_(log), lo_(reinterpret_cast<uintptr_t>(image.lo)), hi_(reinterpret_cast<uintptr_t>(image.hi))
    {
        for (size_t i = 0; i < log.count; i++) {
            const uint64_t a = log.addr[i];
            if (in_image(a)) continue;
            if (regions_.empty() || a >= regions_.back().end + MERGE_GAP)
                regions_.push_back({a & ~uint64_t{15}, a + 1, ""});
            else
                regions_.back().end = a + 1;
        }
        std::vector<uint64_t> anchors(pointers);
        for (size_t i = 0; i < log.qword_count; i++) {
            uint64_t v;
            if (input(log.qword[i], 8, &v)) anchors.push_back(v);
        }
        for (uint64_t v : anchors) {
            if (in_image(v) || region(v, false)) continue;
            Region *after = nullptr;                    /* the nearest buffer above the pointer */
            for (Region &r : regions_)
                if (r.base > v && r.base - v <= POINTER_REACH && (!after || r.base < after->base)) after = &r;
            if (after) {
                after->base = v & ~uint64_t{15};
                continue;
            }
            for (Region &r : regions_)
                if (v > r.end && v - r.end <= MERGE_GAP) r.end = v;
        }
        /* An access far from the pointer it went through (a large object whose fields lie in
           separate runs) keeps the pointer and what it reached in one buffer, at their distance. */
        for (size_t i = 0; i < log.link_count; i++) {
            const uint64_t base = log.link[i][0], reached = log.link[i][1];
            if (in_image(base) || in_image(reached)) continue;
            Region *r = nullptr;
            for (Region &x : regions_)
                if (reached >= x.base && reached < x.end) r = &x;
            if (!r) continue;               /* the private stack: not a buffer */
            r->base = std::min(r->base, base & ~uint64_t{15});
            r->end = std::max(r->end, base + 1);
        }
        std::sort(regions_.begin(), regions_.end(), [](const Region &a, const Region &b) { return a.base < b.base; });
        /* Widened buffers may now meet: join them. */
        std::vector<Region> joined;
        for (const Region &r : regions_) {
            if (!joined.empty() && r.base <= ((joined.back().end + 15) & ~uint64_t{15}))
                joined.back().end = std::max(joined.back().end, r.end);
            else
                joined.push_back(r);
        }
        regions_.swap(joined);
        for (size_t i = 0; i < regions_.size(); i++) {
            regions_[i].end = (regions_[i].end + 15) & ~uint64_t{15};
            regions_[i].name = "b" + std::to_string(i);
        }
    }

    bool in_image(uint64_t a) const { return a >= lo_ && a < hi_; }
    uint32_t guest(uint64_t a) const { return static_cast<uint32_t>(a - lo_ + RT_EBOOT_BASE); }

    /* The region holding `a`; a pointer one past the end still belongs to it. */
    const Region *region(uint64_t a, bool allow_end) const
    {
        for (const Region &r : regions_)
            if (a >= r.base && (a < r.end || (allow_end && a == r.end))) return &r;
        return nullptr;
    }

    /* Where an input byte lives in the case: an image address or buf:NAME+off. */
    std::string location(uint64_t a) const
    {
        if (in_image(a)) return guest_hex(guest(a));
        const Region *r = region(a, false);
        return "buf:" + r->name + "+" + number_hex(a - r->base);
    }

    /* An argument value: a buffer, an image address or a number. */
    std::string describe(uint64_t v) const
    {
        if (const Region *r = region(v, true)) return "buf:" + r->name + "+" + number_hex(v - r->base);
        if (in_image(v)) return "guest:" + guest_hex(guest(v));
        return number_hex(v);
    }

    /* The first value read at `a`, when all n bytes were inputs. */
    bool input(uint64_t a, size_t n, uint64_t *out) const
    {
        uint64_t v = 0;
        for (size_t i = 0; i < n; i++) {
            const uint64_t *p = std::lower_bound(log_.addr, log_.addr + log_.count, a + i);
            if (p == log_.addr + log_.count || *p != a + i) return false;
            const size_t k = static_cast<size_t>(p - log_.addr);
            if (!(log_.flags[k] & RT_TRACE_READ)) return false;
            v |= static_cast<uint64_t>(log_.value[k]) << (8 * i);
        }
        *out = v;
        return true;
    }

    std::string buffers() const
    {
        std::vector<std::string> bytes(regions_.size());
        for (size_t i = 0; i < regions_.size(); i++) bytes[i].assign(regions_[i].end - regions_[i].base, '\0');
        for (size_t i = 0; i < log_.count; i++) {
            if (in_image(log_.addr[i]) || !(log_.flags[i] & RT_TRACE_READ)) continue;
            const Region *r = region(log_.addr[i], false);
            bytes[static_cast<size_t>(r - regions_.data())][log_.addr[i] - r->base] = static_cast<char>(log_.value[i]);
        }
        std::string out;
        for (size_t i = 0; i < regions_.size(); i++) {
            const std::string &b = bytes[i];
            /* `bytes` is zero-padded to `size`: trailing zeros are left out (a far field keeps its
               object's buffer large and mostly unread). */
            size_t used = b.size();
            while (used && !b[used - 1]) used--;
            out += (out.empty() ? "" : ", ") + ("\"" + regions_[i].name + "\": {\"size\": " + std::to_string(b.size()) +
                                                ", \"bytes\": \"" +
                                                bytes_hex(reinterpret_cast<const uint8_t *>(b.data()), used) + "\"}");
        }
        return out;
    }

    std::string memory() const
    {
        std::string out;
        auto add = [&out](const std::string &e) { out += (out.empty() ? "" : ", ") + e; };
        std::set<uint64_t> pointer_bytes;
        std::vector<uint64_t> qwords(log_.qword, log_.qword + log_.qword_count);
        std::sort(qwords.begin(), qwords.end());
        qwords.erase(std::unique(qwords.begin(), qwords.end()), qwords.end());
        for (uint64_t q : qwords) {
            uint64_t v;
            if (constant(q) || !input(q, 8, &v)) continue;
            std::string target;
            if (const Region *r = region(v, true))
                target = "\"pointer\": \"" + r->name + "+" + number_hex(v - r->base) + "\"";
            else if (in_image(v))
                target = "\"guest\": \"" + guest_hex(guest(v)) + "\"";
            else
                continue;
            add("{\"addr\": \"" + location(q) + "\", " + target + "}");
            for (uint64_t i = 0; i < 8; i++) pointer_bytes.insert(q + i);
        }
        /* Image inputs as runs of consecutive bytes. */
        for (size_t i = 0; i < log_.count;) {
            const uint64_t a = log_.addr[i];
            if (!in_image(a) || constant(a) || !(log_.flags[i] & RT_TRACE_READ) || pointer_bytes.count(a)) {
                i++;
                continue;
            }
            std::vector<uint8_t> run;
            size_t j = i;
            while (j < log_.count && log_.addr[j] == a + run.size() && in_image(log_.addr[j]) && !constant(log_.addr[j]) &&
                   (log_.flags[j] & RT_TRACE_READ) && !pointer_bytes.count(log_.addr[j]))
                run.push_back(log_.value[j++]);
            add("{\"addr\": \"" + guest_hex(guest(a)) + "\", \"bytes\": \"" + bytes_hex(run.data(), run.size()) + "\"}");
            i = j;
        }
        return out;
    }

private:
    const rt_trace_log &log_;
    uintptr_t lo_, hi_;
    std::vector<Region> regions_;
};

/* The imports the call made, scripted in order: name, return value, and what each changed behind
   its pointer arguments. Arguments are not compared (argc 0): their count is not known here. A
   return value that points into a buffer or the image (memset returns its destination) is written
   as such, like an argument, so the replay's code gets its own address back. */
std::string imports(const rt_trace_log &log, const Builder &b)
{
    std::string out;
    for (size_t i = 0; i < log.import_count; i++) {
        const rt_trace_import_call &c = log.imports[i];
        std::string writes;
        for (size_t w = c.first_write; w < c.first_write + c.writes; w++) {
            const rt_trace_write &x = log.writes[w];
            writes += (writes.empty() ? "" : ", ") + ("{\"arg\": " + std::to_string(x.arg) + ", \"offset\": " +
                                                      std::to_string(x.offset) + ", \"bytes\": \"" +
                                                      bytes_hex(log.write_bytes + x.data, x.size) + "\"}");
        }
        const std::string described = b.describe(c.ret);
        const std::string ret = described.rfind("0x", 0) == 0 ? std::to_string(static_cast<unsigned long long>(c.ret))
                                                               : "\"" + described + "\"";
        out += (out.empty() ? "" : ", ") + ("{\"name\": \"" + std::string(c.name) + "\", \"argc\": 0, \"ret\": " + ret +
                                             ", \"writes\": [" + writes + "]}");
    }
    return out;
}

}  // namespace

std::string rt::traced_case(const rt_trace_log &log, const rt_trace_args &args, const rt_traced_function &f,
                            int number, const char *run, rt_code_range image)
{
    if (log.incomplete || number < 0 || !run || !f.function || !valid_returns(f.returns)) return "";
    std::vector<uint64_t> pointers(args.gpr, args.gpr + 6);
    pointers.insert(pointers.end(), args.stack, args.stack + log.stack_slots);
    for (size_t i = 0; i < log.import_count; i++) pointers.push_back(log.imports[i].ret);
    const Builder b(log, image, pointers);
    static const char *const registers[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    std::string arguments;
    for (int i = 0; i < 6; i++)
        arguments += (i ? ", \"" : "\"") + std::string(registers[i]) + "\": \"" + b.describe(args.gpr[i]) + "\"";
    arguments += ", \"xmm0\": \"" + bytes_hex(args.xmm[0], 16) + "\", \"xmm1\": \"" + bytes_hex(args.xmm[1], 16) + "\"";
    std::string stack;
    for (unsigned i = 0; i < log.stack_slots; i++)
        stack += (i ? ", \"" : "\"") + b.describe(args.stack[i]) + "\"";
    char id[32];
    std::snprintf(id, sizeof id, "capture_%04d", number);
    return "{\"schema\": 1, \"address\": \"" + guest_hex(f.address) + "\", \"id\": \"" + id + "\", \"returns\": \"" +
           f.returns + "\", \"capture\": {\"function\": \"" + f.function + "\", \"run_id\": \"" + run +
           "\"}, \"args\": {" + arguments + "}, \"stack\": [" + stack + "], \"buffers\": {" + b.buffers() +
           "}, \"memory\": [" + b.memory() + "], \"stubs\": [], \"imports\": [" + imports(log, b) + "]}";
}

extern "C" void rt_capture_set_constant(const rt_code_range *ranges, size_t count)
{
    constant_ranges.assign(ranges, ranges + count);
}

extern "C" int rt_capture_traced_call(const rt_traced_function *f, int number, const rt_trace_args *args,
                                      rt_code_range image, const rt_code_range *code, size_t ranges,
                                      rt_trace_result *result)
{
    rt_trace_log log;
    if (!f || rt_trace_call(f->entry, args, code, ranges, result, &log)) return -1;
    int written = 0;
    const char *run = rt_capture_run_id();
    const std::string json = rt::traced_case(log, *args, *f, number, run, image);
    if (!json.empty()) {
        /* A repeat of a case the library holds is dropped, but the trace still succeeded. */
        if (rt_capture_keep(f->function, json.c_str())) rt_capture_write(f->function, number, json.c_str());
        written = 1;
    } else if (std::getenv("BB_CAPTURE_VERBOSE")) {
        std::fprintf(stderr, "capture: %s: no case: %s\n", f->function,
                     log.incomplete ? log.incomplete : "the trace cannot be written as a case");
    }
    rt_trace_log_free(&log);
    return written;
}
