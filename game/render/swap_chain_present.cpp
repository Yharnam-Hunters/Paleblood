// SPDX-License-Identifier: GPL-2.0-or-later
// Render (graphics framework): pace and submit a present (GXSwapChainCtrl).
//
// render_swap_chain_present (0x025b2fb0, once a frame) takes the present interval type (0..4).
// The first call presents right away and starts the clock. Afterwards, when pacing is on (+0x8),
// it waits until `interval` more 60 Hz vblanks have passed since the last present (sleeping until
// 17 ms before that time, then polling gettimeofday), maps the interval to the swap chain's flip
// mode with two small tables and presents through 0x02ad5dd0(swap chain, mode). With a global
// flag set (0x0553ac86) it first resets a GPU-side state (0x059406c8 -> +0x10 -> +0x8). A 60 Hz
// mode (+0xc == 60) with interval 1 skips the wait; other intervals there report "invalid
// presentIntervalType." through the engine's fatal error, and the code goes on as the original's.
//
// "Uncap FPS++" presents with mode 0 (no vblank sync) at the end; BB_TARGET_FPS=uncapped does too.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../frame_timing/target_fps.h"
#include "runtime/capture.h"
#include "runtime/guest.h"

namespace {

constexpr uint32_t gettimeofday_thunk = 0x02fbe728, usleep_thunk = 0x02fbfe68;
constexpr uint32_t present = 0x02ad5dd0, gpu_flag_get = 0x02ab9a50, gpu_flag_clear = 0x02ab9a40, gpu_reset = 0x02aafa00;
constexpr uint32_t fatal_error = 0x024b55b0;
constexpr uint32_t gpu_reset_flag = 0x0553ac86, gpu_singleton = 0x059406c8;
constexpr uint32_t str_file = 0x04d40153, str_invalid_interval = 0x04d40199;
constexpr uint8_t flip_table_unpaced[5] = {0, 1, 1, 1, 1};   // jump table at 0x025b32b8: 2..4 -> 1
constexpr uint8_t flip_table_60hz[5] = {0, 1, 0, 0, 0};      // jump table at 0x025b32cc: 2..4 -> 0

struct Timeval {
    int64_t seconds;
    int64_t microseconds;
};

using Gettimeofday = int(Timeval *, void *);
using Usleep = int(uint32_t);
using Present = uint64_t(void *swap_chain, uint32_t mode);
using Byte1 = uint8_t(void *);
using Void1 = void(void *);
using Fatal = void(const char *, int, const char *, ...);

template <typename T> T get(const unsigned char *p, unsigned off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }
template <typename T> void put(unsigned char *p, unsigned off, T v) { std::memcpy(p + off, &v, sizeof v); }

struct Capture {
    int number = -1;
    std::string clock, imports, stubs;
    int usleeps = 0;
    void stub(const std::string &e) { stubs += (stubs.empty() ? "" : ", ") + e; }
};

uint64_t now_us(Capture &cap)
{
    Timeval tv;
    rt::fn<Gettimeofday>(gettimeofday_thunk)(&tv, nullptr);
    const uint64_t us = static_cast<uint64_t>(tv.seconds) * 1000000u + static_cast<uint64_t>(tv.microseconds);
    if (cap.number >= 0) cap.clock += (cap.clock.empty() ? "" : ",") + std::to_string(us);
    return us;
}

void fatal(int line, Capture &cap)
{
    rt::fn<Fatal>(fatal_error)(rt::ptr<const char>(str_file), line, rt::ptr<const char>(str_invalid_interval));
    if (cap.number >= 0) cap.stub("{\"address\": \"0x024b55b0\", \"argc\": 3}");
}

}  // namespace

extern "C" void bb_render_swap_chain_present(unsigned char *self, uint32_t interval)
{
    Capture cap;
    cap.number = rt_capture_begin("render_swap_chain_present");
    unsigned char before[0x28];
    std::memcpy(before, self, sizeof before);
    const uint8_t gpu_flag_before = *rt::ptr<uint8_t>(gpu_reset_flag);
    unsigned char *gpu_base = nullptr;
    uint8_t gpu_flag_value = 0;

    auto write = [&](uint32_t mode) {
        if (cap.number < 0) return;
        char head[600];
        uint64_t next;
        uint32_t frames, refresh;
        std::memcpy(&next, before + 0x18, 8);
        std::memcpy(&frames, before + 0x20, 4);
        std::memcpy(&refresh, before + 0xc, 4);
        std::snprintf(head, sizeof head,
                      "{\"schema\": 1, \"address\": \"0x025b2fb0\", \"id\": \"capture_%04d\", \"returns\": \"void\", "
                      "\"args\": {\"rdi\": \"buf:self\", \"rsi\": \"0x%x\"}, \"note\": \"mode %u\", "
                      "\"buffers\": {\"self\": {\"size\": 40}, \"swapchain\": {\"size\": 8}, \"gpu1\": {\"size\": 24}, "
                      "\"gpu2\": {\"size\": 16}, \"gpu\": {\"size\": 46256}}, \"memory\": ["
                      "{\"addr\": \"buf:self\", \"pointer\": \"swapchain\"}, {\"addr\": \"buf:self+8\", \"bytes\": \"%02x\"}, "
                      "{\"addr\": \"buf:self+12\", \"bytes\": \"%02x%02x%02x%02x\"}, "
                      "{\"addr\": \"buf:self+24\", \"bytes\": \"%016llx\"}, {\"addr\": \"buf:self+32\", \"bytes\": \"%02x%02x%02x%02x\"}, "
                      "{\"addr\": \"0x0553ac86\", \"bytes\": \"%02x\"}",
                      cap.number, interval, mode, before[8], refresh & 0xff, (refresh >> 8) & 0xff, (refresh >> 16) & 0xff,
                      refresh >> 24, static_cast<unsigned long long>(__builtin_bswap64(next)), frames & 0xff,
                      (frames >> 8) & 0xff, (frames >> 16) & 0xff, frames >> 24, gpu_flag_before);
        std::string mem = head;
        if (gpu_base) {
            char b[200];
            std::snprintf(b, sizeof b,
                          ", {\"addr\": \"0x059406c8\", \"pointer\": \"gpu1\"}, {\"addr\": \"buf:gpu1+16\", \"pointer\": \"gpu2\"}, "
                          "{\"addr\": \"buf:gpu2+8\", \"pointer\": \"gpu\"}, {\"addr\": \"buf:gpu+46236\", \"bytes\": \"%02x\"}",
                          gpu_flag_value);
            mem += b;
        }
        std::string imports;
        if (!cap.clock.empty())
            imports = "{\"name\": \"gettimeofday\", \"argc\": 2, \"ret\": 0, \"series\": {\"arg\": 0, \"format\": \"timeval_us\", "
                      "\"values\": [" + cap.clock + "]}}";
        for (int i = 0; i < cap.usleeps; i++)
            imports += std::string(imports.empty() ? "" : ", ") + "{\"name\": \"sceKernelUsleep\", \"argc\": 1, \"ret\": 0}";
        const std::string json = mem + "], \"stubs\": [" + cap.stubs + "], \"imports\": [" + imports + "]}";
        rt_capture_write("render_swap_chain_present", cap.number, json.c_str());
    };
    auto submit = [&](uint32_t mode) {
        if (frame_timing::target_fps() == frame_timing::Target::uncapped) mode = 0;
        rt::fn<Present>(present)(get<void *>(self, 0x0), mode);
        if (cap.number >= 0) cap.stub("{\"address\": \"0x02ad5dd0\", \"argc\": 2}");
        write(mode);
    };
    const auto refresh60 = [&] { return get<uint32_t>(self, 0xc) == 60; };
    const auto outside_2_4 = [&] { return interval - 2u >= 3u; };

    if (get<uint64_t>(self, 0x18) == 0) {
        rt::fn<Present>(present)(get<void *>(self, 0x0), interval);
        if (cap.number >= 0) cap.stub("{\"address\": \"0x02ad5dd0\", \"argc\": 2}");
        put<uint64_t>(self, 0x18, now_us(cap));
        put<uint32_t>(self, 0x20, 0);
        write(interval);
        return;
    }

    bool skip_gpu = false;
    if (*rt::ptr<uint8_t>(gpu_reset_flag) && self[0x8] && interval != 0) {
        if (refresh60() && outside_2_4() && interval != 0) {
            skip_gpu = true;
            if (interval != 1) fatal(0xcf, cap);
        }
        if (!skip_gpu) {
            unsigned char *a = *rt::ptr<unsigned char *>(gpu_singleton);
            unsigned char *b = get<unsigned char *>(a, 0x10);
            gpu_base = get<unsigned char *>(b, 0x8);
            unsigned char *state = gpu_base + 0xb478;
            gpu_flag_value = rt::fn<Byte1>(gpu_flag_get)(state);
            if (cap.number >= 0) cap.stub("{\"address\": \"0x02ab9a50\", \"argc\": 1, \"ret\": " + std::to_string(gpu_flag_value) + "}");
            if (gpu_flag_value) {
                rt::fn<Void1>(gpu_flag_clear)(state);
                if (cap.number >= 0) cap.stub("{\"address\": \"0x02ab9a40\", \"argc\": 1}");
            }
            rt::fn<Void1>(gpu_reset)(gpu_base + 0x10);
            if (cap.number >= 0) cap.stub("{\"address\": \"0x02aafa00\", \"argc\": 1}");
        }
    }

    if (!self[0x8] || interval == 0) {
        submit(interval);
        return;
    }
    if (refresh60() && outside_2_4() && interval != 0) {
        if (interval == 1) {
            submit(1);
            return;
        }
        fatal(0xcf, cap);
        submit(interval);
        return;
    }

    // Wait until `interval` more 60 Hz vblanks have passed since the last present.
    const uint32_t frames = get<uint32_t>(self, 0x20) + interval;
    uint64_t now = now_us(cap);
    const uint64_t elapsed_us = static_cast<uint64_t>(frames - get<uint32_t>(self, 0x20)) * 1000000u;
    const uint64_t target = elapsed_us / 60u + get<uint64_t>(self, 0x18);
    if (target > now) {
        const uint64_t scaled = (target - now) * 1000000u;
        if ((scaled >> 6) >= 0xfd55ab1u) {
            const int32_t sleep_us = static_cast<int32_t>(static_cast<uint32_t>(scaled / 1000000u) + 0xffffbd98u);
            if (sleep_us > 0) {
                rt::fn<Usleep>(usleep_thunk)(static_cast<uint32_t>(sleep_us));
                cap.usleeps++;
            }
            now = now_us(cap);
        }
    }
    while (target > now) now = now_us(cap);
    put<uint64_t>(self, 0x18, now_us(cap));
    put<uint32_t>(self, 0x20, frames);

    const uint8_t pacing = self[0x8];
    if (!refresh60() && pacing) {
        submit(0);
        return;
    }
    if (!pacing) {
        if (interval <= 4) {
            submit(flip_table_unpaced[interval] == 1 && interval >= 2 ? 1 : interval);
            return;
        }
        rt::fn<Fatal>(fatal_error)(rt::ptr<const char>(str_file), 0xbf, rt::ptr<const char>(str_invalid_interval));
        if (cap.number >= 0) cap.stub("{\"address\": \"0x024b55b0\", \"argc\": 3}");
    }
    if (interval > 4) {
        fatal(0xcf, cap);
        submit(interval);
        return;
    }
    submit(flip_table_60hz[interval] == 0 && interval >= 2 ? 0 : interval);
}
