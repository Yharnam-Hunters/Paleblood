// SPDX-License-Identifier: GPL-2.0-or-later
// Input: a per-frame step that marks two pad map entries and advances an owner by a fixed step.
//
// input_pad_step_01972900 (0x01972900, once a frame in gameplay) reads FD4PadManager's map
// (singleton 0x058b31f0, map at +0x38 -> +0x8; MSVC tree nodes: left +0x0, right +0x10, nil flag
// +0x19, key +0x20, value +0x28, flag +0x30). It sets the flag of the entry keyed by the address of
// 0x059464a8 (a missing key calls 0x017064b0 with the manager). When this->+0xc4 is 1, +0xd0 is
// set and +0xc8 is 0, it sets the flag of the entry keyed by 0x059566c4 (missing: 0x01972cd0,
// whose result is used) and calls 0x021145b0(this->+0xd0, {entry value, frame-time descriptor}).
// The descriptor's step is 1/30 s; the community frame-rate patches change it to 1/15 s
// ("30 FPS++", "60 FPS++") or 1/240 s ("Uncap FPS++"), which BB_TARGET_FPS does here.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "../frame_timing/target_fps.h"
#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t pad_manager_singleton = 0x058b31f0;
constexpr uint32_t key_first = 0x059464a8, key_second = 0x059566c4;
constexpr uint32_t missing_first = 0x017064b0, missing_second = 0x01972cd0, advance = 0x021145b0;
constexpr uint32_t fatal_error = 0x024b55b0;
constexpr uint32_t str_singleton_header = 0x04d3b369, str_singleton_func = 0x04d3b3bd, str_pad_manager = 0x04d3b33a;
constexpr uint32_t descriptor_vtable = 0x056efd30;   // + 0x10
constexpr uint32_t k_step_30 = 0x3d088889, k_step_15 = 0x3d888889, k_step_240 = 0x3b888889;

struct Step {
    uint64_t value;
    void *vtable;
    uint32_t seconds_bits;
};

using Fatal = void(const char *, int, const char *, const char *, ...);
using Missing = uint64_t(void *manager);
using Advance = void(void *owner, Step *step);

template <typename T> T get(const unsigned char *p, unsigned off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }
template <typename T> void put(unsigned char *p, unsigned off, T v) { std::memcpy(p + off, &v, sizeof v); }

uint32_t step_bits()
{
    switch (frame_timing::target_fps()) {
    case frame_timing::Target::fps30:
    case frame_timing::Target::fps60: return k_step_15;
    case frame_timing::Target::uncapped: return k_step_240;
    default: return k_step_30;
    }
}

uint32_t guest_address(const void *host)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host) - reinterpret_cast<uintptr_t>(rt_image) + RT_EBOOT_BASE);
}

bool in_image(uint64_t v)
{
    const uint64_t base = reinterpret_cast<uintptr_t>(rt_image);
    return v >= base && v < base + 0x6000000;
}

std::string hexbytes(const void *p, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        const unsigned b = static_cast<const unsigned char *>(p)[i];
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

std::string addr(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

// Recording: the tree nodes the lookups read, one buffer per node address, with the fields the
// lookups read (child pointer taken, nil flag, key, value).
struct Capture {
    int number = -1;
    std::map<const unsigned char *, std::string> nodes;
    std::string memory, stubs;
    void mem(const std::string &e) { memory += (memory.empty() ? "" : ", ") + e; }
    void stub(const std::string &e) { stubs += (stubs.empty() ? "" : ", ") + e; }
    std::string node(const unsigned char *n)
    {
        auto it = nodes.find(n);
        if (it != nodes.end()) return it->second;
        const std::string name = "node" + std::to_string(nodes.size());
        nodes[n] = name;
        mem("{\"addr\": \"buf:" + name + "+25\", \"bytes\": \"" + hexbytes(n + 0x19, 1) + "\"}");
        if (!n[0x19]) {
            const uint64_t key = get<uint64_t>(n, 0x20);
            mem(in_image(key) ? "{\"addr\": \"buf:" + name + "+32\", \"guest\": \"" +
                                    addr(guest_address(reinterpret_cast<const void *>(key))) + "\"}"
                              : "{\"addr\": \"buf:" + name + "+32\", \"bytes\": \"" + hexbytes(n + 0x20, 8) + "\"}");
            mem("{\"addr\": \"buf:" + name + "+40\", \"bytes\": \"" + hexbytes(n + 0x28, 8) + "\"}");
        }
        return name;
    }
    void link(const unsigned char *from, unsigned off, const unsigned char *to)
    {
        mem("{\"addr\": \"buf:" + node(from) + "+" + std::to_string(off) + "\", \"pointer\": \"" + node(to) + "\"}");
    }
};

// map.at(key)-style lower bound over the tree; returns the matching node or the header.
unsigned char *find(unsigned char *header, uint64_t key, Capture &cap)
{
    unsigned char *result = header;
    unsigned char *parent = header;
    unsigned off = 0x8;   // header +0x8 is the root
    for (;;) {
        unsigned char *n = get<unsigned char *>(parent, off);
        if (cap.number >= 0) cap.link(parent, off, n);
        if (n[0x19]) break;
        if (static_cast<int64_t>(get<uint64_t>(n, 0x20)) < static_cast<int64_t>(key)) {
            parent = n;
            off = 0x10;
        } else {
            result = n;
            parent = n;
            off = 0x0;
        }
    }
    if (result == header || static_cast<int64_t>(get<uint64_t>(result, 0x20)) > static_cast<int64_t>(key))
        return header;
    return result;
}

unsigned char *pad_map(Capture &cap)
{
    unsigned char *manager = *rt::ptr<unsigned char *>(pad_manager_singleton);
    if (!manager)
        rt::fn<Fatal>(fatal_error)(rt::ptr<const char>(str_singleton_header), 0xb1, rt::ptr<const char>(str_singleton_func),
                                   rt::ptr<const char>(str_pad_manager));
    unsigned char *holder = get<unsigned char *>(manager, 0x38);
    unsigned char *header = get<unsigned char *>(holder, 0x8);
    if (cap.number >= 0 && cap.nodes.empty()) {
        cap.mem("{\"addr\": \"" + addr(pad_manager_singleton) + "\", \"pointer\": \"manager\"}");
        cap.mem("{\"addr\": \"buf:manager+56\", \"pointer\": \"holder\"}");
        cap.mem("{\"addr\": \"buf:holder+8\", \"pointer\": \"" + cap.node(header) + "\"}");
    }
    return header;
}

}  // namespace

extern "C" void bb_input_pad_step_01972900(unsigned char *self)
{
    Capture cap;
    cap.number = rt_capture_begin("input_pad_step_01972900");
    void *manager = *rt::ptr<void *>(pad_manager_singleton);

    unsigned char *header = pad_map(cap);
    unsigned char *entry = find(header, reinterpret_cast<uintptr_t>(rt::ptr<unsigned char>(key_first)), cap);
    if (entry != header) {
        entry[0x30] = 1;
    } else {
        rt::fn<Missing>(missing_first)(manager);
        if (cap.number >= 0) cap.stub("{\"address\": \"0x017064b0\", \"argc\": 1}");
    }

    const uint32_t state = get<uint32_t>(self, 0xc4), blocked = get<uint32_t>(self, 0xc8);
    void *owner = get<void *>(self, 0xd0);
    if (state == 1 && owner && blocked == 0) {
        header = pad_map(cap);
        entry = find(header, reinterpret_cast<uintptr_t>(rt::ptr<unsigned char>(key_second)), cap);
        uint64_t value;
        if (entry != header) {
            entry[0x30] = 1;
            value = get<uint64_t>(entry, 0x28);
        } else {
            value = rt::fn<Missing>(missing_second)(manager);
            if (cap.number >= 0) {
                char ret[32];
                std::snprintf(ret, sizeof ret, "0x%llx", static_cast<unsigned long long>(value));
                cap.stub(std::string("{\"address\": \"0x01972cd0\", \"argc\": 1, \"ret\": \"") + ret + "\"}");
            }
        }
        Step step{value, rt::ptr<unsigned char>(descriptor_vtable) + 0x10, step_bits()};
        reinterpret_cast<Advance *>(rt::ptr<void>(advance))(owner, &step);
        if (cap.number >= 0)
            cap.stub("{\"address\": \"0x021145b0\", \"argc\": 2, \"argmem\": [{\"arg\": 1, \"size\": 20}]}");
    }

    if (cap.number >= 0) {
        char head[512];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x01972900\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:self\"}, ", cap.number);
        std::string buffers = "\"self\": {\"size\": 224}, \"manager\": {\"size\": 64}, \"holder\": {\"size\": 16}, \"owner\": {\"size\": 8}";
        for (const auto &n : cap.nodes) buffers += ", \"" + n.second + "\": {\"size\": 56}";
        char fields[256];
        std::snprintf(fields, sizeof fields,
                      "{\"addr\": \"buf:self+196\", \"bytes\": \"%s\"}, {\"addr\": \"buf:self+200\", \"bytes\": \"%s\"}",
                      hexbytes(&state, 4).c_str(), hexbytes(&blocked, 4).c_str());
        std::string mem = std::string(fields) + (owner ? ", {\"addr\": \"buf:self+208\", \"pointer\": \"owner\"}"
                                                       : ", {\"addr\": \"buf:self+208\", \"bytes\": \"0000000000000000\"}");
        const std::string json = std::string(head) + "\"buffers\": {" + buffers + "}, \"memory\": [" + mem + ", " +
                                 cap.memory + "], \"stubs\": [" + cap.stubs + "], \"imports\": []}";
        rt_capture_write("input_pad_step_01972900", cap.number, json.c_str());
    }
}
