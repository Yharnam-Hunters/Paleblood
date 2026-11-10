// SPDX-License-Identifier: GPL-2.0-or-later
// Sleep for a number of milliseconds through the POSIX usleep and report success. The
// millisecond-to-microsecond multiply is 32-bit, so delays above about 71.6 minutes wrap, and the
// result of usleep is ignored: the function always returns true. Called from two places in the
// same caller (0x00719aa0, 0x00719d6e).
#include <cstdint>

#include "time_of_day.h"

extern "C" bool bb_frame_timing_posix_sleep_ms(uint32_t milliseconds)
{
    using namespace frame_timing::time_of_day;
    posix_usleep(milliseconds * microseconds_per_millisecond);
    return true;
}
