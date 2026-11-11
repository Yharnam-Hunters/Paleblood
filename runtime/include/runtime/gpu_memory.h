/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GPU_MEMORY_H
#define RUNTIME_GPU_MEMORY_H

/* Direct memory as the renderer's GPU reaches it (runtime/syslib/kernel_memory.c, docs/RENDERER.md).

   A GPU-visible direct map that the title lets the system place (no address hint) goes at
   rt_gpu_memory_base() + its offset in the pool. So every such guest address is a fixed distance
   from the same byte of the pool, and the renderer gives the GPU the whole pool as one buffer (a
   second host mapping of the pool's file): a shader's guest address A is at
   A - rt_gpu_memory_base() + the buffer's GPU address. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The guest address of pool offset 0 for GPU-visible maps. Linux uses the fixed runtime
   window; Darwin selects an available aligned span below 2^40 on first use. This selects
   an address policy, not a permanent reservation: each actual map remains non-replacing. */
uint64_t rt_gpu_memory_base(void);

/* The pool's host memory file and its size, for the renderer's own mapping; returns -1 before the
   title has allocated any direct memory (the file does not exist yet). */
int rt_gpu_memory_pool(int *fd, uint64_t *size);

#ifdef __cplusplus
}
#endif

#endif
