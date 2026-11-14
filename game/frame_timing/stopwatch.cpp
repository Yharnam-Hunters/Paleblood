// SPDX-License-Identifier: GPL-2.0-or-later
// A stopwatch embedded in a frame-timing object: start stores the current time in seconds
// (relative to the game's start time) unless it is already running; stop adds the time since
// start to two running totals and clears the start. Both are called from the task frame around
// 0x013d4412..0x013d46c4 and tail-called from 0x01456ec6/0x01456ee6.
//
// frame_timing_stopwatch_frame_end_014486d0 (the profile's frame end, task_parts.cpp) closes a
// frame: every 30 frames the first total and start count become per-frame averages and start
// again from zero; every frame the second start count and total are folded into their peaks and
// cleared.
#include <cstddef>
#include <cstdint>

#include "../engine/scalar.h"
#include "time_of_day.h"

namespace {

using namespace frame_timing::time_of_day;

struct Stopwatch {
    uint8_t unknown_0x00[0x38];
    float start_seconds;   // 0 while stopped
    float total_seconds;          // since the last average
    float average_seconds;        // per frame, over the last 30 frames
    uint32_t start_count;         // since the last average
    float average_starts;         // per frame, over the last 30 frames
    uint32_t start_count_2;       // starts this frame (the frame end clears it)
    int32_t peak_start_count_2;   // the most starts in one frame
    float total_seconds_2;        // seconds this frame (the frame end clears it)
    float peak_total_seconds_2;   // the most seconds in one frame
    int32_t frame_count;          // frames since the last average
};
static_assert(offsetof(Stopwatch, start_seconds) == 0x38 && offsetof(Stopwatch, total_seconds) == 0x3c);
static_assert(offsetof(Stopwatch, average_seconds) == 0x40 && offsetof(Stopwatch, start_count) == 0x44);
static_assert(offsetof(Stopwatch, average_starts) == 0x48 && offsetof(Stopwatch, start_count_2) == 0x4c);
static_assert(offsetof(Stopwatch, peak_start_count_2) == 0x50 && offsetof(Stopwatch, total_seconds_2) == 0x54);
static_assert(offsetof(Stopwatch, peak_total_seconds_2) == 0x58 && offsetof(Stopwatch, frame_count) == 0x5c);

constexpr int32_t frames_per_average = 30;

// Microseconds of the time of day when the game started.
RT_GLOBAL(0x05940660, game_start_microseconds, const uint64_t);
constexpr double microseconds_per_second_double = 1000000.0;

// Seconds since the game started, as a float: the unsigned microsecond difference converted to
// double, divided in double, and only then rounded to float.
float seconds_since_start()
{
    const uint64_t elapsed = now_microseconds() - game_start_microseconds.get();
    return static_cast<float>(static_cast<double>(elapsed) / microseconds_per_second_double);
}

}  // namespace

// Starts only from exactly zero (either sign); a NaN start counts as running.
extern "C" void bb_frame_timing_stopwatch_start(Stopwatch *watch)
{
    if (!(watch->start_seconds == 0.0f)) return;
    watch->start_seconds = seconds_since_start();
    ++watch->start_count;
    ++watch->start_count_2;
}

// Stops only a positive start; zero, negative and NaN starts are left alone.
extern "C" void bb_frame_timing_stopwatch_stop(Stopwatch *watch)
{
    if (!(watch->start_seconds > 0.0f)) return;
    const float elapsed = engine::subtract(seconds_since_start(), watch->start_seconds);
    watch->start_seconds = 0.0f;
    watch->total_seconds = engine::add(elapsed, watch->total_seconds);
    watch->total_seconds_2 = engine::add(elapsed, watch->total_seconds_2);
}

// The frame count goes up with a 32-bit wrap, and the averages are taken once the count before
// it reached 29 (a count already past 30, or a wrapped one, averages at once). The start counts
// are taken as signed: converted to float as signed, and the peak compared signed. The peak
// seconds keep the old peak unless this frame's seconds are above it (a NaN on either side keeps
// the old one).
extern "C" void bb_frame_timing_stopwatch_frame_end_014486d0(Stopwatch *watch)
{
    const int32_t previous = watch->frame_count;
    const int32_t frames = static_cast<int32_t>(static_cast<uint32_t>(previous) + 1);
    watch->frame_count = frames;
    if (previous >= frames_per_average - 1) {
        const float frames_float = static_cast<float>(frames);
        watch->average_seconds = engine::divide(watch->total_seconds, frames_float);
        watch->total_seconds = 0.0f;
        watch->average_starts =
            engine::divide(static_cast<float>(static_cast<int32_t>(watch->start_count)), frames_float);
        watch->start_count = 0;
        watch->frame_count = 0;
    }
    const int32_t frame_starts = static_cast<int32_t>(watch->start_count_2);
    if (frame_starts >= watch->peak_start_count_2) watch->peak_start_count_2 = frame_starts;
    watch->start_count_2 = 0;
    watch->peak_total_seconds_2 = engine::max_of(watch->total_seconds_2, watch->peak_total_seconds_2);
    watch->total_seconds_2 = 0.0f;
}
