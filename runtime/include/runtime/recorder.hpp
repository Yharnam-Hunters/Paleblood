/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RECORDER_HPP
#define RUNTIME_RECORDER_HPP

/* C++ helper for recording a case (docs/VERIFY.md) from a replacement: every object the function
   reads becomes a buffer named b0, b1, ..., sized by the furthest field read; every field
   (address and length) is recorded once, with the value it had when it was first read (fields of
   objects inside the executable's image are recorded at their PS4 addresses); stubbed
   calls and library calls are listed in order. `begin` decides whether this call is recorded;
   everything else does nothing when it is not. */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "runtime/capture.h"
#include "runtime/guest.h"

namespace rt {

inline std::string hex_bytes(const void *p, size_t n)
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

inline std::string guest_hex(uint32_t a)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", a);
    return b;
}

inline bool in_image(const void *p, uint64_t size = 0x6000000)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p), base = reinterpret_cast<uintptr_t>(rt_image);
    return v >= base && v < base + size;
}

/* The PS4 address of a host pointer into the executable's image. */
inline uint32_t guest_of(const void *host)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host) - reinterpret_cast<uintptr_t>(rt_image) + RT_EBOOT_BASE);
}

class Recorder {
public:
    int number = -1;
    std::string memory, stubs, imports;

    bool begin(const char *function)
    {
        name_ = function;
        number = rt_capture_begin(function);
        return on();
    }
    bool on() const { return number >= 0; }

    /* The buffer holding `base` (created on first use), grown to cover `extent` bytes. */
    std::string name(const void *base, unsigned extent = 16)
    {
        auto it = bufs_.find(base);
        if (it == bufs_.end()) it = bufs_.emplace(base, Buf{"b" + std::to_string(bufs_.size()), 16}).first;
        if (extent > it->second.size) it->second.size = (extent + 15) & ~15u;
        return it->second.name;
    }
    /* Where a field lives in the case: a buffer, or the image itself (global objects). */
    std::string at(const void *base, unsigned off, unsigned n)
    {
        if (in_image(base)) return guest_hex(guest_of(base) + off);
        return "buf:" + name(base, off + n) + "+" + std::to_string(off);
    }
    /* A value for a case argument or a stub's return: a buffer, an image address, or a number. */
    std::string value(const void *p)
    {
        if (!p) return "0x0";
        if (in_image(p)) return "guest:" + guest_hex(guest_of(p));
        return "buf:" + name(p);
    }

    void bytes(const void *base, unsigned off, unsigned n)
    {
        entry(at(base, off, n), n, "\"bytes\": \"" + hex_bytes(static_cast<const char *>(base) + off, n) + "\"");
    }
    /* A pointer field: to another recorded object, to the image, or its raw bytes when null. */
    void pointer(const void *base, unsigned off)
    {
        const void *target;
        std::memcpy(&target, static_cast<const char *>(base) + off, sizeof target);
        if (!target) return bytes(base, off, 8);
        const std::string a = at(base, off, 8);
        if (in_image(target)) return entry(a, 8, "\"guest\": \"" + guest_hex(guest_of(target)) + "\"");
        entry(a, 8, "\"pointer\": \"" + name(target) + "\"");
    }
    void global_bytes(uint32_t address, unsigned n)
    {
        entry(guest_hex(address), n, "\"bytes\": \"" + hex_bytes(rt::ptr<void>(address), n) + "\"");
    }
    void global_pointer(uint32_t address)
    {
        const void *target = *rt::ptr<const void *>(address);
        if (!target) return global_bytes(address, 8);
        if (in_image(target)) return entry(guest_hex(address), 8, "\"guest\": \"" + guest_hex(guest_of(target)) + "\"");
        entry(guest_hex(address), 8, "\"pointer\": \"" + name(target) + "\"");
    }

    void stub(const std::string &e) { stubs += (stubs.empty() ? "" : ", ") + e; }
    void import(const std::string &e) { imports += (imports.empty() ? "" : ", ") + e; }

    /* The case: `args` is the inside of the "args" object; `extra` more top-level members. */
    void write(const std::string &address, const std::string &args, const std::string &returns = "void",
               const std::string &extra = "")
    {
        if (!on()) return;
        char head[160];
        std::snprintf(head, sizeof head, "{\"schema\": 1, \"address\": \"%s\", \"id\": \"capture_%04d\", \"returns\": \"%s\", ",
                      address.c_str(), number, returns.c_str());
        std::string bufs;
        for (const auto &b : bufs_)
            bufs += (bufs.empty() ? "" : ", ") + ("\"" + b.second.name + "\": {\"size\": " + std::to_string(b.second.size) + "}");
        const std::string json = std::string(head) + "\"args\": {" + args + "}, \"buffers\": {" + bufs + "}, \"memory\": [" +
                                 memory + "], \"stubs\": [" + stubs + "], \"imports\": [" + imports + "]" +
                                 (extra.empty() ? "" : ", " + extra) + "}";
        rt_capture_write(name_, number, json.c_str());
    }

private:
    struct Buf {
        std::string name;
        unsigned size;
    };
    void entry(const std::string &addr, unsigned n, const std::string &rest)
    {
        if (!seen_.insert({addr, n}).second) return;
        memory += (memory.empty() ? "" : ", ") + ("{\"addr\": \"" + addr + "\", " + rest + "}");
    }
    const char *name_ = "";
    std::map<const void *, Buf> bufs_;
    std::set<std::pair<std::string, unsigned>> seen_;
};

}  // namespace rt

#endif
