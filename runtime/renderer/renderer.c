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

/* Deferred steps (RT_GPU_STEP_DEFERRABLE) waiting for the GPU, oldest first: each with the ticket of
   the last submission before it (runtime/render_executor.h). The completion thread waits for each
   ticket in turn and performs the step, so the renderer's thread can go on recording while the GPU
   runs; steps are performed in submission order. */
typedef struct deferred {
    rt_gpu_step step;
    uint64_t ticket;
    struct deferred *next;
} deferred;

static pthread_mutex_t deferred_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t deferred_changed = PTHREAD_COND_INITIALIZER;
static deferred *deferred_head, *deferred_tail;
static int deferred_started, deferred_busy;
static const deferred *deferred_running; /* the step the completion thread is performing (or waiting for), if any */
static pid_t deferred_process;
static _Thread_local int on_completion_thread;
static uint64_t last_ticket; /* the renderer thread's last submission */

static void *completion_thread(void *unused)
{
    (void)unused;
    on_completion_thread = 1;
    for (;;) {
        pthread_mutex_lock(&deferred_lock);
        while (!deferred_head) pthread_cond_wait(&deferred_changed, &deferred_lock);
        deferred *const d = deferred_head;
        deferred_head = d->next;
        if (!deferred_head) deferred_tail = NULL;
        deferred_busy = 1, deferred_running = d;
        pthread_mutex_unlock(&deferred_lock);
        char error[256];
        /* the executor is open whenever a ticket is: tickets come only from its submissions */
        if (d->ticket && rt_executor_wait_ticket(executor, d->ticket, error, sizeof error)) rt_fatal(d->step.function, error);
        d->step.perform(&d->step);
        pthread_mutex_lock(&deferred_lock);
        deferred_busy = 0, deferred_running = NULL;
        free(d);
        pthread_cond_broadcast(&deferred_changed);
        pthread_mutex_unlock(&deferred_lock);
    }
    return NULL;
}

static void defer_step(const rt_gpu_step *step, uint64_t ticket)
{
    deferred *const d = malloc(sizeof *d);
    if (!d) rt_fatal(step->function, "no memory for a GPU step");
    *d = (deferred){*step, ticket, NULL};
    pthread_mutex_lock(&deferred_lock);
    if (deferred_started && deferred_process != getpid()) deferred_started = 0, deferred_busy = 0, deferred_head = deferred_tail = NULL;
    if (!deferred_started) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, completion_thread, NULL)) rt_fatal(step->function, "the renderer's completion thread cannot start");
        pthread_detach(thread);
        deferred_started = 1, deferred_process = getpid();
    }
    if (deferred_tail) deferred_tail->next = d;
    else deferred_head = d;
    deferred_tail = d;
    pthread_cond_broadcast(&deferred_changed);
    pthread_mutex_unlock(&deferred_lock);
}

/* Whether a wait step will hold once the deferred steps not yet performed have been: one of them
   writes what it waits for (rt_gpu_step's holds). */
static int deferred_satisfies(const rt_gpu_step *wait)
{
    pthread_mutex_lock(&deferred_lock);
    int satisfied = deferred_running && wait->holds(wait, &deferred_running->step);
    for (const deferred *d = deferred_head; d && !satisfied; d = d->next) satisfied = wait->holds(wait, &d->step);
    pthread_mutex_unlock(&deferred_lock);
    return satisfied;
}

/* Waits until every deferred step so far has been performed. */
static void drain_deferred(void)
{
    pthread_mutex_lock(&deferred_lock);
    while (deferred_head || deferred_busy) pthread_cond_wait(&deferred_changed, &deferred_lock);
    pthread_mutex_unlock(&deferred_lock);
}

/* Records calls [first, last) of a batch into the executor's open batch; their targets are written back
   by the work recorded. The open batch goes to the GPU when the queue runs dry, when the calls recorded
   since the last submission reach SUBMIT_CALLS, or before the renderer waits for the GPU (submit_open):
   one Vulkan submission for many of the game's command buffers and their segments between steps,
   instead of one each, each with its fence wait for a free batch slot. */
enum { SUBMIT_CALLS = 512 };
static size_t calls_unsubmitted;

static void run_calls(batch *b, size_t first, size_t last)
{
    if (first == last) return;
    pthread_mutex_lock(&renderer_lock);
    rt_executor *const x = executor_for(b->function);
    char error[256];
    uint64_t ticket = 0;
    if (x && rt_executor_record_calls(x, b->calls + first, last - first, &ticket, error, sizeof error)) rt_fatal(b->function, error);
    if (x) last_ticket = ticket, calls_unsubmitted += last - first;
    pthread_mutex_unlock(&renderer_lock);
}

/* Hands the executor's open batch to the GPU (function names the submission for a stop). */
static void submit_open(const char *function)
{
    pthread_mutex_lock(&renderer_lock);
    char error[256];
    if (executor && rt_executor_submit(executor, error, sizeof error)) rt_fatal(function, error);
    calls_unsubmitted = 0;
    pthread_mutex_unlock(&renderer_lock);
}

/* Each step once the calls before its offset have run, and before the calls after it: a deferrable
   step is handed to the completion thread with the ticket of the work before it; a wait that holds
   already is performed at once, and one that a deferred step will satisfy is passed (the GPU runs the
   work after it after the work before it in any case); any other waits for all earlier steps and
   work, and is performed here. */
static void run_batch(batch *b)
{
    size_t done = 0;
    for (size_t i = 0; i < b->step_count; i++) {
        size_t upto = done;
        while (upto < b->call_count && b->calls[upto].offset < b->steps[i].offset) upto++;
        run_calls(b, done, upto);
        done = upto;
        if (b->steps[i].flags & RT_GPU_STEP_DEFERRABLE) {
            defer_step(&b->steps[i], last_ticket);
            continue;
        }
        if (b->steps[i].holds && b->steps[i].holds(&b->steps[i], NULL)) { /* a wait that holds already: the GPU passes it */
            b->steps[i].perform(&b->steps[i]);
            continue;
        }
        if (b->steps[i].holds && deferred_satisfies(&b->steps[i])) continue; /* the work ahead of it on the GPU writes what it waits for */
        submit_open(b->steps[i].function); /* the deferred steps may wait for the open batch */
        drain_deferred();
        pthread_mutex_lock(&renderer_lock);
        char error[256];
        if (executor && rt_executor_finish(executor, error, sizeof error)) rt_fatal(b->steps[i].function, error);
        pthread_mutex_unlock(&renderer_lock);
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
        pthread_mutex_lock(&queue_lock);
        const int queue_dry = queue_head == NULL;
        pthread_mutex_unlock(&queue_lock);
        if (queue_dry || calls_unsubmitted >= SUBMIT_CALLS) submit_open(b->function); /* nothing to join it soon, or enough to start on */
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
    if (on_renderer_thread || on_completion_thread) return; /* their own steps: everything before them has run */
    pthread_mutex_lock(&queue_lock);
    while (queue_head || running) pthread_cond_wait(&queue_changed, &queue_lock);
    pthread_mutex_unlock(&queue_lock);
    drain_deferred(); /* the last batch's deferred steps (its work: their tickets) */
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
