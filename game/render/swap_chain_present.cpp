// SPDX-License-Identifier: GPL-2.0-or-later
// Render (graphics framework): pace and submit a present (GXSwapChainCtrl).
//
// render_swap_chain_present (0x025b2fb0, once a frame) takes the present interval type (0..4).
// The first call presents right away and starts the clock. Afterwards, when pacing is on, it
// waits until `interval` more 60 Hz vblanks have passed since the last present (sleeping until
// 17 ms before that time, then polling the clock), maps the interval to the swap chain's present
// mode and presents. With a global flag set it first resets a GPU-side state. A 60 Hz display
// with interval 1 skips the wait; other intervals there report "invalid presentIntervalType."
// through the engine's fatal error, and the code goes on as the original's.
//
// "Uncap FPS++" presents with mode 0 (no vblank sync) at the end; BB_TARGET_FPS=uncapped does too.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "../frame_timing/target_fps.h"
#include "runtime/original.h"

namespace {

struct SwapChain;

struct GXSwapChainCtrl {
    SwapChain *swap_chain;
    uint8_t pacing;
    uint32_t refresh_hz;
    uint8_t unknown_0x10[0x8];
    uint64_t last_present_us;   // 0 until the first present
    uint32_t vblanks;           // vblanks counted since the clock started
};
static_assert(offsetof(GXSwapChainCtrl, pacing) == 0x8 && offsetof(GXSwapChainCtrl, refresh_hz) == 0xc);
static_assert(offsetof(GXSwapChainCtrl, last_present_us) == 0x18 && offsetof(GXSwapChainCtrl, vblanks) == 0x20);

// The GPU context the reset works on, two objects away from its singleton; the reset and the
// flag are objects inside it.
struct GpuResetPart {
    uint8_t unknown_0x0000[0xb468];
};
struct GpuFlagState {
    uint8_t unknown_0x00[0x38];
};
struct GpuContext {
    uint8_t unknown_0x0000[0x10];
    GpuResetPart reset_part;
    GpuFlagState flag_state;
};
static_assert(offsetof(GpuContext, reset_part) == 0x10 && offsetof(GpuContext, flag_state) == 0xb478);
struct GpuHolder2 {
    uint8_t unknown_0x00[0x8];
    GpuContext *context;
};
struct GpuHolder1 {
    uint8_t unknown_0x00[0x10];
    GpuHolder2 *holder;
};
static_assert(offsetof(GpuHolder2, context) == 0x8 && offsetof(GpuHolder1, holder) == 0x10);

struct Timeval {
    int64_t seconds;
    int64_t microseconds;
};

RT_ORIGINAL(0x02fbe728, c_gettimeofday, int(Timeval *, void *timezone));
RT_ORIGINAL(0x02fbfe68, kernel_usleep, int(uint32_t microseconds));
RT_ORIGINAL(0x02ad5dd0, swap_chain_present, uint64_t(SwapChain *, uint32_t mode));
RT_ORIGINAL(0x02ab9a50, gpu_flag_get, uint8_t(GpuFlagState *));
RT_ORIGINAL(0x02ab9a40, gpu_flag_clear, void(GpuFlagState *));
RT_ORIGINAL(0x02aafa00, gpu_reset, void(GpuResetPart *));
RT_GLOBAL(0x0553ac86, gpu_reset_requested, uint8_t);
RT_GLOBAL(0x059406c8, gpu_holder_instance, GpuHolder1 *);
RT_GLOBAL(0x04d40153, source_file, const char);
RT_GLOBAL(0x04d40199, message_invalid_interval, const char);   // "invalid presentIntervalType."
constexpr int32_t line_invalid_interval_unpaced = 0xbf, line_invalid_interval = 0xcf;

constexpr uint32_t refresh_60_hz = 60, vblanks_per_second = 60;
constexpr uint32_t min_paced_interval = 2, max_interval = 4;
// The swap chain's present modes: right away, or at the next vblank.
constexpr uint32_t present_immediate = 0, present_vsync = 1;
constexpr uint64_t us_per_second = 1000000;
// Sleep first when at least 17.001 ms remain (the original compares the remaining microseconds
// times 10^6), for the remaining time less 17 ms.
constexpr uint64_t sleep_threshold_scaled = 17001000000, sleep_margin_us = 17000;

uint64_t now_us()
{
    Timeval tv;
    c_gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.seconds) * us_per_second + static_cast<uint64_t>(tv.microseconds);
}

void report_invalid_interval(int32_t line)
{
    engine::fatal_error(source_file.address(), line, message_invalid_interval.address());
}

void present(GXSwapChainCtrl *self, uint32_t mode)
{
    if (frame_timing::target_fps() == frame_timing::Target::uncapped) mode = present_immediate;
    swap_chain_present(self->swap_chain, mode);
}

// A 60 Hz display needs no pacing for interval 1; intervals 2..4 are paced; others are invalid.
bool unpaced_on_60_hz(const GXSwapChainCtrl *self, uint32_t interval)
{
    return self->refresh_hz == refresh_60_hz && (interval < min_paced_interval || interval > max_interval) && interval != 0;
}

void reset_gpu_state()
{
    GpuContext *gpu = gpu_holder_instance.get()->holder->context;
    if (gpu_flag_get(&gpu->flag_state)) gpu_flag_clear(&gpu->flag_state);
    gpu_reset(&gpu->reset_part);
}

// Waits until `interval` vblanks after the last present, then restarts the count from now.
void wait_for_vblanks(GXSwapChainCtrl *self, uint32_t interval)
{
    const uint32_t vblanks = self->vblanks + interval;
    uint64_t now = now_us();
    const uint64_t target = static_cast<uint64_t>(interval) * us_per_second / vblanks_per_second + self->last_present_us;
    if (target > now) {
        const uint64_t remaining_scaled = (target - now) * us_per_second;
        if (remaining_scaled >= sleep_threshold_scaled) {
            const auto remaining_us = static_cast<uint32_t>(remaining_scaled / us_per_second);
            const auto sleep_us = static_cast<int32_t>(remaining_us - static_cast<uint32_t>(sleep_margin_us));
            if (sleep_us > 0) kernel_usleep(static_cast<uint32_t>(sleep_us));
            now = now_us();
        }
    }
    while (target > now) now = now_us();
    self->last_present_us = now_us();
    self->vblanks = vblanks;
}

}  // namespace

extern "C" void bb_render_swap_chain_present(GXSwapChainCtrl *self, uint32_t interval)
{
    if (self->last_present_us == 0) {
        swap_chain_present(self->swap_chain, interval);   // the first present: no pacing, no option
        self->last_present_us = now_us();
        self->vblanks = 0;
        return;
    }

    if (gpu_reset_requested.get() && self->pacing && interval != 0) {
        if (unpaced_on_60_hz(self, interval)) {
            if (interval != 1) report_invalid_interval(line_invalid_interval);
        } else {
            reset_gpu_state();
        }
    }

    if (!self->pacing || interval == 0) return present(self, interval);
    if (unpaced_on_60_hz(self, interval)) {
        if (interval != 1) report_invalid_interval(line_invalid_interval);
        return present(self, interval);
    }

    wait_for_vblanks(self, interval);

    // The present mode: the wait did the pacing, so the mode only says whether to sync.
    if (self->refresh_hz != refresh_60_hz && self->pacing) return present(self, present_immediate);
    if (!self->pacing) {
        if (interval <= max_interval) return present(self, interval == 0 ? present_immediate : present_vsync);
        report_invalid_interval(line_invalid_interval_unpaced);
    }
    if (interval > max_interval) {
        report_invalid_interval(line_invalid_interval);
        return present(self, interval);
    }
    present(self, interval == 1 ? present_vsync : present_immediate);
}
