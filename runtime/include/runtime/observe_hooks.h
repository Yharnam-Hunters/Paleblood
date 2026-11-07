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

#include "runtime/host.h"
#include "runtime/hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The integer argument registers of the observed call, in order: rdi, rsi, rdx, rcx, r8, r9. */
enum { RT_OBSERVED_ARGUMENTS = 6 };
typedef void (*rt_observer_fn)(const uint64_t args[RT_OBSERVED_ARGUMENTS]);

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
