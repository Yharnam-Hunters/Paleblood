// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the SprjFlipper constructor.
//
// frame_timing_flipper_init (0x02434520) sets the vtable, reads the config value Game.FlipMode
// twice through a temporary wide string (current mode, default 4; pending mode, default 3; each
// clamped to 4), sets a 1/30 s interval with sync interval 0, stamps the clock into the eight
// time fields and clears the frame ring, the history and the flags. The limiter
// (frame_limiter.cpp) documents what the fields mean. With BB_TARGET_FPS=60 the first interval
// is 1/60 s, as the 60 FPS patches write it (target_fps.h).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/capture.h"
#include "runtime/guest.h"
#include "target_fps.h"

namespace {

constexpr uint32_t flipper_vtable = 0x0575a910;
constexpr uint32_t config_singleton = 0x0593d710;
constexpr uint32_t wide_string_construct = 0x02bc0e00;
constexpr uint32_t config_get_int = 0x024eba00;
constexpr uint32_t gettimeofday_thunk = 0x02fbe728;
constexpr uint32_t str_game_flip_mode = 0x04d7ea96;    // L"Game.FlipMode"
constexpr uint32_t max_mode = 4;
// The game's wide string object (its constructor fills it): data pointer at +0x8, capacity at
// +0x20, allocator at +0x28, a flag the caller sets at +0x30. A heap buffer (capacity >= 8) is
// freed through the allocator's slot +0x70.
constexpr unsigned str_data = 0x08, str_capacity = 0x20, str_allocator = 0x28, str_flag = 0x30;
constexpr unsigned str_size = 0x40;

struct Timeval {
    int64_t seconds;
    int64_t microseconds;
};

using Construct = void(void *self, const void *text);
using GetInt = uint32_t(void *config, void *key, uint32_t fallback);
using Free = void(void *allocator, void *data);
using Gettimeofday = int(Timeval *tv, void *tz);

template <typename T> T get(const unsigned char *p, unsigned off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }
template <typename T> void put(unsigned char *p, unsigned off, T v) { std::memcpy(p + off, &v, sizeof v); }

uint32_t guest_address(const void *host)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(host) - reinterpret_cast<uintptr_t>(rt_image) + RT_EBOOT_BASE);
}

std::string hex(const void *p, size_t n)
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

struct Capture {
    int number = -1;
    std::string buffers, memory, stubs, imports;
    int strings = 0;

    void add(std::string &list, const std::string &entry) { list += std::string(list.empty() ? "" : ", ") + entry; }
    static std::string addr(uint32_t a)
    {
        char b[16];
        std::snprintf(b, sizeof b, "0x%08x", a);
        return b;
    }
};

uint32_t read_flip_mode(void *config, uint32_t fallback, Capture &cap)
{
    alignas(16) unsigned char str[str_size] = {};
    rt::fn<Construct>(wide_string_construct)(str, rt::ptr<const void>(str_game_flip_mode));
    str[str_flag] = 1;
    const uint32_t value = rt::fn<GetInt>(config_get_int)(config, str, fallback);
    const uint64_t capacity = get<uint64_t>(str, str_capacity);
    void *allocator = get<void *>(str, str_allocator);
    void *free_fn = nullptr;
    if (capacity >= 8) {
        free_fn = (*static_cast<void ***>(allocator))[0x70 / 8];
        reinterpret_cast<Free *>(free_fn)(allocator, get<void *>(str, str_data));
    }
    if (cap.number >= 0) {
        const int n = cap.strings++;
        const std::string data = "strdata" + std::to_string(n), alloc = "stralloc" + std::to_string(n);
        cap.add(cap.buffers, "\"" + data + "\": {\"size\": 8}");
        std::string writes = "{\"arg\": 0, \"offset\": 8, \"pointer\": \"" + data + "\"}, "
                             "{\"arg\": 0, \"offset\": 32, \"bytes\": \"" + hex(&capacity, 8) + "\"}";
        if (capacity >= 8) {
            cap.add(cap.buffers, "\"" + alloc + "\": {\"size\": 8}, \"" + alloc + "_vt\": {\"size\": 120}");
            cap.add(cap.memory, "{\"addr\": \"buf:" + alloc + "\", \"pointer\": \"" + alloc + "_vt\"}");
            cap.add(cap.memory, "{\"addr\": \"buf:" + alloc + "_vt+112\", \"guest\": \"" + Capture::addr(guest_address(free_fn)) + "\"}");
            writes += ", {\"arg\": 0, \"offset\": 40, \"pointer\": \"" + alloc + "\"}";
        }
        cap.add(cap.stubs, "{\"address\": \"0x02bc0e00\", \"argc\": 2, \"writes\": [" + writes + "]}");
        cap.add(cap.stubs, "{\"address\": \"0x024eba00\", \"argc\": 3, \"ret\": " + std::to_string(value) + "}");
        if (capacity >= 8)
            cap.add(cap.stubs, "{\"address\": \"" + Capture::addr(guest_address(free_fn)) + "\", \"argc\": 2}");
    }
    return value;
}

}  // namespace

extern "C" void bb_frame_timing_flipper_init(unsigned char *f)
{
    Capture cap;
    cap.number = rt_capture_begin("frame_timing_flipper_init");

    put<uint64_t>(f, 0x0, reinterpret_cast<uintptr_t>(rt_guest(flipper_vtable)));
    const uint32_t mode = read_flip_mode(*rt::ptr<void *>(config_singleton), 4, cap);
    put<uint32_t>(f, 0x8, mode <= max_mode ? mode : max_mode);
    const uint32_t pending = read_flip_mode(*rt::ptr<void *>(config_singleton), 3, cap);
    put<uint32_t>(f, 0xc, pending <= max_mode ? pending : max_mode);
    put<uint32_t>(f, 0x10, 0);
    f[0x14] = 1;
    put<uint32_t>(f, 0x18, frame_timing::fixed_step_bits());

    Timeval tv;
    const int r = rt::fn<Gettimeofday>(gettimeofday_thunk)(&tv, nullptr);
    const uint64_t now = static_cast<uint64_t>(tv.seconds) * 1000000ull + static_cast<uint64_t>(tv.microseconds);
    for (unsigned off = 0x20; off <= 0x58; off += 8)
        put<uint64_t>(f, off, now);

    put<uint32_t>(f, 0x2b8, 0);
    f[0x276] = 0;
    put<uint16_t>(f, 0x274, 0);
    put<uint32_t>(f, 0x270, 0);
    put<uint64_t>(f, 0x268, 0);
    put<uint64_t>(f, 0x260, 0);
    put<uint32_t>(f, 0x2bc, 0xffffffffu);
    put<uint32_t>(f, 0x2c0, 0xffffffffu);
    put<uint16_t>(f, 0x2c4, 0);
    for (unsigned i = 0; i < 0x200; i += 0x10) {
        put<uint64_t>(f, 0x60 + i, 0);
        f[0x68 + i] = 0;
    }
    for (unsigned off = 0x2b0; off >= 0x278; off -= 8)
        put<uint64_t>(f, off, 0);

    if (cap.number >= 0) {
        cap.add(cap.imports, "{\"name\": \"gettimeofday\", \"argc\": 2, \"ret\": " + std::to_string(r) +
                                 ", \"writes\": [{\"arg\": 0, \"offset\": 0, \"size\": 16, \"bytes\": \"" + hex(&tv, 16) + "\"}]}");
        char head[256];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x02434520\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:flipper\"}, ", cap.number);
        const std::string json = std::string(head) + "\"buffers\": {\"flipper\": {\"size\": 712}, \"config\": {\"size\": 8}, " +
                                 cap.buffers + "}, \"memory\": [{\"addr\": \"0x0593d710\", \"pointer\": \"config\"}" +
                                 (cap.memory.empty() ? "" : ", " + cap.memory) + "], \"stubs\": [" + cap.stubs +
                                 "], \"imports\": [" + cap.imports + "]}";
        rt_capture_write("frame_timing_flipper_init", cap.number, json.c_str());
    }
}
