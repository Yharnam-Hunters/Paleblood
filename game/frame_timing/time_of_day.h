// SPDX-License-Identifier: GPL-2.0-or-later
// The time-of-day and sleep imports behind the engine's small clock helpers (stopwatches, job
// waits, random seeds), and the microsecond count they all derive from a timeval.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace frame_timing::time_of_day {

struct Timeval {
    int64_t seconds;
    int64_t microseconds;
};
static_assert(sizeof(Timeval) == 16);

RT_ORIGINAL(0x02fbe728, c_gettimeofday, int(Timeval *, void *timezone));
RT_ORIGINAL(0x02fbe8d8, posix_usleep, int(uint32_t microseconds));
RT_ORIGINAL(0x02fbfe68, kernel_usleep, int(uint32_t microseconds));

constexpr uint64_t microseconds_per_second = 1000000;
constexpr uint32_t microseconds_per_millisecond = 1000;

// Seconds times a million plus microseconds, in 64 bits: a time far outside the range of a real
// clock wraps instead of saturating, as the original's 64-bit multiply and add do.
inline uint64_t to_microseconds(const Timeval &time)
{
    return static_cast<uint64_t>(time.seconds) * microseconds_per_second +
           static_cast<uint64_t>(time.microseconds);
}

// The current time of day in microseconds; the call's result is ignored, as every caller does.
inline uint64_t now_microseconds()
{
    Timeval time;
    c_gettimeofday(&time, nullptr);
    return to_microseconds(time);
}

}  // namespace frame_timing::time_of_day
