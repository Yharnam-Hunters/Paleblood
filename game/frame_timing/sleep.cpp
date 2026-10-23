// SPDX-License-Identifier: GPL-2.0-or-later
// Convert the engine's millisecond delay to the kernel's microsecond delay. The original
// multiplies in 32 bits, so large delays wrap, and always reports success after the kernel call.
#include <cstdint>

#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x02fbfe68, kernel_usleep, int(uint32_t microseconds));

constexpr uint32_t microseconds_per_millisecond = 1000;

}  // namespace

extern "C" int bb_frame_timing_sleep_ms(uint32_t milliseconds)
{
    kernel_usleep(milliseconds * microseconds_per_millisecond);
    return 0;
}
