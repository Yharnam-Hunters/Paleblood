/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. The libkernel calls and data the title's own C
   library (libc.elf, Sony's FreeBSD-derived libc with HP libunwind for C++ exceptions) imports for
   its start-up, standard output, clock and exception machinery.

   - __progname (data): FreeBSD's `const char *__progname`, the program's name; libc.elf reads it
     through a data slot. shadPS4's kernel.cpp and the deadinside28 port both point it at
     "eboot.bin" (facts only), used here.
   - __pthread_cxa_finalize(info): FreeBSD libthr's hook that drops a module's pthread_atfork
     handlers when the module is finalized (libc.elf +0x2f506). The runtime has no fork and records
     no atfork handlers, so there is nothing to drop (PS4Recomp does nothing too).
   - _is_signal_return(ip): libunwind's question whether an instruction address is a signal
     trampoline (libc.elf +0x54530: 0 is an ordinary frame). The runtime delivers no guest signals,
     so there is no trampoline and the answer is always 0 (PS4Recomp answers 0).
   - _write(fd, buffer, size): POSIX write(2) without the cancellation point; libc.elf's stdio uses
     it only for descriptors 1 and 2 (+0x27362; other files go through libSceFios2). It writes to
     the host's standard output or error and answers the byte count; any other descriptor stops by
     name.
   - getrusage(who, usage): POSIX/FreeBSD getrusage(2), which libc.elf's clock() calls with
     RUSAGE_SELF (+0x2ef13) and reads the user and system times from (FreeBSD's struct rusage, 144
     bytes: two timevals, then fourteen longs). The runtime answers the host's figures for the
     process (PS4Recomp does the same). Another `who` stops by name.
   - signal(sig, handler): ISO C signal(); libc.elf's one call sets SIGKILL to SIG_DFL (+0x13027).
     The runtime keeps each signal's disposition and answers the previous one (initially SIG_DFL,
     0), delivering nothing; a signal outside 1 to 128, or a handler other than SIG_DFL for SIGKILL
     or SIGSTOP, answers SIG_ERR (-1) with errno EINVAL (FreeBSD's sigaction rules).
   - sceKernelGettimezone(tz): two ints, the standard time offset in minutes and the daylight
     saving minutes. libc.elf's localtime and mktime compute local time as UTC plus (first + second)
     minutes, so the first counts minutes east of UTC (shadPS4's gettimeofday fills it the same
     way; facts only); the runtime answers the host's time zone (the console's setting is the
     owner's) and 0.
   - sceKernelDebugRaiseException(code, zero) and sceKernelDebugRaiseExceptionOnReleaseMode: the
     libc's fatal checks (abort() is +0x23560 with code 0xa002000b). As in shadPS4: a non-zero second
     argument answers EINVAL (0x80020016); otherwise the program ends, here by name with the code.
   - getpid(): the process id, which libunwind passes to sysctl's process memory map query
     (runtime/syslib/kernel_unwind.c). The runtime answers 1000, the fixed id a family of native ports
     uses (facts only; maintainer's ruling of 2026-11-10): a fixed number keeps runs
     reproducible, and the host's pid is not the guest's.
   - _read(fd, buffer, size): POSIX read(2) without the cancellation point; libc.elf's stdio reads
     only standard input with it (+0x2729a; other files go through libSceFios2). A console title
     has no standard input, so the answer is end of file, 0 bytes (maintainer's ruling); any other
     descriptor stops by name.
   - __stack_chk_guard (data): the stack protector's canary, a 64-bit word that code built with
     -fstack-protector stores in each protected frame and compares on return. Every port we can
     cite gives it a fixed non-zero value rather than a random one (shadPS4 0xdeadbeef54321abc, the
     droogie and Astrr00 hosts their own constants; facts only), so runs are reproducible; the
     runtime uses shadPS4's value (maintainer's ruling). Unbound, the import read a zeroed page. */
#include "runtime/guest_errno.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define SCE_KERNEL_ERROR_EINVAL ((int32_t)0x80020016u)
enum { BSD_EINVAL = 22, BSD_SIGKILL = 9, BSD_SIGSTOP = 17, MAX_SIGNAL = 128, GUEST_STDOUT = 1, GUEST_STDERR = 2 };
enum { RUSAGE_SELF_BSD = 0, RUSAGE_LONGS = 14, MINUTES_PER_DST = 60 };
enum { GUEST_PROCESS_ID = 1000, GUEST_STDIN = 0 };
#define SIG_DFL_GUEST ((uintptr_t)0)
#define SIG_ERR_GUEST ((uintptr_t)-1)

static const char program_name[] = "eboot.bin";
static const char *progname[1] = {program_name}; /* the variable itself: an array, so its name is its address */

static uint64_t stack_chk_guard[1] = {UINT64_C(0xdeadbeef54321abc)}; /* the variable itself, as for __progname */

static void pthread_cxa_finalize(void *info) { (void)info; }

static int32_t libc_getpid(void) { return GUEST_PROCESS_ID; }

static int64_t libc_read(int32_t fd, void *buffer, uint64_t size)
{
    (void)buffer, (void)size;
    if (fd != GUEST_STDIN) rt_fatal("_read", "a descriptor other than standard input: not modelled");
    return 0; /* no standard input: end of file */
}

static int32_t is_signal_return(uintptr_t instruction) { (void)instruction; return 0; }

static int64_t libc_write(int32_t fd, const void *buffer, uint64_t size)
{
    static const char fn[] = "_write";
    if (fd != GUEST_STDOUT && fd != GUEST_STDERR) rt_fatal(fn, "a descriptor other than standard output and error: not modelled");
    const ssize_t n = write(fd == GUEST_STDOUT ? STDOUT_FILENO : STDERR_FILENO, buffer, size);
    if (n < 0) rt_fatal(fn, "a host write error without an established answer");
    return n;
}

typedef struct {
    int64_t seconds, microseconds;
} guest_timeval;

typedef struct {
    guest_timeval user, system;
    int64_t counts[RUSAGE_LONGS];
} guest_rusage;
_Static_assert(sizeof(guest_rusage) == 144, "FreeBSD amd64 struct rusage is 144 bytes");

static int32_t libc_getrusage(int32_t who, guest_rusage *usage)
{
    static const char fn[] = "getrusage";
    if (who != RUSAGE_SELF_BSD || !usage) rt_fatal(fn, "another process scope or no structure: not modelled");
    struct rusage host;
    if (getrusage(RUSAGE_SELF, &host)) rt_fatal(fn, "a host error without an established answer");
    *usage = (guest_rusage){{host.ru_utime.tv_sec, host.ru_utime.tv_usec}, {host.ru_stime.tv_sec, host.ru_stime.tv_usec},
                            {host.ru_maxrss, host.ru_ixrss, host.ru_idrss, host.ru_isrss, host.ru_minflt, host.ru_majflt,
                             host.ru_nswap, host.ru_inblock, host.ru_oublock, host.ru_msgsnd, host.ru_msgrcv, host.ru_nsignals,
                             host.ru_nvcsw, host.ru_nivcsw}};
    return 0;
}

static pthread_mutex_t signals_lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t dispositions[MAX_SIGNAL + 1];

static uintptr_t libc_signal(int32_t sig, uintptr_t handler)
{
    if (sig < 1 || sig > MAX_SIGNAL || ((sig == BSD_SIGKILL || sig == BSD_SIGSTOP) && handler != SIG_DFL_GUEST)) {
        *rt_guest_errno() = BSD_EINVAL;
        return SIG_ERR_GUEST;
    }
    pthread_mutex_lock(&signals_lock);
    const uintptr_t previous = dispositions[sig];
    dispositions[sig] = handler;
    pthread_mutex_unlock(&signals_lock);
    return previous;
}

typedef struct {
    int32_t minutes_east, dst_minutes;
} guest_timezone;

static int32_t get_timezone(guest_timezone *zone)
{
    if (!zone) rt_fatal("sceKernelGettimezone", "no structure: the error value is not established");
    const time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local)) rt_fatal("sceKernelGettimezone", "the host has no local time");
    const int32_t dst = local.tm_isdst > 0 ? MINUTES_PER_DST : 0;
    *zone = (guest_timezone){(int32_t)(local.tm_gmtoff / 60) - dst, dst};
    return 0;
}

static int32_t raise_exception(const char *caller, uint32_t code, int64_t zero)
{
    if (zero != 0) return SCE_KERNEL_ERROR_EINVAL;
    char message[64];
    snprintf(message, sizeof message, "the program raised exception code 0x%08x", (unsigned)code);
    rt_fatal(caller, message);
}

static int32_t debug_raise_exception(uint32_t code, int64_t zero)
{
    return raise_exception("sceKernelDebugRaiseException", code, zero);
}

static int32_t debug_raise_exception_release(uint32_t code, int64_t zero)
{
    return raise_exception("sceKernelDebugRaiseExceptionOnReleaseMode", code, zero);
}

RT_SYSLIB("libkernel", __progname, progname);
RT_SYSLIB("libkernel", __stack_chk_guard, stack_chk_guard);
RT_SYSLIB("libkernel", __pthread_cxa_finalize, pthread_cxa_finalize);
RT_SYSLIB("libkernel", getpid, libc_getpid);
RT_SYSLIB("libkernel", _read, libc_read);
RT_SYSLIB("libkernel", _is_signal_return, is_signal_return);
RT_SYSLIB("libkernel", _write, libc_write);
RT_SYSLIB("libkernel", getrusage, libc_getrusage);
RT_SYSLIB("libkernel", signal, libc_signal);
RT_SYSLIB("libkernel", sceKernelGettimezone, get_timezone);
RT_SYSLIB("libkernel", sceKernelDebugRaiseException, debug_raise_exception);
RT_SYSLIB("libkernel", sceKernelDebugRaiseExceptionOnReleaseMode, debug_raise_exception_release);
