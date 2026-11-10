/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer behind the runtime's GPU calls (runtime/renderer.h), using the selected executor. */
#include "runtime/renderer.h"

#include "runtime/gpu_memory.h"
#include "runtime/process.h"
#include "runtime/render_calls.h"
#include "runtime/render_executor.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* Submissions waiting for the renderer's thread, oldest first: each a copy of its buffer's recorded
   calls and of the steps that follow them. */
typedef struct batch {
    const char *function;
    rt_render_call *calls;
    size_t call_count;
    rt_gpu_step *steps;
    size_t step_count;
    struct batch *next;
} batch;

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_changed = PTHREAD_COND_INITIALIZER;
static batch *queue_head, *queue_tail;
static int thread_started, running; /* running: a batch taken off the queue and not finished */
static pid_t thread_process;        /* a child after fork has no renderer thread: it starts its own */
static _Thread_local int on_renderer_thread;

/* Runs calls [first, last) of a batch; their targets are written back before it returns. */
static void run_calls(batch *b, size_t first, size_t last)
{
    if (first == last) return;
    pthread_mutex_lock(&renderer_lock);
    rt_executor *const x = executor_for(b->function);
    char error[256];
    if (x && rt_executor_run_calls(x, b->calls + first, last - first, error, sizeof error)) rt_fatal(b->function, error);
    pthread_mutex_unlock(&renderer_lock);
}

/* Each step once the calls before its offset have run, and before the calls after it. */
static void run_batch(batch *b)
{
    size_t done = 0;
    for (size_t i = 0; i < b->step_count; i++) {
        size_t upto = done;
        while (upto < b->call_count && b->calls[upto].offset < b->steps[i].offset) upto++;
        run_calls(b, done, upto);
        done = upto;
        b->steps[i].perform(&b->steps[i]);
    }
    run_calls(b, done, b->call_count);
}

static void *renderer_thread(void *unused)
{
    (void)unused;
    on_renderer_thread = 1;
    for (;;) {
        pthread_mutex_lock(&queue_lock);
        while (!queue_head) pthread_cond_wait(&queue_changed, &queue_lock);
        batch *const b = queue_head;
        queue_head = b->next;
        if (!queue_head) queue_tail = NULL;
        running = 1;
        pthread_mutex_unlock(&queue_lock);
        run_batch(b);
        free(b->calls);
        free(b->steps);
        free(b);
        pthread_mutex_lock(&queue_lock);
        running = 0;
        pthread_cond_broadcast(&queue_changed);
        pthread_mutex_unlock(&queue_lock);
    }
    return NULL;
}

void rt_renderer_wait_idle(void)
{
    if (on_renderer_thread) return; /* its own steps: everything before them has run */
    pthread_mutex_lock(&queue_lock);
    while (queue_head || running) pthread_cond_wait(&queue_changed, &queue_lock);
    pthread_mutex_unlock(&queue_lock);
}

void rt_renderer_submit(const char *function, const void *buffer, uint32_t bytes, const rt_gpu_step *steps, size_t step_count)
{
    size_t call_count = 0;
    rt_render_call *const calls = rt_render_take((uint64_t)(uintptr_t)buffer, bytes, &call_count);
    if (call_count == SIZE_MAX) rt_fatal(function, "no memory for a submission's calls");
    if (!enabled) { /* nothing is drawn: the work is done at once */
        free(calls);
        for (size_t i = 0; i < step_count; i++) steps[i].perform(&steps[i]);
        return;
    }
    /* The executor opens with the first buffer that has drawing calls, on the renderer's thread:
       giving the GPU the whole pool takes seconds. Buffers without calls still queue, so their steps
       come after the work submitted before them. */
    batch *const b = calloc(1, sizeof *b);
    if (!b) rt_fatal(function, "no memory for a submission");
    b->function = function;
    b->calls = calls; /* the batch owns the copy */
    if (step_count && !(b->steps = malloc(step_count * sizeof *b->steps))) rt_fatal(function, "no memory for a submission's steps");
    if (step_count) memcpy(b->steps, steps, step_count * sizeof *b->steps);
    b->call_count = call_count, b->step_count = step_count;
    pthread_mutex_lock(&queue_lock);
    if (thread_started && thread_process != getpid()) thread_started = 0, running = 0, queue_head = queue_tail = NULL;
    if (!thread_started) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, renderer_thread, NULL)) rt_fatal(function, "the renderer's thread cannot start");
        pthread_detach(thread);
        thread_started = 1, thread_process = getpid();
    }
    if (queue_tail) queue_tail->next = b;
    else queue_head = b;
    queue_tail = b;
    pthread_cond_broadcast(&queue_changed);
    pthread_mutex_unlock(&queue_lock);
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
    rt_renderer_wait_idle();
    pthread_mutex_lock(&renderer_lock);
    char error[256];
    const int drawn = executor && rt_executor_read_target(executor, address, frame, width, height, error, sizeof error) == 0;
    pthread_mutex_unlock(&renderer_lock);
    return drawn;
}
