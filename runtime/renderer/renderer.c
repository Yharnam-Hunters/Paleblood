/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer behind the runtime's GPU calls (runtime/renderer.h), in builds with Vulkan. */
#include "runtime/renderer.h"

#include "runtime/gpu_memory.h"
#include "runtime/process.h"
#include "runtime/render_calls.h"
#include "runtime/render_executor.h"

#include <pthread.h>

static pthread_mutex_t renderer_lock = PTHREAD_MUTEX_INITIALIZER;
static int enabled;
static rt_executor *executor;

void rt_renderer_configure(int on) { enabled = on; }

/* The executor, opened once the title has GPU-visible memory; NULL before that or when off. */
static rt_executor *executor_for(const char *function)
{
    if (!enabled || executor) return executor;
    rt_executor_memory memory = {.guest_base = rt_gpu_memory_base()};
    if (rt_gpu_memory_pool(&memory.fd, &memory.size)) return NULL; /* no direct memory yet: nothing to draw from */
    char error[256];
    if (rt_executor_open(&memory, &executor, error, sizeof error)) rt_fatal(function, error);
    return executor;
}

void rt_renderer_submit(const char *function, const void *buffer, uint32_t bytes)
{
    /* Opening the executor gives the GPU the whole pool, which takes seconds, so it waits for the
       first buffer that has drawing calls to run. */
    size_t calls = 0;
    rt_render_submitted((uint64_t)(uintptr_t)buffer, bytes, &calls);
    if (!enabled || !calls) return;
    pthread_mutex_lock(&renderer_lock);
    rt_executor *const x = executor_for(function);
    char error[256];
    if (x && rt_executor_run(x, (uint64_t)(uintptr_t)buffer, bytes, error, sizeof error)) rt_fatal(function, error);
    pthread_mutex_unlock(&renderer_lock);
}

void rt_renderer_skip(const char *function, const void *buffer, uint32_t bytes)
{
    (void)function;
    size_t calls = 0;
    rt_render_submitted((uint64_t)(uintptr_t)buffer, bytes, &calls); /* taken, and not run */
}

void rt_renderer_bind_shader(const char *function, rt_renderer_shader kind, const void *at, const uint32_t *words)
{
    if (!enabled) return; /* nothing would take the call: boots without graphics record nothing */
    (void)function;
    const uint64_t address = (uint64_t)(uintptr_t)at;
    static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS]; /* code address 0: a draw with it stops */
    if (kind == RT_RENDERER_VERTEX_SHADER) rt_render_record_vertex_shader_at(address, words);
    else if (kind == RT_RENDERER_COMPUTE_SHADER) rt_render_record_compute_shader_at(address, words);
    else rt_render_record_pixel_shader_at(address, words ? words : no_pixel_shader);
}

int rt_renderer_frame(uint64_t address, uint32_t *frame, uint32_t width, uint32_t height)
{
    pthread_mutex_lock(&renderer_lock);
    char error[256];
    const int drawn = executor && rt_executor_read_target(executor, address, frame, width, height, error, sizeof error) == 0;
    pthread_mutex_unlock(&renderer_lock);
    return drawn;
}
