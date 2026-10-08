// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the frame limiter (SprjFlipper's per-frame wait).
//
// frame_timing_pace_frame (0x02434770) runs once per frame from frame_timing_frame_step. It
// applies the flip mode (the pending one if there is one, else the current one; read from the
// Game.FlipMode config by frame_timing_flipper_init), waits until the frame's time budget is
// used, records the frame in a 32-entry ring and a 16-entry history, and derives the frame rate.
// In this build the wait-mode query always returns 1, so only the spinning wait runs; the
// sleeping wait is kept for fidelity.
//
// BB_TARGET_FPS (our option, not in the original; target_fps.h): unset keeps the original
// behaviour exactly. 30, 60 and uncapped do what the community patches "30 FPS++", "60 FPS++" and
// "Uncap FPS++" do to this function (checked against them: verify.py run --patch ... --env ...):
// a fixed mode (interval 1/30, 1/60 or 1/240 s), no overrides, and the sync interval kept on late
// frames. The frame step and the flipper constructor apply the rest of those patches.
//
// BB_LIMITER_WAIT=sleep (our option): use the sleeping wait the game carries but never selects,
// instead of the spinning one: it sleeps until about 5 ms before the frame's end and spins only
// the rest, so a fast machine does not keep a core busy. Only the wait changes; unset keeps the
// original's spinning wait.
//
// Fidelity: every conversion and comparison mirrors the original instruction (engine/scalar.h).
// Build with -ffp-contract=off (game/CMakeLists.txt).
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../engine/engine.h"
#include "../engine/scalar.h"
#include "flipper.h"
#include "runtime/original.h"
#include "target_fps.h"

namespace {

using namespace frame_timing;

struct Timeval {
    int64_t seconds;
    int64_t microseconds;
};

// An object told when the game runs at a 60 FPS target.
struct FpsNotify {
    uint8_t unknown_0x00[0x4c];
    uint8_t flag_a, flag_b;
    uint32_t target_fps;
};
static_assert(offsetof(FpsNotify, flag_a) == 0x4c && offsetof(FpsNotify, target_fps) == 0x50);
constexpr uint32_t notify_fps = 60;

struct SprjWindow;
RT_GLOBAL(0x05940500, window_instance, SprjWindow *);
RT_GLOBAL(0x04d3b00c, window_name, const char);
RT_GLOBAL(0x05940e00, notify_instance, FpsNotify *);
RT_ORIGINAL(0x013e3980, spinning_wait_only, uint8_t());   // returns 1 in this build
RT_ORIGINAL(0x02fbe728, c_gettimeofday, int(Timeval *, void *timezone));
RT_ORIGINAL(0x02fbfe68, kernel_usleep, int(uint32_t microseconds));

// Constants of the original (read-only data)
constexpr float us_per_second = 1000000.0f, minus_us_per_second = -1000000.0f;
constexpr double us_per_second_double = 1000000.0;
constexpr uint64_t us_per_second_integer = 1000000;
constexpr float min_wait = 0.001f, sleep_threshold = 0.005f, sleep_margin = -0.005f;
constexpr float thousand = 1000.0f, late_tolerance = 0.01f, history_frames = 16.0f;
constexpr uint32_t interval_240_bits = 0x3b888889;   // 1/240 s: the Uncap FPS++ patch's interval
constexpr uint64_t min_budget_divisor = 3;            // catching up never cuts below a third
constexpr uint32_t ring_mask = frame_ring_size - 1;

// What each flip mode sets: sync interval, the mode flag, the interval, and the two windows the
// catch-up looks back over.
struct FlipModeSettings {
    uint32_t sync_interval;
    uint8_t mode_flag;
    uint32_t interval_bits;
    uint32_t late_window_spin, late_window_sleep;
};
constexpr FlipModeSettings flip_modes[] = {
    {2, 1, k_interval_30, 1, 30},
    {2, 1, k_interval_30, 0, 30},
    {1, 1, k_interval_60, 0, 30},
    {1, 0, k_interval_30, 1, 30},
    {1, 0, k_interval_30, 0, 30},
};
constexpr uint32_t flip_mode_count = sizeof flip_modes / sizeof flip_modes[0];

void set_mode(SprjFlipper *f, uint32_t sync_interval, uint8_t mode_flag, uint32_t interval_bits, uint32_t spin,
              uint32_t sleep)
{
    f->sync_interval = sync_interval;
    f->mode_flag_0x14 = mode_flag;
    f->interval = std::bit_cast<float>(interval_bits);
    f->late_window_spin = spin;
    f->late_window_sleep = sleep;
}

bool sleeping_wait()
{
    static const bool sleep = [] {
        const char *v = std::getenv("BB_LIMITER_WAIT");
        if (!v || !*v || !std::strcmp(v, "spin")) return false;
        if (!std::strcmp(v, "sleep")) return true;
        std::fprintf(stderr, "frame limiter: BB_LIMITER_WAIT=%s is not sleep or spin; ignored\n", v);
        return false;
    }();
    return sleep;
}

uint64_t now_us()
{
    Timeval tv;
    c_gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.seconds) * us_per_second_integer + static_cast<uint64_t>(tv.microseconds);
}

uint64_t seconds_to_us(float seconds) { return engine::double_to_u64(static_cast<double>(seconds) * us_per_second_double); }

// Applies the flip mode, the option's fixed mode, the reset and the overrides. Returns whether
// the reset ran (it skips this frame's wait).
bool apply_mode(SprjFlipper *f, Target target)
{
    const uint8_t pending = f->has_pending_mode;
    f->used_pending_mode = pending;
    f->has_pending_mode = 0;
    const uint32_t mode = pending ? f->pending_flip_mode : f->flip_mode;
    if (target != Target::original) {
        // As the community patches rewrite this part: always one fixed mode (for 30 and 60 only
        // while the real mode is in range), with no catch-up windows, and none of the overrides
        // below (the reset flag is neither honoured nor cleared).
        if (target == Target::uncapped)
            set_mode(f, 1, 0, interval_240_bits, 0, 0);
        else if (mode < flip_mode_count)
            set_mode(f, 1, 1, target == Target::fps30 ? k_interval_30 : k_interval_60, 0, 0);
        return false;
    }
    if (mode < flip_mode_count) {
        const FlipModeSettings &m = flip_modes[mode];
        set_mode(f, m.sync_interval, m.mode_flag, m.interval_bits, m.late_window_spin, m.late_window_sleep);
    }
    bool reset = false;
    if (f->reset_mode) {
        set_mode(f, 1, 0, k_interval_30, 0, 0);
        f->reset_mode = 0;
        reset = true;
    }
    if (f->late_window_spin_override >= 0) f->late_window_spin = static_cast<uint32_t>(f->late_window_spin_override);
    if (f->late_window_sleep_override >= 0) f->late_window_sleep = static_cast<uint32_t>(f->late_window_sleep_override);
    if (f->reset_late_windows) {
        f->late_window_spin = 0;
        f->late_window_sleep = 0;
        f->reset_late_windows = 0;
    }
    if (f->no_late_windows) {
        f->late_window_spin = 0;
        f->late_window_sleep = 0;
    }
    return reset;
}

// The frame's budget: the interval, shortened while recent frames ran late (never below a third).
// late_sum and counted are the recent late frames' total time and how many were looked at.
uint64_t frame_budget_us(const SprjFlipper *f, uint32_t window, uint64_t &late_sum, uint32_t &counted)
{
    uint64_t budget_us = seconds_to_us(f->interval);
    late_sum = 0;
    counted = 0;
    for (uint32_t back = 0; counted < window; back++) {
        const FrameRecord &record = f->ring[(f->ring_index - back) & ring_mask];
        late_sum += record.frame_us;
        counted++;
        if (!record.late) break;
    }
    if (!f->catching_up) return budget_us;
    uint64_t limit = static_cast<uint64_t>(counted + 1) * budget_us;
    if (late_sum + budget_us <= limit) return budget_us;
    const uint64_t third = budget_us / min_budget_divisor;
    if (limit <= late_sum) return third;
    limit -= late_sum;
    if (budget_us < limit) limit = budget_us;
    if (third > limit) limit = third;
    return limit;
}

// Waits out the rest of the budget: spins on the clock, sleeping first when the game would (or
// BB_LIMITER_WAIT=sleep says so) and there is time.
void wait(SprjFlipper *f, uint64_t budget_us, bool spin_only, uint64_t &now)
{
    const float budget_s = engine::u64_to_float(budget_us) / us_per_second;
    float remaining = budget_s - engine::u64_to_float(now - f->frame_start_us) / us_per_second;
    if (0.0f > remaining) return;
    for (;;) {
        const float wait_s = engine::min_of(budget_s, remaining);
        if (!(wait_s > 0.0f)) return;
        if ((!spin_only || sleeping_wait()) && wait_s > min_wait && wait_s > sleep_threshold) {
            const int32_t us = engine::truncate(((wait_s + sleep_margin) * thousand) * thousand);
            if (us > 0) kernel_usleep(static_cast<uint32_t>(us));
        }
        now = now_us();
        f->frame_end_us = now;
        remaining = budget_s + engine::u64_to_float(now - f->frame_start_us) / minus_us_per_second;
        if (0.0f > remaining) return;
    }
}

// The frame rate from the last 16 frame times (summed in the original's order).
void record_history(SprjFlipper *f, float frame_s)
{
    float sum = f->history[1];
    f->history[0] = sum;
    sum = sum + 0.0f;
    for (int i = 1; i + 1 < frame_history_size; i++) {
        const float v = f->history[i + 1];
        f->history[i] = v;
        sum = engine::add(sum, v);   // both can be NaN: the original's order (NaN payload)
    }
    f->history[frame_history_size - 1] = frame_s;
    const float total = sum + frame_s;
    f->frame_rate = total > min_wait ? history_frames / total : 0.0f;
}

}  // namespace

extern "C" void bb_frame_timing_pace_frame(SprjFlipper *f)
{
    if (!window_instance.get()) engine::report_missing(window_name.address());
    const bool spin_only = spinning_wait_only();
    const Target target = target_fps();
    const bool reset = apply_mode(f, target);

    // The previous frame's end is this frame's start.
    f->frame_start_us = f->frame_end_us;
    uint64_t now = now_us();
    f->frame_end_us = now;

    const uint32_t window = spin_only ? f->late_window_spin : f->late_window_sleep;
    uint64_t late_sum;
    uint32_t counted;
    const uint64_t budget_us = frame_budget_us(f, window, late_sum, counted);
    if (!reset && !f->skip_wait) wait(f, budget_us, spin_only, now);

    // Record the frame in the ring.
    if (f->skip_wait) f->catching_up = 1;
    f->ring_index = (f->ring_index + 1) & ring_mask;
    FrameRecord &record = f->ring[f->ring_index];
    record.frame_us = now - f->frame_start_us;
    const uint8_t was_late = f->catching_up;
    record.late = was_late;

    const uint64_t frame_us = f->frame_end_us - f->frame_start_us;
    const float frame_s = engine::u64_to_float(frame_us) / us_per_second;
    f->frame_seconds = frame_s;

    // On time: within the interval (with 1% tolerance) for this frame and the late ones before it.
    uint8_t window_used = window == 0;
    if (was_late) window_used = counted >= window;
    const float interval = f->interval;
    const uint64_t tolerance_us = seconds_to_us(interval * late_tolerance);
    const uint64_t allowed = static_cast<uint64_t>(counted + 1) * seconds_to_us(interval) + tolerance_us;
    uint8_t on_time = frame_us + late_sum < allowed ? 1 : window_used;
    if (f->skip_wait) on_time = 0;
    f->catching_up = on_time ^ 1;
    // The patches keep the sync interval when the frame was late (the original zeroes it).
    if (!on_time && target == Target::original) f->sync_interval = 0;

    record_history(f, frame_s);

    f->skip_wait = 0;
    if (FpsNotify *notify = notify_instance.get(); notify && f->notify_60_fps) {
        notify->flag_a = 1;
        notify->flag_b = 1;
        notify->target_fps = notify_fps;
    }
    f->notify_60_fps = 0;
}
