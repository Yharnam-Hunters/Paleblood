// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: a per-frame task that hands the frame time to its parts.
//
// frame_timing_task_013d3520 (0x013d3520, called once a frame in gameplay) takes the frame-time
// descriptor ({descriptor vtable, seconds}): a begin call (0x01456eb0(0)), then its part at
// +0x70 (0x01456c80), the descriptor to its part at +0x18 (0x013daac0), the seconds to its part
// at +0x10 (0x013d5440, float in xmm0), each only when present, then two end calls (0x01456ed0(0)
// and a tail call of 0x01456ef0). What the parts are is not known yet.
//
// BB_TARGET_FPS=uncapped does what "Uncap FPS++" does here: the seconds are read, the caller's
// descriptor is set to 1.0, the seconds are divided by that 1.0 (vdivss), and 0x013d5440 is
// called even when the +0x10 part is missing.
#include <immintrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/capture.h"
#include "runtime/guest.h"
#include "target_fps.h"

namespace {

constexpr uint32_t begin_fn = 0x01456eb0, part_70_fn = 0x01456c80, part_18_fn = 0x013daac0,
                   part_10_fn = 0x013d5440, end_fn = 0x01456ed0, tail_fn = 0x01456ef0;

using Int1 = void(int);
using Ptr1 = void(void *);
using Ptr2 = void(void *, void *);
using PtrFloat = void(void *, float);
using Void0 = void();

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

}  // namespace

extern "C" void bb_frame_timing_task_013d3520(unsigned char *self, unsigned char *step)
{
    const int number = rt_capture_begin("frame_timing_task_013d3520");
    std::string memory, stubs;
    if (number >= 0) {
        char b[64];
        std::snprintf(b, sizeof b, "0x%08x", guest_address(get<void *>(step, 0)));
        memory = std::string("{\"addr\": \"buf:step\", \"guest\": \"") + b + "\"}, {\"addr\": \"buf:step+8\", \"bytes\": \"" +
                 hex(step + 8, 4) + "\"}";
    }
    auto part = [&](unsigned off) -> void * {
        void *p = get<void *>(self, off);
        if (number >= 0) {
            const std::string name = "part" + std::to_string(off);
            memory += p ? ", {\"addr\": \"buf:self+" + std::to_string(off) + "\", \"pointer\": \"" + name + "\"}"
                        : ", {\"addr\": \"buf:self+" + std::to_string(off) + "\", \"bytes\": \"0000000000000000\"}";
        }
        return p;
    };

    rt::fn<Int1>(begin_fn)(0);
    stubs = "{\"address\": \"0x01456eb0\", \"argc\": 1}";
    if (void *p70 = part(0x70)) {
        rt::fn<Ptr1>(part_70_fn)(p70);
        stubs += ", {\"address\": \"0x01456c80\", \"argc\": 1}";
    }
    if (void *p18 = part(0x18)) {
        rt::fn<Ptr2>(part_18_fn)(p18, step);
        stubs += ", {\"address\": \"0x013daac0\", \"argc\": 2, \"argmem\": [{\"arg\": 1, \"size\": 12}]}";
    }
    void *p10 = part(0x10);
    if (frame_timing::target_fps() == frame_timing::Target::uncapped) {
        __m128 seconds = _mm_load_ss(reinterpret_cast<const float *>(step + 8));
        put<float>(step, 8, 1.0f);
        seconds = _mm_div_ss(seconds, _mm_load_ss(reinterpret_cast<const float *>(step + 8)));
        rt::fn<PtrFloat>(part_10_fn)(p10, _mm_cvtss_f32(seconds));
        stubs += ", {\"address\": \"0x013d5440\", \"argc\": 1, \"argf32\": [0]}";
    } else if (p10) {
        rt::fn<PtrFloat>(part_10_fn)(p10, get<float>(step, 8));
        stubs += ", {\"address\": \"0x013d5440\", \"argc\": 1, \"argf32\": [0]}";
    }
    rt::fn<Int1>(end_fn)(0);
    stubs += ", {\"address\": \"0x01456ed0\", \"argc\": 1}, {\"address\": \"0x01456ef0\"}";

    if (number >= 0) {
        char head[256];
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x013d3520\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:self\", \"rsi\": \"buf:step\"}, ", number);
        const std::string json = std::string(head) +
            "\"buffers\": {\"self\": {\"size\": 120}, \"step\": {\"size\": 16}, \"part16\": {\"size\": 8}, "
            "\"part24\": {\"size\": 8}, \"part112\": {\"size\": 8}}, \"memory\": [" + memory + "], \"stubs\": [" +
            stubs + "], \"imports\": []}";
        rt_capture_write("frame_timing_task_013d3520", number, json.c_str());
    }
    rt::fn<Void0>(tail_fn)();
}
