/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory (libkernel, libScePosix, libSceSysmodule,
   libSceLibcInternal). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

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

/* getpagesize, imported by eboot from libScePosix and by libc.elf from libkernel
   (symbols/imports.csv, two rows naming one call): the loader binds an import by library and NID
   (runtime/boot.c), so each library needs its own entry, both pointing here.

   The PS4's system page size is not established by any evidence we can cite. The target's ELF
   headers bound it at best, and only under assumptions about the PS4 loader; the guest itself just
   rejects a non-power-of-two and caches whatever it gets, so it never states the number. The full
   record - what is observed, what is inferred and on which premise, and what remains open - is in
   docs/runtime-syscalls.md, section "getpagesize: observed, inferred, unknown".

   So the call fails loudly instead of returning an invented number or the host's page size. It is
   registered with RT_SYSLIB_UNRESOLVED: the boot gets a named diagnostic if it reaches this, and
   coverage leaves the import unchecked, since a diagnostic is not an implementation. */
static void kernel_getpagesize(void)
{
    rt_fatal("getpagesize", "the PS4 system page size is not established by public evidence");
}
RT_SYSLIB_UNRESOLVED("libScePosix", getpagesize, kernel_getpagesize);
RT_SYSLIB_UNRESOLVED("libkernel", getpagesize, kernel_getpagesize);

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

/* Sleeps the calling thread for at least the given number of microseconds. A guest thread runs on
   a host thread, so the host thread sleeps. A host signal that interrupts the sleep is not a guest
   event (the runtime delivers no signals to the guest), so the sleep resumes with the time left. */
static void sleep_microseconds(const char *caller, uint32_t microseconds)
{
    struct timespec left = {.tv_sec = microseconds / 1000000, .tv_nsec = (long)(microseconds % 1000000) * 1000};
    while (nanosleep(&left, &left) != 0)
        if (errno != EINTR) rt_fatal(caller, "the host could not sleep");
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
