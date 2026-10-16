/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory (libkernel, libScePosix, libSceSysmodule,
   libSceLibcInternal). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <string.h>

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
