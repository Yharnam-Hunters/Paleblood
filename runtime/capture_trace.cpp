/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A traced call as a schema-1 case (runtime/include/runtime/capture_trace.h).

   - Bytes inside the executable's image become `memory` entries at their PS4 addresses.
   - Every other byte the call touched belongs to a buffer: bytes closer than MERGE_GAP join one
     buffer (an object and its neighbours), which starts at a 16-byte boundary so aligned vector
     loads stay aligned. A byte read before it was written holds the value read; the rest are 0.
   - An eight-byte input whose value points into a buffer or into the image is a pointer: the
     case gives it the buffer's address (`pointer`) or the image address (`guest`), so it still
     points at the same thing in the verifier's sandbox.
   - Arguments and stack arguments that point into a buffer or the image are written the same way;
     other values are numbers. */
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
    Builder(const rt_trace_log &log, rt_code_range image)
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
            out += (out.empty() ? "" : ", ") + ("\"" + regions_[i].name + "\": {\"size\": " + std::to_string(b.size()) +
                                                ", \"bytes\": \"" +
                                                bytes_hex(reinterpret_cast<const uint8_t *>(b.data()), b.size()) + "\"}");
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
            if (!input(q, 8, &v)) continue;
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
            if (!in_image(a) || !(log_.flags[i] & RT_TRACE_READ) || pointer_bytes.count(a)) {
                i++;
                continue;
            }
            std::vector<uint8_t> run;
            size_t j = i;
            while (j < log_.count && log_.addr[j] == a + run.size() && in_image(log_.addr[j]) &&
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

}  // namespace

std::string rt::traced_case(const rt_trace_log &log, const rt_trace_args &args, const rt_traced_function &f,
                            int number, const char *run, rt_code_range image)
{
    if (log.incomplete || number < 0 || !run || !f.function || !valid_returns(f.returns)) return "";
    const Builder b(log, image);
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
           "}, \"memory\": [" + b.memory() + "], \"stubs\": [], \"imports\": []}";
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
        rt_capture_write(f->function, number, json.c_str());
        written = 1;
    } else if (std::getenv("BB_CAPTURE_VERBOSE")) {
        std::fprintf(stderr, "capture: %s: no case: %s\n", f->function,
                     log.incomplete ? log.incomplete : "the trace cannot be written as a case");
    }
    rt_trace_log_free(&log);
    return written;
}
