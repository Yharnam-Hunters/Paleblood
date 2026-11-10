/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RENDERER_H
#define RUNTIME_RENDERER_H

/* The renderer as the runtime's GPU calls use it (docs/RENDERER.md, phase 3). When the display is
   on (pbboot --display headless|window, in a build with Vulkan), each submitted command buffer's
   recorded drawing calls run on the GPU (runtime/render_executor.h), and a flip shows what was drawn
   into the flipped buffer. Otherwise these calls do nothing, and boots are unchanged.

   Submissions run on the renderer's own thread, in order, while the title goes on: the GPU's
   completion writes, waits and flips (rt_gpu_step) come after the calls before them, never earlier.
   The executor opens the first time a submitted buffer has recorded drawing calls, once the title
   has direct memory (the GPU's view of guest memory, runtime/gpu_memory.h): giving the GPU the
   whole pool takes seconds, so a boot whose buffers have no recorded calls never pays for it. A call it cannot run stops by the submitting
   function's name, with the reason. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turns the renderer on or off, before the guest runs. */
void rt_renderer_configure(int enabled);

/* What the GPU does between a command buffer's drawing calls, in order: a "work done" write into
   guest memory, a wait on guest memory, a flip. The submitting library fills one in (perform, the
   byte offset of its packet in the buffer, and its words); the renderer performs it once the calls
   recorded before that offset have run, and runs the calls after it only once it is done. So the
   guest sees a label only once the work before it is done, and the work after a wait reads memory
   only once the wait holds. RT_GPU_STEP_AFTER_ALL is after every call (a flip). */
enum { RT_GPU_STEP_AFTER_ALL = UINT32_MAX };
typedef struct rt_gpu_step {
    void (*perform)(const struct rt_gpu_step *step);
    const char *function; /* the submitting call, for stops by name */
    uint32_t offset;
    uint64_t words[6];
} rt_gpu_step;

/* A command buffer the title submitted: its address and size in bytes, and the steps the GPU takes
   among its calls (step_count of them, copied, in offset order). With the display on (pbboot --display headless|window,
   in a build with Vulkan), submissions run in submission order on the renderer's own thread, as the
   PS4's GPU runs them beside the CPU: the call returns at once, having taken a copy of the buffer's
   recorded calls (runtime/render_calls.h) and of the steps. Otherwise the steps are performed at once
   and nothing is drawn. */
void rt_renderer_submit(const char *function, const void *buffer, uint32_t bytes, const rt_gpu_step *steps, size_t step_count);

/* Waits until every submission so far has run and its steps have been performed (at once when the
   display is off, and on the renderer's own thread, which is always caught up with itself). */
void rt_renderer_wait_idle(void);

/* A command buffer the title handed to a compute queue (sceGnmDingDong follows the ring's indirect
   buffers): its recorded calls are taken and dropped, not run, since compute queues do not run on the
   renderer yet. Taking them keeps them out of a later buffer that reuses the same memory: a shader
   bound in a compute queue's buffer is placed by address (rt_renderer_bind_shader), and a graphics
   buffer submitted later over that address would otherwise pick it up. */
void rt_renderer_skip(const char *function, const void *buffer, uint32_t bytes);

/* A vertex, pixel or compute shader the title bound (sceGnmSetVsShader, sceGnmSetPsShader and their
   Update forms, sceGnmSetCsShader): its register block, 7 words for a vertex or compute shader and 12
   for a pixel shader
   (runtime/render_calls.h), written into the command buffer at guest address at. The renderer
   takes it into the drawing calls of the buffer submitted with that address. No pixel shader (words
   NULL, a depth-only pass) is recorded as a pixel shader at code address 0, and a draw with it stops
   in the executor. */
typedef enum { RT_RENDERER_VERTEX_SHADER, RT_RENDERER_PIXEL_SHADER, RT_RENDERER_COMPUTE_SHADER } rt_renderer_shader;
void rt_renderer_bind_shader(const char *function, rt_renderer_shader kind, const void *at, const uint32_t *words);

/* What has been drawn into the frame buffer at a guest address, as width x height BGRA pixels into
   frame, once the submissions so far have run (rt_renderer_wait_idle). Returns 1 when something was
   drawn there (frame is filled), 0 otherwise (frame unchanged). */
int rt_renderer_frame(uint64_t address, uint32_t *frame, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif
