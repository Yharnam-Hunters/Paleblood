/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_SYSLIB_H
#define RUNTIME_SYSLIB_H

/* The PS4 system functions and unresolved diagnostics our runtime registers, by library and symbol
   name. The loader binds an import to an entry here when the import's NID is the entry's
   (runtime/nid.h); every other import is bound to a trampoline that stops the boot and reports it
   by name.

   Functional implementations live in runtime/syslib/<group>.c and use RT_SYSLIB. A diagnostic for
   unresolved behavior may use RT_SYSLIB_UNRESOLVED; it is registered so the call can fail loudly,
   but is not counted as implemented by tools/runtime_issues.py. Clean room: written from public
   documentation and observed behaviour, never from another emulator's code. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RT_SYSLIB_CAPABILITY_UNRESOLVED = 0,
    RT_SYSLIB_CAPABILITY_IMPLEMENTED = 1,
} rt_syslib_capability;

typedef struct {
    const char *library;   /* "libkernel", "libc", "libScePad", ... */
    const char *symbol;    /* "sceKernelGetProcessTime", "malloc", ... */
    void *fn;              /* called with the System V AMD64 convention, as the PS4 does */
    rt_syslib_capability capability;
} rt_syslib_entry;

/* All entries, from every group (gathered by the linker; see RT_SYSLIB and RT_SYSLIB_UNRESOLVED). */
const rt_syslib_entry *rt_syslib_begin(void);
const rt_syslib_entry *rt_syslib_end(void);

#define RT_SYSLIB_CAT2(a, b) a##b
#define RT_SYSLIB_CAT(a, b) RT_SYSLIB_CAT2(a, b)
#if defined(__APPLE__) && defined(__MACH__)
#define RT_SYSLIB_SECTION "__DATA,rt_syslib"
#else
#define RT_SYSLIB_SECTION "rt_syslib"
#endif
#define RT_SYSLIB_REGISTER(entry, library, symbol, fn, capability)                                        \
    __attribute__((used, section(RT_SYSLIB_SECTION), aligned(sizeof(void *)))) static const rt_syslib_entry \
        entry = {library, #symbol, (void *)(fn), capability}
/* RT_SYSLIB("libkernel", sceKernelUsleep, my_usleep); at file scope. Use only when the runtime
   provides the imported behavior. tools/runtime_issues.py counts these as implemented. */
#define RT_SYSLIB(library, symbol, fn) \
    RT_SYSLIB_REGISTER(RT_SYSLIB_CAT(rt_syslib_entry_, __LINE__), library, symbol, fn, \
                       RT_SYSLIB_CAPABILITY_IMPLEMENTED)
/* A registered fail-loud diagnostic for a behavior that remains unresolved. The loader can bind
   this entry and report the specific call when reached, but runtime issue coverage must leave it
   unchecked. */
#define RT_SYSLIB_UNRESOLVED(library, symbol, fn) \
    RT_SYSLIB_REGISTER(RT_SYSLIB_CAT(rt_syslib_unresolved_entry_, __LINE__), library, symbol, fn, \
                       RT_SYSLIB_CAPABILITY_UNRESOLVED)

#ifdef __cplusplus
}
#endif

#endif
