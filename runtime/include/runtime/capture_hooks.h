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
   else the replacement itself. Returns how many hooks can be recorded. */
int rt_capture_hooks_prepare(unsigned char *image, uint64_t image_size, const rt_hook_entry *table, size_t count,
                             const void **targets);

#ifdef __cplusplus
}
#endif

#endif
