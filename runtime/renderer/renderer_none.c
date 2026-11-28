/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer in builds without Vulkan (runtime/renderer.h): nothing is drawn, and frames stay as
   video out keeps them. */
#include "runtime/renderer.h"

void rt_renderer_configure(int enabled) { (void)enabled; }

/* The flags and memory of the last submission's steps, for the system library's tests
   (test/syslib/gnm_occlusion.c, gnm_constant_engine.c, gnm_dma_data.c): with the work done at once, nothing else tells
   a deferrable step from another. */
enum { OBSERVED_STEPS = 16 };
static uint32_t observed_flags[OBSERVED_STEPS];
static uint64_t observed_memory[OBSERVED_STEPS][2];
static size_t observed_count;

size_t rt_renderer_none_step_flags(uint32_t *flags, size_t capacity)
{
    const size_t count = observed_count < capacity ? observed_count : capacity;
    for (size_t i = 0; i < count; i++) flags[i] = observed_flags[i];
    return observed_count;
}

size_t rt_renderer_none_step_memory(uint64_t (*memory)[2], size_t capacity)
{
    const size_t count = observed_count < capacity ? observed_count : capacity;
    for (size_t i = 0; i < count; i++) memory[i][0] = observed_memory[i][0], memory[i][1] = observed_memory[i][1];
    return observed_count;
}

void rt_renderer_submit(const char *function, unsigned queue, const void *buffer, uint32_t bytes, const rt_gpu_step *steps,
                        size_t step_count)
{
    (void)function, (void)queue, (void)buffer, (void)bytes;
    observed_count = step_count;
    for (size_t i = 0; i < step_count && i < OBSERVED_STEPS; i++)
        observed_flags[i] = steps[i].flags, observed_memory[i][0] = steps[i].memory[0], observed_memory[i][1] = steps[i].memory[1];
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
