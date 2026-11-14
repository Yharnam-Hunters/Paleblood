/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GAME_GUEST_POINTER_H
#define RUNTIME_GAME_GUEST_POINTER_H

/* The game library borrows the runtime's already-qualified Darwin guest-pointer slot.
   Keep the implementation here: replacements include its behavior in their source hashes.
   This bridge owns no key or TCB, and never changes the host's GS base. */
#if defined(__APPLE__) && defined(__MACH__)
#if !defined(__x86_64__)
#error "The game guest-pointer bridge requires a whole-process Darwin x86-64 build"
#endif
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*rt_game_guest_offset_fn)(void);
/* Written before hooks are installed; immutable while any replacement can run. */
extern rt_game_guest_offset_fn rt_game_guest_offset_provider;

static inline int rt_game_guest_pointer_initialize(void)
{
    dlerror();
    rt_game_guest_offset_fn provider = (rt_game_guest_offset_fn)dlsym(RTLD_DEFAULT, "rt_guest_pointer_offset");
    const char *error = dlerror();
    if (error || !provider) {
        fputs("bbgame: guest thread pointer: canonical Darwin runtime provider is unavailable\n", stderr);
        return -1;
    }
    rt_game_guest_offset_provider = provider;
    return 0;
}

/* The canonical provider rejects unqualified callers. Look up the current thread's slot on
   each call, including a replacement's reread after an original initializer changes TLS. */
static inline unsigned char *rt_game_guest_thread_pointer(void)
{
    if (!rt_game_guest_offset_provider) {
        fputs("bbgame: guest thread pointer: bridge was not initialized\n", stderr);
        abort();
    }
    const uintptr_t offset = rt_game_guest_offset_provider();
    unsigned char *pointer;
    __asm__ volatile("movq %%gs:(%1), %0" : "=r"(pointer) : "r"(offset) : "memory");
    return pointer;
}

#ifdef __cplusplus
}
#endif
#endif
#endif
