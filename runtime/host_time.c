/* SPDX-License-Identifier: GPL-2.0-or-later */
/* POSIX relative sleeps are rechecked against the original monotonic deadline after
   every wake, including EINTR. No wall-clock fallback or accumulated relative drift. */
#include "runtime/host_time.h"
#include <errno.h>
int rt_host_sleep_until_monotonic(const struct timespec *deadline)
{
    if (!deadline || deadline->tv_sec < 0 || deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000)
        return EINVAL;
    for (;;) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) return errno;
        if (now.tv_sec > deadline->tv_sec ||
            (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec)) return 0;
        /* Monotonic readings and the checked future deadline are nonnegative, so
           their difference fits time_t without adding seconds or multiplying. */
        if (now.tv_sec < 0) return EINVAL;
        struct timespec remaining = {deadline->tv_sec - now.tv_sec, deadline->tv_nsec - now.tv_nsec};
        if (remaining.tv_nsec < 0) { remaining.tv_sec--; remaining.tv_nsec += 1000000000; }
        if (nanosleep(&remaining, NULL) && errno != EINTR) return errno;
    }
}
