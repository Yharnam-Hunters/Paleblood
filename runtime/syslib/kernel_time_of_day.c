/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. The time of day, clock_gettime and the process
   time (libkernel, libScePosix).

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
   FreeBSD answers another clock with EINVAL and a timespec it cannot write with EFAULT.

   sceKernelClockGettime(id, tp) is the same call under its SCE name, which returns 0x80020000 plus
   the error number; the executable's call (0x0111a815) asks for clock 4 and treats anything but 0
   as failure. The POSIX name reports errors through errno, which the runtime does not model, so
   its errors stop by name. */
enum { BSD_CLOCK_REALTIME = 0, BSD_CLOCK_MONOTONIC = 4, BSD_CLOCK_SECOND = 13 };
enum { BSD_EFAULT = 14, BSD_EINVAL = 22 };

typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} guest_timespec;

/* 0, or the FreeBSD error number. */
static int read_clock(int32_t id, guest_timespec *tp)
{
    struct timespec ts;
    switch (id) {
    case BSD_CLOCK_REALTIME:
    case BSD_CLOCK_SECOND: clock_gettime(CLOCK_REALTIME, &ts); break;
    case BSD_CLOCK_MONOTONIC: clock_gettime(CLOCK_MONOTONIC, &ts); break;
    default: return BSD_EINVAL;
    }
    if (!tp) return BSD_EFAULT;
    tp->seconds = ts.tv_sec;
    tp->nanoseconds = id == BSD_CLOCK_SECOND ? 0 : ts.tv_nsec;
    return 0;
}

static int posix_clock_gettime(int32_t id, guest_timespec *tp)
{
    if (read_clock(id, tp)) rt_fatal("clock_gettime", "an unknown clock or no timespec: errno is not modelled");
    return 0;
}

static int32_t sce_clock_gettime(int32_t id, guest_timespec *tp)
{
    const int error = read_clock(id, tp);
    return error ? (int32_t)(0x80020000u + (unsigned)error) : 0;
}

/* uint64_t sceKernelGetProcessTime(void), as the OpenOrbis homebrew SDK's libkernel.h declares it:
   the time since the process started. Its unit is microseconds: the executable turns it into
   seconds by dividing by 1,000,000 (0x00e4fb6e, the compiler's multiply by 0x431bde83 and shift by
   50; 0x0252607e, a division by the double 1000000.0), and seeds a random generator with it
   (0x00835dba). The runtime measures from its own start on the host's monotonic clock. */
static struct timespec process_start;

__attribute__((constructor)) static void note_process_start(void)
{
    clock_gettime(CLOCK_MONOTONIC, &process_start);
}

static uint64_t get_process_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const int64_t elapsed_ns = (int64_t)(now.tv_sec - process_start.tv_sec) * 1000000000 + (now.tv_nsec - process_start.tv_nsec);
    return (uint64_t)(elapsed_ns / NANOSECONDS_PER_MICROSECOND);
}

RT_SYSLIB("libScePosix", gettimeofday, posix_gettimeofday);
RT_SYSLIB("libkernel", gettimeofday, posix_gettimeofday);
RT_SYSLIB("libkernel", sceKernelGettimeofday, sce_gettimeofday);
RT_SYSLIB("libkernel", clock_gettime, posix_clock_gettime);
RT_SYSLIB("libkernel", sceKernelClockGettime, sce_clock_gettime);
RT_SYSLIB("libkernel", sceKernelGetProcessTime, get_process_time);
