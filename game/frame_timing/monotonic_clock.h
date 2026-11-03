// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace frame_timing::monotonic {

struct Timespec {
    int64_t seconds;
    int64_t nanoseconds;
};
static_assert(sizeof(Timespec) == 16);

struct ClockState {
    uint8_t unknown_0x00[0x10];
    int32_t base_seconds;  // Seconds of the first clock call; zero until then.
};
static_assert(offsetof(ClockState, base_seconds) == 0x10);

RT_ORIGINAL(0x02fbfe58, kernel_clock_gettime, int(int clock_id, Timespec *time));
RT_GLOBAL(0x056d6ae8, clock_state_instance, ClockState *);

constexpr int clock_monotonic = 4;
// Every clock-call failure maps to this value, regardless of the kernel error.
constexpr int clock_error = 0x21;

}  // namespace frame_timing::monotonic
