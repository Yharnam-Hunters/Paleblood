/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RT_HOST_LOCK_WAIT_H
#define RT_HOST_LOCK_WAIT_H
#include "runtime/host_time.h"
#include <errno.h>

/* Darwin has no pthread_mutex/rwlock_clock*lock. Retry the native try operation with
   sleeps of up to one millisecond, capped by the original monotonic deadline. There is
   an immediate attempt even for an expired deadline. Scheduling/fairness remain
   the host's; this polling adapter does not promise native timed-lock wake latency. */
static inline int rt_host_lock_wait(int (*attempt)(void *), void *lock, const struct timespec *deadline)
{
    if (!deadline || deadline->tv_sec < 0 || deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000)
        return EINVAL;
    for (;;) {
        const int status = attempt(lock);
        if (status != EBUSY) return status;
        struct timespec wake;
        if (clock_gettime(CLOCK_MONOTONIC, &wake)) return errno;
        if (wake.tv_sec > deadline->tv_sec ||
            (wake.tv_sec == deadline->tv_sec && wake.tv_nsec >= deadline->tv_nsec)) return ETIMEDOUT;
        /* The current time precedes a representable nonnegative deadline. */
        if (wake.tv_sec < deadline->tv_sec || deadline->tv_nsec - wake.tv_nsec > 1000000) {
            if (wake.tv_nsec >= 999000000) { ++wake.tv_sec; wake.tv_nsec -= 999000000; }
            else wake.tv_nsec += 1000000;
            if (wake.tv_sec > deadline->tv_sec ||
                (wake.tv_sec == deadline->tv_sec && wake.tv_nsec > deadline->tv_nsec)) wake = *deadline;
        } else wake = *deadline;
        const int slept = rt_host_sleep_until_monotonic(&wake);
        if (slept) return slept;
    }
}
#endif
