/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RENDERER_H
#define RUNTIME_RENDERER_H

/* The renderer as the runtime's GPU calls use it (docs/RENDERER.md, phase 3). When the display is
   on (pbboot --display headless|window, in a build with Vulkan), each submitted command buffer's
   recorded drawing calls run on the GPU (runtime/render_executor.h), and a flip shows what was drawn
   into the flipped buffer. Otherwise these calls do nothing, and boots are unchanged.

   The executor opens the first time a submitted buffer has recorded drawing calls, once the title
   has direct memory (the GPU's view of guest memory, runtime/gpu_memory.h): giving the GPU the
   whole pool takes seconds, so a boot whose buffers have no recorded calls never pays for it. A call it cannot run stops by the submitting
   function's name, with the reason. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turns the renderer on or off, before the guest runs. */
void rt_renderer_configure(int enabled);

/* A command buffer the title submitted: its address and size in bytes. */
void rt_renderer_submit(const char *function, const void *buffer, uint32_t bytes);

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
   frame. Returns 1 when something was drawn there (frame is filled), 0 otherwise (frame unchanged). */
int rt_renderer_frame(uint64_t address, uint32_t *frame, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif
