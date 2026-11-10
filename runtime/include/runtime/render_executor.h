/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RENDER_EXECUTOR_H
#define RUNTIME_RENDER_EXECUTOR_H

/* The draw executor (docs/RENDERER.md, phase 3): runs a submitted command buffer's recorded calls
   (runtime/render_calls.h) on the selected Vulkan or Metal backend.

   The guest's GPU-visible memory is one host memory file (the direct-memory pool,
   runtime/gpu_memory.h). The executor maps it a second time and gives that mapping to the GPU as
   one buffer, so a guest address A is at A - guest_base + the buffer's GPU address; the
   translated shaders add that distance to every address they form. Shader code is read from the
   backend's CPU mapping of those guest bytes.

   At each draw the executor translates the vertex and pixel shaders from their register blocks
   (cached), and draws into an image that stands for the render target, found by its address.
   rt_executor_read_target gives that image's pixels, for video out to show at a flip.

   Supported profiles differ: docs/RENDERER.md describes Vulkan, docs/macos-metal.md Metal.
   A recorded state or operation outside the selected backend's profile stops with a reason. */

#include "runtime/render_calls.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_executor rt_executor;

/* The guest memory the GPU reaches: the pool's file, its size, and the guest address of its offset 0. */
typedef struct {
    int fd;
    uint64_t size;
    uint64_t guest_base;
} rt_executor_memory;

/* Opens a GPU device and gives it the memory. Returns 0, or -1 with the reason in error (no
   suitable device, the memory cannot be imported). */
int rt_executor_open(const rt_executor_memory *memory, rt_executor **executor, char *error, size_t error_size);

/* Runs the calls recorded for a command buffer (its guest address) within its submitted size, in
   order, and waits for the GPU. The state they set (registers, user data, shaders) carries over to
   the next buffer, as on the console's GPU, whose state no submission resets. Returns 0, or -1 with
   the reason in error. */
int rt_executor_run(rt_executor *executor, uint64_t buffer, uint32_t size, char *error, size_t error_size);
/* The same for calls already taken from their buffer (rt_render_submitted): count of them, in order. */
int rt_executor_run_calls(rt_executor *executor, const rt_render_call *calls, size_t count, char *error, size_t error_size);

/* Forgets the state earlier buffers set, as a GPU fresh from reset has none (tests: independent cases). */
void rt_executor_reset(rt_executor *executor);

/* The pixels of the image standing for the render target at a guest address, width x height
   from its top left, as 8-bit BGRA (the display's frames). Returns 0, or -1 when no draw has used
   that address or the size is past the image. */
int rt_executor_read_target(rt_executor *executor, uint64_t address, uint32_t *bgra, uint32_t width, uint32_t height,
                            char *error, size_t error_size);

void rt_executor_close(rt_executor *executor);

#ifdef __cplusplus
}
#endif

#endif
