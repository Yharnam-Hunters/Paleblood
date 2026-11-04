// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the game's monotonic millisecond clock.
//
// frame_timing_get_monotonic_ms (0x0111a7f0) reads CLOCK_MONOTONIC and writes the milliseconds
// since its first call to *out. The seconds of that first call are kept in a shared clock state
// object, which the microsecond variant at 0x0111a760 uses too. All arithmetic is 32-bit and
// wraps, as in the original.
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
    int32_t base_seconds;   // the seconds of the first call; 0 until then
};
static_assert(offsetof(ClockState, base_seconds) == 0x10);

// The executable's call stub for sceKernelClockGettime.
RT_ORIGINAL(0x02fbfe58, kernel_clock_gettime, int(int clock_id, Timespec *ts));
RT_GLOBAL(0x056d6ae8, clock_state_instance, ClockState *);

constexpr int clock_monotonic = 4;
// Returned unchanged when the clock call fails (*out is not written).
constexpr int clock_error = 0x21;
constexpr uint32_t ms_per_second = 1000, ns_per_ms = 1000000;

}  // namespace

extern "C" int bb_frame_timing_get_monotonic_ms(uint32_t *out)
{
    Timespec ts;
    if (kernel_clock_gettime(clock_monotonic, &ts) != 0) return clock_error;
    ClockState *state = clock_state_instance.get();
    const uint32_t seconds = static_cast<uint32_t>(ts.seconds);
    if (state->base_seconds == 0) state->base_seconds = static_cast<int32_t>(seconds);
    const uint32_t elapsed_ms = (seconds - static_cast<uint32_t>(state->base_seconds)) * ms_per_second;
    *out = elapsed_ms + static_cast<uint32_t>(ts.nanoseconds) / ns_per_ms;
    return 0;
}
