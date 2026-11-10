// SPDX-License-Identifier: GPL-2.0-or-later
// A stopwatch embedded in a frame-timing object: start stores the current time in seconds
// (relative to the game's start time) unless it is already running; stop adds the time since
// start to two running totals and clears the start. Both are called from the task frame around
// 0x013d4412..0x013d46c4 and tail-called from 0x01456ec6/0x01456ee6.
#include <cstddef>
#include <cstdint>

#include "../engine/scalar.h"
#include "time_of_day.h"

namespace {

using namespace frame_timing::time_of_day;

struct Stopwatch {
    uint8_t unknown_0x00[0x38];
    float start_seconds;   // 0 while stopped
    float total_seconds;
    uint8_t unknown_0x40[0x4];
    uint32_t start_count;
    uint8_t unknown_0x48[0x4];
    uint32_t start_count_2;   // a second start counter; what resets it apart from 0x44 is unknown
    uint8_t unknown_0x50[0x4];
    float total_seconds_2;    // a second total; what resets it apart from 0x3c is unknown
};
static_assert(offsetof(Stopwatch, start_seconds) == 0x38 && offsetof(Stopwatch, total_seconds) == 0x3c);
static_assert(offsetof(Stopwatch, start_count) == 0x44 && offsetof(Stopwatch, start_count_2) == 0x4c);
static_assert(offsetof(Stopwatch, total_seconds_2) == 0x54);

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
