// SPDX-License-Identifier: GPL-2.0-or-later
// The monotonic microsecond clock shares its first-call seconds with the millisecond clock.
// The output and elapsed-seconds arithmetic wrap at 32 bits, including the truncated
// nanosecond input. A failed clock read leaves the output and shared clock state untouched.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct Timespec {
    int64_t seconds;
    int64_t nanoseconds;
};

struct ClockState {
    uint8_t unknown_0x00[0x10];
    int32_t base_seconds;
};
static_assert(offsetof(ClockState, base_seconds) == 0x10);

RT_ORIGINAL(0x02fbfe58, kernel_clock_gettime, int(int clock_id, Timespec *time));
RT_GLOBAL(0x056d6ae8, clock_state_instance, ClockState *);

constexpr int clock_monotonic = 4;
constexpr int clock_error = 0x21;
constexpr uint32_t us_per_second = 1000000;
constexpr uint32_t ns_per_us = 1000;

}  // namespace

extern "C" int bb_frame_timing_get_monotonic_us(uint32_t *out)
{
    Timespec time;
    if (kernel_clock_gettime(clock_monotonic, &time) != 0) return clock_error;
    ClockState *state = clock_state_instance.get();
    const uint32_t seconds = static_cast<uint32_t>(time.seconds);
    if (state->base_seconds == 0) state->base_seconds = static_cast<int32_t>(seconds);
    const uint32_t elapsed_us = (seconds - static_cast<uint32_t>(state->base_seconds)) * us_per_second;
    *out = elapsed_us + static_cast<uint32_t>(time.nanoseconds) / ns_per_us;
    return 0;
}
