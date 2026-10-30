/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. The time of day and clock_gettime (libkernel,
   libScePosix).

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

/* clock_gettime(clockid_t id, struct timespec *tp): POSIX, with FreeBSD's clock numbers and amd64
   struct timespec (two 64-bit fields, seconds and nanoseconds). The bundled libc's time()
   (libc.elf+0x2f030) asks for clock 13, FreeBSD's CLOCK_SECOND, and returns its seconds; FreeBSD
   answers CLOCK_SECOND with the wall clock's whole seconds and 0 nanoseconds (kern_clock_gettime).
   The runtime answers the clocks whose meaning FreeBSD fixes and the host has: CLOCK_REALTIME (0)
   and CLOCK_SECOND from the host's wall clock, CLOCK_MONOTONIC (4) from the host's monotonic clock.
   Other clocks and a missing timespec stop by name: their errors go through errno, which the runtime
   does not model. */
enum { BSD_CLOCK_REALTIME = 0, BSD_CLOCK_MONOTONIC = 4, BSD_CLOCK_SECOND = 13 };

typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} guest_timespec;

static int clock_get_time(int32_t id, guest_timespec *tp)
{
    static const char fn[] = "clock_gettime";
    if (!tp) rt_fatal(fn, "no timespec: the error goes through errno, which is not modelled");
    struct timespec ts;
    switch (id) {
    case BSD_CLOCK_REALTIME:
    case BSD_CLOCK_SECOND: clock_gettime(CLOCK_REALTIME, &ts); break;
    case BSD_CLOCK_MONOTONIC: clock_gettime(CLOCK_MONOTONIC, &ts); break;
    default: rt_fatal(fn, "a clock other than CLOCK_REALTIME, CLOCK_MONOTONIC or CLOCK_SECOND is not implemented");
    }
    tp->seconds = ts.tv_sec;
    tp->nanoseconds = id == BSD_CLOCK_SECOND ? 0 : ts.tv_nsec;
    return 0;
}

RT_SYSLIB("libScePosix", gettimeofday, posix_gettimeofday);
RT_SYSLIB("libkernel", gettimeofday, posix_gettimeofday);
RT_SYSLIB("libkernel", sceKernelGettimeofday, sce_gettimeofday);
RT_SYSLIB("libkernel", clock_gettime, clock_get_time);
