/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory (libkernel, libScePosix, libSceSysmodule,
   libSceLibcInternal). */
#include "runtime/guest_errno.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The process parameter block: the executable's PT_SCE_PROCPARAM segment as mapped. */
static void *kernel_get_proc_param(void)
{
    if (!rt_process.proc_param) rt_fatal("sceKernelGetProcParam", "the executable has no process parameter segment");
    return rt_process.proc_param;
}
RT_SYSLIB("libkernel", sceKernelGetProcParam, kernel_get_proc_param);

/* The C library registers its heap functions (a table of function pointers, its own or the
   application's replacements) so that system code allocates from the application's heap. Called
   once while the C library starts; the result is not used by the caller. */
static void kernel_rtld_set_application_heap_api(void **heap_api)
{
    if (!heap_api) rt_fatal("_sceKernelRtldSetApplicationHeapAPI", "no table");
    rt_process.heap_api = heap_api;
}
RT_SYSLIB("libkernel", _sceKernelRtldSetApplicationHeapAPI, kernel_rtld_set_application_heap_api);

/* __error(void): the address of the calling thread's errno, FreeBSD's int *__error(void) behind
   its errno macro. Imported by eboot, libc and libSceFios2. Each guest thread runs on its own host
   thread, so a thread-local int is that thread's errno. It starts at 0; the runtime's POSIX-style
   calls set it to a FreeBSD error number when they fail. */
static _Thread_local int guest_errno;
int *rt_guest_errno(void) { return &guest_errno; }
RT_SYSLIB("libkernel", __error, rt_guest_errno);

/* getpagesize, imported by eboot from libScePosix and by libc.elf from libkernel
   (symbols/imports.csv, two rows naming one call): the loader binds an import by library and NID
   (runtime/boot.c), so each library needs its own entry, both pointing here.

   It answers 16384 (16 KiB), the maintainer's ruling of 2026-11-10. The PS4's own number is not
   measured (docs/runtime-syscalls.md, "getpagesize: observed, inferred, unknown"), but every source
   we can cite points the same way: shadPS4's kernel answers 16 KiB, as do a family of native
   ports and the Astrr00 port (facts only); all sixteen PT_LOAD segments of the target's
   images are aligned to 0x4000; and the runtime's own memory calls work in 16 KiB pages. The
   guest only needs a power of two (its callers reject anything else) and caches it. */
enum { GUEST_PAGE_SIZE = 0x4000 };

static int32_t kernel_getpagesize(void) { return GUEST_PAGE_SIZE; }
RT_SYSLIB("libScePosix", getpagesize, kernel_getpagesize);
RT_SYSLIB("libkernel", getpagesize, kernel_getpagesize);

/* memcpy and memset, imported by libSceFios2.elf from libSceLibcInternal (symbols/imports.csv). ISO C
   (7.24.2.1 and 7.24.6.1) fixes their whole result for valid arguments: memcpy copies n bytes from
   the source to the destination, memset writes c converted to unsigned char into the first n bytes,
   and both return the destination. No target-specific value is involved, so the host's own functions
   carry out the same contract. What the standard leaves undefined (overlapping memcpy regions, null
   pointers) is not modelled: no PS4-specific behaviour for those cases is established. */
static void *libc_memcpy(void *dest, const void *src, size_t n)
{
    return memcpy(dest, src, n);
}
RT_SYSLIB("libSceLibcInternal", memcpy, libc_memcpy);

static void *libc_memset(void *dest, int c, size_t n)
{
    return memset(dest, c, n);
}
RT_SYSLIB("libSceLibcInternal", memset, libc_memset);

/* Sleeps the calling thread for at least the given time. A guest thread runs on a host thread, so
   the host thread sleeps. A host signal that interrupts the sleep is not a guest event (the runtime
   delivers no signals to the guest), so the sleep resumes with the time left. */
static void sleep_for(const char *caller, struct timespec left)
{
    while (nanosleep(&left, &left) != 0)
        if (errno != EINTR) rt_fatal(caller, "the host could not sleep");
}

static void sleep_microseconds(const char *caller, uint32_t microseconds)
{
    sleep_for(caller, (struct timespec){.tv_sec = microseconds / 1000000,
                                        .tv_nsec = (long)(microseconds % 1000000) * 1000});
}

/* sceKernelUsleep, imported by eboot and libSceFios2.elf from libkernel. The guest's own verified
   call sites take it as int(uint32_t microseconds) and expect 0 back; docs/runtime-syscalls.md
   names them. */
static int kernel_usleep(uint32_t microseconds)
{
    sleep_microseconds("sceKernelUsleep", microseconds);
    return 0;
}
RT_SYSLIB("libkernel", sceKernelUsleep, kernel_usleep);

/* usleep, imported by eboot from libScePosix: suspends the thread for the given microseconds and
   returns 0 (FreeBSD usleep(3); useconds_t is 32-bit unsigned there). One second or more is the
   open case: SUSv2 lets usleep fail with EINVAL from 1000000 on, FreeBSD sleeps instead, and no
   PS4-specific source says which answer the guest gets, so that range stops by name. */
static int posix_usleep(uint32_t microseconds)
{
    if (microseconds >= 1000000)
        rt_fatal("usleep", "the PS4 answer for one second or more (sleep, or EINVAL) is not established");
    sleep_microseconds("usleep", microseconds);
    return 0;
}
RT_SYSLIB("libScePosix", usleep, posix_usleep);

/* sleep, imported by eboot from libScePosix: suspends the thread for the given seconds and returns
   the unslept seconds, so 0 once the time has elapsed (POSIX sleep; FreeBSD sleep(3) has the same
   unsigned int(unsigned int) shape). Only a signal returns early, and the guest gets none. Above
   INT_MAX FreeBSD's libc sleeps INT_MAX seconds and returns the rest without a signal, which POSIX
   does not provide for, and no PS4-specific source says which answer the guest gets: that range
   stops by name. */
static unsigned int posix_sleep(unsigned int seconds)
{
    if (seconds > INT_MAX)
        rt_fatal("sleep",
                 "the PS4 answer above INT_MAX seconds (the full sleep, or FreeBSD's remainder) is not established");
    sleep_for("sleep", (struct timespec){.tv_sec = seconds});
    return 0;
}
RT_SYSLIB("libScePosix", sleep, posix_sleep);

/* FreeBSD amd64 struct timespec: two 64-bit fields, seconds and nanoseconds. */
typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} guest_timespec;

/* nanosleep(const struct timespec *rqtp, struct timespec *rmtp), imported by libc.elf from
   libkernel: POSIX, and FreeBSD nanosleep(2) has the same shape. It suspends the thread for the
   requested time and returns 0. Only a signal returns early, with the time left in rmtp, and the
   guest gets none, so rmtp is never written. The request is copied before sleeping, so rqtp and
   rmtp may name the same object, as POSIX allows. Its errors go through errno, which the runtime
   does not model, so they stop by name: no request (FreeBSD EFAULT) and nanoseconds outside
   [0, 1000000000) (POSIX EINVAL). A negative second count is the open case: POSIX does not define
   it, FreeBSD's kern_time.c returns 0 without sleeping, the host answers EINVAL, and no
   PS4-specific source says which answer the guest gets. */
static int kernel_nanosleep(const guest_timespec *rqtp, guest_timespec *rmtp)
{
    (void)rmtp;
    if (!rqtp) rt_fatal("nanosleep", "no requested time: errno is not modelled");
    if (rqtp->nanoseconds < 0 || rqtp->nanoseconds >= 1000000000)
        rt_fatal("nanosleep", "nanoseconds outside [0, 1000000000): errno is not modelled");
    if (rqtp->seconds < 0)
        rt_fatal("nanosleep", "the PS4 answer for negative seconds (0 at once, or EINVAL) is not established");
    sleep_for("nanosleep", (struct timespec){.tv_sec = rqtp->seconds, .tv_nsec = rqtp->nanoseconds});
    return 0;
}
RT_SYSLIB("libkernel", nanosleep, kernel_nanosleep);

/* __stack_chk_fail, imported by eboot, libSceFios2.elf and libc.elf from libkernel. Code built with
   the stack protector calls it when a function's saved canary no longer matches the guard, that is,
   when the function's stack frame was overwritten. The LSB contract is void(void) and fixes the
   whole guest-visible result: the call does not return and the program ends (FreeBSD's libc
   reports it and aborts). So stopping the process by name is the implementation, not a placeholder
   for an unknown answer. The guard itself, __stack_chk_guard, is a separate data import. */
static void kernel_stack_chk_fail(void)
{
    rt_fatal("__stack_chk_fail", "the guest's stack protector found a stack frame overwritten");
}
RT_SYSLIB("libkernel", __stack_chk_fail, kernel_stack_chk_fail);

/* _exit(int status), imported by libc.elf from libkernel: POSIX, and FreeBSD _exit(2) has the same
   void(int) shape and never returns. It ends the process at once: no atexit handlers run, no stdio
   buffer is flushed, and the parent sees the status's low 8 bits. The host's _exit does exactly
   that. When the boot harness set rt_exit_observer, it first records the exit as the run's outcome
   (runtime/boot.c) and does not return; if it does return, the process ends as usual. */
void (*rt_exit_observer)(int status);

static _Noreturn void kernel_exit(int status)
{
    if (rt_exit_observer) rt_exit_observer(status);
    _exit(status);
}
RT_SYSLIB("libkernel", _exit, kernel_exit);
