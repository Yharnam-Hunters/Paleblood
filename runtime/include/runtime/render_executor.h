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

/* The guest memory the GPU reaches: the pool's file, its size, and the guest address of its offset 0.
   collect_cpu_writes reports the pool bytes inside the given ranges (pool offset and length,
   ascending) that the CPU wrote since they were last collected (rt_write_watch_collect,
   runtime/write_watch.h); without it, or when it answers -1, the executor takes all memory as
   changed at each buffer. guard keeps the CPU from `length` bytes at guest address `address` until
   unguard lifts it, and answers 0, or -1 when it cannot (rt_write_watch_guard): a render target drawn
   into is guarded until it is written back and the CPU touches it (rt_executor_resolve). Without
   them every drawn target is written back at each recorded buffer's end. */
typedef struct {
    int fd;
    uint64_t size;
    uint64_t guest_base;
    int (*collect_cpu_writes)(const uint64_t (*ranges)[2], size_t count, void (*written)(void *context, uint64_t pool_offset, uint64_t length),
                              void *context);
    int (*guard)(uint64_t address, uint64_t length);
    void (*unguard)(uint64_t address, uint64_t length);
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

/* The same without waiting: the calls are recorded and handed to the GPU, and *ticket names the
   submission (tickets count up). Their results (guest memory written, labels' preconditions) hold once
   rt_executor_wait_ticket has returned for that ticket or a later one. Returns 0, or -1 with the reason. */
int rt_executor_submit_calls(rt_executor *executor, const rt_render_call *calls, size_t count, uint64_t *ticket, char *error,
                             size_t error_size);
/* The same without handing the work to the GPU yet: the calls are recorded into the open batch, and
   *ticket names the submission that will hold them. Later calls join the same batch, until
   rt_executor_submit (or a wait inside the executor) hands it over. Waiting for *ticket before then
   blocks until it has been submitted and has run. The render targets they draw stay in their images,
   guarded from the CPU (rt_executor_memory's guard): their guest memory receives the draws when later
   GPU work reads that memory (a dispatch, an image over it), when the CPU touches it
   (rt_executor_resolve), or when rt_executor_submit_calls writes every drawn target back. A target
   that cannot be guarded is written back at the buffer's end. Returns 0, or -1 with the reason. */
int rt_executor_record_calls(rt_executor *executor, const rt_render_call *calls, size_t count, uint64_t *ticket, char *error,
                             size_t error_size);
/* Before the CPU touches `length` bytes at guest address `address` (a fault on a guarded page):
   writes back the render targets drawn into over them, waits for that to run, and lifts the guards of
   the targets there whose memory is now current. Returns 0, or -1 with the reason. */
int rt_executor_resolve(rt_executor *executor, uint64_t address, uint64_t length, char *error, size_t error_size);
/* Hands the open batch, if any, to the GPU without waiting. Returns 0, or -1 with the reason. */
int rt_executor_submit(rt_executor *executor, char *error, size_t error_size);
/* Waits until the submission `ticket` (and every one before it) has run; ticket 0 returns at once. Unlike
   the other calls it may run on another thread while the executor records. Run-time stops are reported
   by the next call on the recording thread. Returns 0, or -1 with the reason (the GPU failed). */
int rt_executor_wait_ticket(rt_executor *executor, uint64_t ticket, char *error, size_t error_size);
/* Waits until all submitted work has run and checks the run-time stop words. Returns 0, or -1 with the
   reason. */
int rt_executor_finish(rt_executor *executor, char *error, size_t error_size);
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
