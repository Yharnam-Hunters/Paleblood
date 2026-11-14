/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_CAPTURE_HOOKS_H
#define RUNTIME_CAPTURE_HOOKS_H

/* Recording hooked calls by tracing their originals (runtime/capture_hooks.c,
   docs/capture-architecture.md). */

#include <stddef.h>
#include <stdint.h>

#include "runtime/hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called before the hooks are installed, while the originals are intact. Fills targets[i] with
   what hook i should jump to: a recording thunk when capture is on and the hook can be recorded,
   else the replacement itself, or NULL (with BB_CAPTURE_ORIGINAL=1): leave this hook out, so the
   original runs. Returns how many hooks can be recorded. */
int rt_capture_hooks_prepare(unsigned char *image, uint64_t image_size, const rt_hook_entry *table, size_t count,
                             const void **targets);

/* Called after rt_capture_hooks_prepare and before the family hooks are installed, while the
   members are intact: copies each member's original code, so a traced call that reaches a member
   runs (and records) the game's own code there instead of leaving the traced code for the shared
   native replacement. Untraced calls still reach the replacement. Returns how many members a
   trace can follow (0 when nothing is recorded or with BB_CAPTURE_ORIGINAL=1, where the members
   are not hooked). */
int rt_capture_families_prepare(unsigned char *image, uint64_t image_size, const rt_family_hook *table, size_t count);

/* Capture is on with BB_CAPTURE_ORIGINAL=1: only originals run (family hooks are left out too). */
int rt_capture_original_mode(void);

/* A writable block of size bytes within 1.75 GB of the image (relocated RIP-relative operands still
   reach their data), or NULL. */
unsigned char *rt_map_near_image(unsigned char *image, uint64_t image_size, size_t size);

#ifdef __cplusplus
}
#endif

#endif
