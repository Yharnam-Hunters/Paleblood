// SPDX-License-Identifier: GPL-2.0-or-later
// SprjFlipper: the frame limiter's object (built by frame_timing_flipper_init, created on first
// use by frame_timing_frame_step, run once a frame by frame_timing_pace_frame, which documents
// what the fields do). Times are microseconds from gettimeofday; intervals are seconds.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace frame_timing {

constexpr int frame_ring_size = 32;      // recent frames, indexed by ring_index
constexpr int frame_history_size = 16;   // recent frame times, for the frame rate
constexpr int clock_stamp_count = 6;

struct FrameRecord {
    uint64_t frame_us;
    uint8_t late;
    uint8_t unknown_0x09[0x7];
};
static_assert(sizeof(FrameRecord) == 0x10);

struct SprjFlipper {
    const void *vtable;
    uint32_t flip_mode;           // from Game.FlipMode, 0..4
    uint32_t pending_flip_mode;   // applied on the next frame when has_pending_mode is set
    uint32_t sync_interval;
    uint8_t mode_flag_0x14;       // set by the flip mode; meaning not known
    float interval;               // the frame's time budget, seconds
    uint64_t frame_start_us;
    uint64_t frame_end_us;
    uint64_t clock_stamps[clock_stamp_count];   // stamped with the clock at construction
    FrameRecord ring[frame_ring_size];
    uint32_t ring_index;
    float frame_seconds;          // the last frame's measured time
    uint32_t late_window_spin;    // how many late frames the catch-up looks back (spinning wait)
    uint32_t late_window_sleep;   // the same for the sleeping wait
    uint8_t catching_up;          // the last frame ran late
    uint8_t reset_late_windows;
    uint8_t no_late_windows;      // set by the frame step when it creates the flipper
    uint8_t skip_wait;
    uint8_t notify_60_fps;
    uint8_t used_pending_mode;
    uint8_t has_pending_mode;
    float history[frame_history_size];
    float frame_rate;
    int32_t late_window_spin_override;    // -1: none
    int32_t late_window_sleep_override;   // -1: none
    uint8_t reset_mode;
    uint8_t flag_0x2c5;
};
static_assert(offsetof(SprjFlipper, flip_mode) == 0x8 && offsetof(SprjFlipper, sync_interval) == 0x10);
static_assert(offsetof(SprjFlipper, mode_flag_0x14) == 0x14 && offsetof(SprjFlipper, interval) == 0x18);
static_assert(offsetof(SprjFlipper, frame_start_us) == 0x20 && offsetof(SprjFlipper, clock_stamps) == 0x30);
static_assert(offsetof(SprjFlipper, ring) == 0x60 && offsetof(SprjFlipper, ring_index) == 0x260);
static_assert(offsetof(SprjFlipper, frame_seconds) == 0x264 && offsetof(SprjFlipper, late_window_spin) == 0x268);
static_assert(offsetof(SprjFlipper, catching_up) == 0x270 && offsetof(SprjFlipper, no_late_windows) == 0x272);
static_assert(offsetof(SprjFlipper, notify_60_fps) == 0x274 && offsetof(SprjFlipper, has_pending_mode) == 0x276);
static_assert(offsetof(SprjFlipper, history) == 0x278 && offsetof(SprjFlipper, frame_rate) == 0x2b8);
static_assert(offsetof(SprjFlipper, late_window_spin_override) == 0x2bc && offsetof(SprjFlipper, reset_mode) == 0x2c4);
static_assert(sizeof(SprjFlipper) == 0x2c8);

RT_GLOBAL(0x059404f8, flipper_instance, SprjFlipper *);
RT_GLOBAL(0x04d3acac, flipper_name, const char);

}  // namespace frame_timing
