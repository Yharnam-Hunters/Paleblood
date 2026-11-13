/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_OBSERVE_HOOKS_H
#define RUNTIME_OBSERVE_HOOKS_H

/* Observe hooks: the renderer learns what a function of the game was asked to do without the
   function being replaced. The hook calls an observer with the call's arguments, then the original
   runs as it would have, from its first instructions relocated next to the image
   (rt_trace_relocate), so everything it writes is the original's own.

   They exist for the GPU library functions that tell the renderer state and have no replacement yet
   (docs/RENDERER.md): a replacement, when one lands, makes the same observer call itself and its
   observe hook is dropped. They are installed only when the host has the renderer's record
   (runtime/render_observer.h) and the instructions can be relocated (Capstone); otherwise nothing is
   hooked and the game runs as before. An observer runs before the original, so it reports the call
   even if the original then finds no room for its packets. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "runtime/host.h"
#include "runtime/hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The integer argument registers of the observed call, in order: rdi, rsi, rdx, rcx, r8, r9. The
   array is the frame rt_observe_entry saved (runtime/observe_entry.S): after them come rax and the
   hook, then xmm0-xmm7, 16 bytes each from word RT_OBSERVED_XMM0, where floating-point arguments
   arrive (rt_observed_float); an observer reading them declares the whole frame. */
enum { RT_OBSERVED_ARGUMENTS = 6, RT_OBSERVED_XMM0 = 8, RT_OBSERVED_XMM_WORDS = 2, RT_OBSERVED_FRAME_WORDS = RT_OBSERVED_XMM0 + 8 * RT_OBSERVED_XMM_WORDS };
typedef void (*rt_observer_fn)(const uint64_t args[RT_OBSERVED_ARGUMENTS]);

/* The call's stack arguments (the seventh integer argument on): rt_observe_entry keeps the caller's rsp and
   lowers it by its 200-byte frame (RT_OBSERVED_ENTRY_FRAME_BYTES, runtime/observe_entry.S), so above the frame
   come the return address and then the arguments the caller pushed, from word RT_OBSERVED_STACK. */
enum { RT_OBSERVED_ENTRY_FRAME_BYTES = 200, RT_OBSERVED_STACK = RT_OBSERVED_ENTRY_FRAME_BYTES / 8 + 1 };

/* The observed call's n-th stack argument (n from 0: the seventh integer argument), as the caller stored
   it; the frame is the whole saved frame, which the caller's stack continues. */
static inline uint64_t rt_observed_stack_argument(const uint64_t *frame, unsigned n) { return frame[RT_OBSERVED_STACK + n]; }

/* The observed call's n-th float argument (in xmm n's low 32 bits). */
static inline float rt_observed_float(const uint64_t frame[RT_OBSERVED_FRAME_WORDS], unsigned n)
{
    float value;
    memcpy(&value, &frame[RT_OBSERVED_XMM0 + RT_OBSERVED_XMM_WORDS * n], sizeof value);
    return value;
}

typedef struct {
    uint32_t address;          /* PS4 virtual address of the observed function */
    uint32_t size;             /* its size in bytes */
    const char *name;
    rt_observer_fn observe;
} rt_observe_hook;

/* Installs the observe hooks (through host->install_hook) for table[0..count), leaving out any
   address the replacements in replaced[0..replaced_count) take. Called while the originals are intact.
   Returns how many were installed. */
int rt_observe_hooks_install(const rt_host *host, const rt_observe_hook *table, size_t count, const rt_hook_entry *replaced,
                             size_t replaced_count);

#ifdef __cplusplus
}
#endif

#endif
