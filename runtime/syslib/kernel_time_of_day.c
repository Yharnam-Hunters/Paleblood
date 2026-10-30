/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. The time of day (libkernel, libScePosix).

   Facts used:
   - gettimeofday(struct timeval *, struct timezone *) is POSIX; the PS4's system is FreeBSD, whose
     amd64 struct timeval is two 64-bit fields, seconds and microseconds, and whose call returns 0
     and, given no timeval, fills nothing (FreeBSD sys_gettimeofday). The executable's own call
     passes a timeval on its stack and no timezone, and reads both fields as 64-bit values.
   - int32_t sceKernelGettimeofday(OrbisKernelTimeval *) takes the same struct timeval (the OpenOrbis
     homebrew SDK header declares it so, as a corroboration of the shape only); like the other SCE
     names it returns 0 on success.

   The time of day is the host's wall clock: the guest asks the system it runs on, which is the
   host. A timezone argument (obsolete in POSIX; what the PS4 fills is not established) stops by
   name, as does a missing timeval for the SCE name, whose answer is not established. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <time.h>

typedef struct {
    int64_t seconds;
    int64_t microseconds;
} guest_timeval;

enum { NANOSECONDS_PER_MICROSECOND = 1000 };

static void now(guest_timeval *tv)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tv->seconds = ts.tv_sec;
    tv->microseconds = ts.tv_nsec / NANOSECONDS_PER_MICROSECOND;
}

static int time_of_day(guest_timeval *tv, const void *zone, const char *caller)
{
    if (zone) rt_fatal(caller, "a timezone argument: what the system fills in is not established");
    if (tv) now(tv);
    return 0;
}

static int posix_gettimeofday(guest_timeval *tv, const void *zone)
{
    return time_of_day(tv, zone, "gettimeofday");
}

static int32_t sce_gettimeofday(guest_timeval *tv)
{
    if (!tv) rt_fatal("sceKernelGettimeofday", "no timeval: the error value is not established");
    return time_of_day(tv, NULL, "sceKernelGettimeofday");
}

RT_SYSLIB("libScePosix", gettimeofday, posix_gettimeofday);
RT_SYSLIB("libkernel", gettimeofday, posix_gettimeofday);
RT_SYSLIB("libkernel", sceKernelGettimeofday, sce_gettimeofday);
