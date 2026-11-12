// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the millisecond sleep built on POSIX usleep.

#include <stdbool.h>

#define MICROSECONDS_PER_MILLISECOND 1000

int usleep(unsigned microseconds);

// Sleeps for the given milliseconds; always reports success (matches with a bool return).
bool frame_timing_posix_sleep_ms(unsigned milliseconds)
{
    usleep(milliseconds * MICROSECONDS_PER_MILLISECOND);
    return true;
}
