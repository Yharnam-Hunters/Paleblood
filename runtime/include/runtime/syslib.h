/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_SYSLIB_H
#define RUNTIME_SYSLIB_H

/* The PS4 system functions our runtime implements, by library and symbol name. The loader binds an
   import to an entry here when the import's NID is the entry's (runtime/nid.h); every other import
   is bound to a trampoline that stops the boot and reports it by name.

   Implementations live in runtime/syslib/<group>.c, one file per system call group (kernel,
   threads, memory, files, input, audio, video out, GNM, ...), each adding its entries to the
   table with RT_SYSLIB. Clean room: written from public documentation and observed behaviour,
   never from another emulator's code. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *library;   /* "libkernel", "libc", "libScePad", ... */
    const char *symbol;    /* "sceKernelGetProcessTime", "malloc", ... */
    void *fn;              /* called with the System V AMD64 convention, as the PS4 does */
} rt_syslib_entry;

/* All entries, from every group (gathered by the linker; see RT_SYSLIB). */
const rt_syslib_entry *rt_syslib_begin(void);
const rt_syslib_entry *rt_syslib_end(void);

#define RT_SYSLIB_CAT2(a, b) a##b
#define RT_SYSLIB_CAT(a, b) RT_SYSLIB_CAT2(a, b)
/* RT_SYSLIB("libkernel", sceKernelUsleep, my_usleep); at file scope */
#define RT_SYSLIB(library, symbol, fn)                                                                    \
    __attribute__((used, section("rt_syslib"), aligned(sizeof(void *)))) static const rt_syslib_entry     \
        RT_SYSLIB_CAT(rt_syslib_entry_, __LINE__) = {library, #symbol, (void *)(fn)}

#ifdef __cplusplus
}
#endif

#endif
