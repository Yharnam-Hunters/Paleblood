// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for time-of-day readings. The originals check the stack
// canary (the timeval's address is taken): match with ps4-o2-sspall.

#include <stddef.h>

enum {
    MICROSECONDS_PER_SECOND = 1000000,
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

// Four words for seeding: the time, and the time offset by a counter that advances once per
// offset word (by two per call).
typedef struct {
    unsigned seconds;
    unsigned microseconds;
    unsigned counter_seconds;
    unsigned counter_microseconds;
} TimeSeedWords;

// Defined in the same module: the original addresses it directly.
extern unsigned time_seed_counter __attribute__((visibility("hidden")));

int gettimeofday(struct timeval *time, void *zone);

// The time of day in microseconds.
long frame_timing_time_of_day_us(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * MICROSECONDS_PER_SECOND + now.tv_usec;
}

TimeSeedWords frame_timing_time_seed_words(void)
{
    struct timeval now;
    TimeSeedWords words;
    unsigned seconds, microseconds;
    gettimeofday(&now, NULL);
    seconds = now.tv_sec;
    microseconds = now.tv_usec;
    words.seconds = seconds;
    words.microseconds = microseconds;
    words.counter_seconds = seconds + time_seed_counter++;
    words.counter_microseconds = microseconds + time_seed_counter++;
    return words;
}
