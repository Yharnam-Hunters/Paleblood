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

/* What has been drawn into the frame buffer at a guest address, as width x height BGRA pixels into
   frame. Returns 1 when something was drawn there (frame is filled), 0 otherwise (frame unchanged). */
int rt_renderer_frame(uint64_t address, uint32_t *frame, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif
