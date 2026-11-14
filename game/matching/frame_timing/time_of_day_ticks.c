// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the tick clock read from the time of day. The original
// checks the stack canary (the timeval's address is taken): match with ps4-o2-sspall.

#include <stddef.h>
#include <stdint.h>

#define MICROSECONDS_PER_SECOND 1000000

struct timeval {
    long tv_sec;
    long tv_usec;
};

typedef struct {
    uint8_t unknown_0x000[0x1d8];
    unsigned long microseconds_per_tick;
} TickClockOwner;

int gettimeofday(struct timeval *time, void *zone);

// The time of day in microseconds, divided into the owner's ticks.
unsigned long frame_timing_time_of_day_ticks(const TickClockOwner *owner)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (unsigned long)(now.tv_sec * MICROSECONDS_PER_SECOND + now.tv_usec) /
           owner->microseconds_per_tick;
}
