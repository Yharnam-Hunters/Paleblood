/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer in builds without Vulkan (runtime/renderer.h): nothing is drawn, and frames stay as
   video out keeps them. */
#include "runtime/renderer.h"

void rt_renderer_configure(int enabled) { (void)enabled; }

void rt_renderer_submit(const char *function, const void *buffer, uint32_t bytes, const rt_gpu_step *steps, size_t step_count)
{
    (void)function, (void)buffer, (void)bytes;
    for (size_t i = 0; i < step_count; i++) steps[i].perform(&steps[i]); /* no GPU: the work is done at once */
}

void rt_renderer_wait_idle(void) {}

void rt_renderer_bind_shader(const char *function, rt_renderer_shader kind, const void *at, const uint32_t *words)
{
    (void)function, (void)kind, (void)at, (void)words;
}

int rt_renderer_frame(uint64_t address, uint32_t *frame, uint32_t width, uint32_t height)
{
    (void)address, (void)frame, (void)width, (void)height;
    return 0;
}
