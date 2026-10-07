/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory (libkernel, libScePosix, libSceSysmodule,
   libSceLibcInternal). */
#include "runtime/process.h"
#include "runtime/syslib.h"

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
