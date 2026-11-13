/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer behind the runtime's GPU calls (runtime/renderer.h), using the selected executor. */
#include "runtime/renderer.h"

#include "runtime/gpu_memory.h"
#include "runtime/process.h"
#include "runtime/render_calls.h"
#include "runtime/render_executor.h"
#include "runtime/write_watch.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The executor's reasons: long enough for an unfinished batch's list of draws and dispatches. */
enum { RENDER_ERROR_BYTES = 1024 };

static pthread_mutex_t renderer_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int holds_renderer_lock; /* a fault this thread takes while holding it cannot be resolved */
static int enabled;
static rt_executor *executor;

static void lock_renderer(void)
{
    pthread_mutex_lock(&renderer_lock);
    holds_renderer_lock = 1;
}

static void unlock_renderer(void)
{
    holds_renderer_lock = 0;
    pthread_mutex_unlock(&renderer_lock);
}

void rt_renderer_configure(int on) { enabled = on; }

static int collect_cpu_writes(const uint64_t (*ranges)[2], size_t count, void (*written)(void *context, uint64_t pool_offset, uint64_t length),
                              void *context);

/* Render targets drawn into are guarded from the CPU until written back (runtime/write_watch.h,
   rt_executor_memory): by pool offset there, by guest address in the executor. */
static int guard_target(uint64_t address, uint64_t length) { return rt_write_watch_guard(address - rt_gpu_memory_base(), length); }
static void unguard_target(uint64_t address, uint64_t length) { rt_write_watch_unguard(address - rt_gpu_memory_base(), length); }

/* A CPU access to a guarded target's memory (on the write watch's thread, while the accessing thread
   waits, or before a system call): the executor writes the targets there back and lifts their guards.
   The renderer's own threads read no guest memory while recording; one that faults holding the
   renderer's lock would wait for itself, and stops instead. */
static void resolve_guarded(uint64_t pool_offset, uint64_t length)
{
    static const char function[] = "the renderer (a CPU access to a render target's memory)";
    if (holds_renderer_lock) rt_fatal(function, "the renderer's own thread touched a guarded render target");
    lock_renderer();
    char error[RENDER_ERROR_BYTES];
    if (executor && rt_executor_resolve(executor, rt_gpu_memory_base() + pool_offset, length, error, sizeof error)) rt_fatal(function, error);
    unlock_renderer();
}

/* The executor, opened once the title has GPU-visible memory; NULL before that or when off. */
static rt_executor *executor_for(const char *function)
{
    if (!enabled || executor) return executor;
    /* the CPU's writes, when the runtime watches them (runtime/write_watch.h): textures reload only then */
    const int watched = rt_write_watch_active();
    rt_executor_memory memory = {.guest_base = rt_gpu_memory_base(), .collect_cpu_writes = watched ? collect_cpu_writes : NULL,
                                 .guard = watched ? guard_target : NULL, .unguard = watched ? unguard_target : NULL,
                                 .gds = rt_gpu_memory_gds(), .gds_bytes = RT_GPU_GDS_BYTES};
    if (rt_gpu_memory_pool(&memory.fd, &memory.size)) return NULL; /* no direct memory yet: nothing to draw from */
    if (watched) rt_write_watch_set_resolver(resolve_guarded);
    char error[RENDER_ERROR_BYTES];
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
    uint64_t submission;             /* its place among the submissions, from 1 */
    struct batch *next;
} batch;

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_changed = PTHREAD_COND_INITIALIZER;
static batch *queue_head, *queue_tail;
static int thread_started, running; /* running: a batch taken off the queue and not finished */
static uint64_t submissions;        /* buffers submitted so far (queue_lock) */
/* The CPU's writes (runtime/write_watch.h) are collected at most once for all the buffers submitted
   before the collect: running_submission is the buffer being run, collected_through the last buffer
   submitted before the last collect. Scanning the watched memory for each buffer would cost more than
   the reloads it saves (the character creation screen submits 70 buffers at once). */
static uint64_t running_submission, collected_through;
static pid_t thread_process;        /* a child after fork has no renderer thread: it starts its own */
/* How far the game may submit ahead of the renderer's thread: a submission returns only once the
   renderer has taken a buffer at most RENDER_LAG_LIMIT before it (queue_lock). The console's GPU starts
   on a buffer soon after it is submitted, and the game's per-frame GPU memory rings rely on it: with
   no bound, the renderer fell tens of seconds behind during the clinic's load (each constant-engine
   step waits for the GPU), the game's ring allocations ran out, and it wrote draws with null table
   pointers that faulted the GPU (12 November 2026). While the renderer's thread performs a wait step
   (one with holds: memory the game may have yet to write, step_waiting), submissions are not held, so
   that wait can end. */
enum { RENDER_LAG_LIMIT = 4 };
static int step_waiting;            /* the renderer's thread is performing a waiting step (queue_lock) */
static _Thread_local int on_renderer_thread;

/* Deferred steps (RT_GPU_STEP_DEFERRABLE) waiting for the GPU, oldest first: each with the ticket of
   the last submission before it (runtime/render_executor.h). The completion thread waits for each
   ticket in turn and performs the step, so the renderer's thread can go on recording while the GPU
   runs; steps are performed in submission order. */
typedef struct deferred {
    rt_gpu_step step;
    uint64_t ticket;
    struct deferred *next;
    int has_snapshot;                /* a flip's copy of its target (RT_GPU_STEP_SHOWS_TARGET) */
    rt_executor_snapshot snapshot;
} deferred;

static pthread_mutex_t deferred_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t deferred_changed = PTHREAD_COND_INITIALIZER;
static deferred *deferred_head, *deferred_tail;
static int deferred_started, deferred_busy;
static const deferred *deferred_running; /* the step the completion thread is performing (or waiting for), if any */
static pid_t deferred_process;
static _Thread_local int on_completion_thread;
static _Thread_local const deferred *performing; /* the step the completion thread is performing */
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
        char error[RENDER_ERROR_BYTES];
        /* the executor is open whenever a ticket is: tickets come only from its submissions */
        if (d->ticket && rt_executor_wait_ticket(executor, d->ticket, error, sizeof error)) rt_fatal(d->step.function, error);
        performing = d;
        d->step.perform(&d->step);
        performing = NULL;
        if (d->has_snapshot) rt_executor_release_snapshot(executor, &d->snapshot);
        pthread_mutex_lock(&deferred_lock);
        deferred_busy = 0, deferred_running = NULL;
        free(d);
        pthread_cond_broadcast(&deferred_changed);
        pthread_mutex_unlock(&deferred_lock);
    }
    return NULL;
}

static void defer_step(const rt_gpu_step *step, uint64_t ticket, const rt_executor_snapshot *snapshot)
{
    deferred *const d = malloc(sizeof *d);
    if (!d) rt_fatal(step->function, "no memory for a GPU step");
    *d = (deferred){*step, ticket, NULL, snapshot != NULL, snapshot ? *snapshot : (rt_executor_snapshot){0, 0}};
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
    lock_renderer();
    rt_executor *const x = executor_for(b->function);
    char error[RENDER_ERROR_BYTES];
    uint64_t ticket = 0;
    if (x && rt_executor_record_calls(x, b->calls + first, last - first, &ticket, error, sizeof error)) rt_fatal(b->function, error);
    if (x) last_ticket = ticket, calls_unsubmitted += last - first;
    unlock_renderer();
}

/* Hands the executor's open batch to the GPU (function names the submission for a stop). */
static void submit_open(const char *function)
{
    lock_renderer();
    char error[RENDER_ERROR_BYTES];
    if (executor && rt_executor_submit(executor, error, sizeof error)) rt_fatal(function, error);
    calls_unsubmitted = 0;
    unlock_renderer();
}

/* A flip (RT_GPU_STEP_SHOWS_TARGET): its target copied with the work recorded so far, and the step
   performed from the completion thread once the copy has run. Without a copy (no draw has used the
   target yet) the step still comes later, and shows nothing. */
static void show_later(const rt_gpu_step *step)
{
    lock_renderer();
    char error[RENDER_ERROR_BYTES];
    rt_executor_snapshot snapshot;
    const int copied = executor && rt_executor_snapshot_target(executor, step->words[4], &snapshot, error, sizeof error) == 0;
    unlock_renderer();
    submit_open(step->function); /* the frame goes to the GPU now, not with the next one's work */
    defer_step(step, copied ? snapshot.ticket : last_ticket, copied ? &snapshot : NULL);
}

/* Each step once the calls before its offset have run, and before the calls after it: a deferrable
   step is handed to the completion thread with the ticket of the work before it; a wait that holds
   already is performed at once, and one that a deferred step will satisfy is passed (the GPU runs the
   work after it after the work before it in any case); any other waits for all earlier steps and
   work, and is performed here. */
/* Performs a step on the renderer's thread. A wait step (holds) may be for memory a submitting thread
   has yet to write, so submissions are not held while it runs (RENDER_LAG_LIMIT). */
static void set_step_waiting(int waiting)
{
    pthread_mutex_lock(&queue_lock);
    step_waiting = waiting;
    pthread_cond_broadcast(&queue_changed);
    pthread_mutex_unlock(&queue_lock);
}

static void perform_step(const rt_gpu_step *step)
{
    if (!step->holds) {
        step->perform(step);
        return;
    }
    set_step_waiting(1);
    step->perform(step);
    set_step_waiting(0);
}

static void run_batch(batch *b)
{
    size_t done = 0;
    for (size_t i = 0; i < b->step_count; i++) {
        size_t upto = done;
        while (upto < b->call_count && b->calls[upto].offset < b->steps[i].offset) upto++;
        run_calls(b, done, upto);
        done = upto;
        if (b->steps[i].flags & RT_GPU_STEP_DEFERRABLE) {
            defer_step(&b->steps[i], last_ticket, NULL);
            continue;
        }
        if (b->steps[i].flags & RT_GPU_STEP_SHOWS_TARGET) {
            show_later(&b->steps[i]);
            continue;
        }
        if (b->steps[i].holds && b->steps[i].holds(&b->steps[i], NULL)) { /* a wait that holds already: the GPU passes it */
            perform_step(&b->steps[i]);
            continue;
        }
        if (b->steps[i].holds && deferred_satisfies(&b->steps[i])) continue; /* the work ahead of it on the GPU writes what it waits for */
        submit_open(b->steps[i].function); /* the deferred steps may wait for the open batch */
        drain_deferred();
        lock_renderer();
        char error[RENDER_ERROR_BYTES];
        if (executor && rt_executor_finish(executor, error, sizeof error)) rt_fatal(b->steps[i].function, error);
        unlock_renderer();
        perform_step(&b->steps[i]);
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
        running = 1, running_submission = b->submission;
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
    b->submission = ++submissions;
    pthread_cond_broadcast(&queue_changed);
    if (!on_renderer_thread && !on_completion_thread)
        while (b->submission > running_submission + RENDER_LAG_LIMIT && !step_waiting) pthread_cond_wait(&queue_changed, &queue_lock);
    pthread_mutex_unlock(&queue_lock);
}

/* The executor's collect (on the renderer's thread): nothing new for a buffer submitted before the
   last collect, whose writes before it that collect has reported. */
static int collect_cpu_writes(const uint64_t (*ranges)[2], size_t count, void (*written)(void *context, uint64_t pool_offset, uint64_t length),
                              void *context)
{
    if (running_submission <= collected_through) return 0;
    pthread_mutex_lock(&queue_lock);
    const uint64_t through = submissions;
    pthread_mutex_unlock(&queue_lock);
    if (rt_write_watch_collect(ranges, count, written, context)) return -1;
    collected_through = through;
    return 0;
}

void rt_renderer_bind_shader(const char *function, rt_renderer_shader kind, const void *at, const uint32_t *words)
{
    if (!enabled) return; /* nothing would take the call: boots without graphics record nothing */
    (void)function;
    const uint64_t address = (uint64_t)(uintptr_t)at;
    static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS]; /* code address 0: a draw with it stops */
    if (kind == RT_RENDERER_VERTEX_SHADER) rt_render_record_vertex_shader_at(address, words);
    else if (kind == RT_RENDERER_COMPUTE_SHADER) rt_render_record_compute_shader_at(address, words);
    else if (kind == RT_RENDERER_LOCAL_SHADER) rt_render_record_local_shader_at(address, words);
    else if (kind == RT_RENDERER_HULL_SHADER) rt_render_record_hull_shader_at(address, words);
    else if (kind == RT_RENDERER_EXPORT_SHADER) rt_render_record_export_shader_at(address, words);
    else if (kind == RT_RENDERER_GEOMETRY_SHADER) rt_render_record_geometry_shader_at(address, words);
    else rt_render_record_pixel_shader_at(address, words ? words : no_pixel_shader);
}

int rt_renderer_frame(uint64_t address, uint32_t *frame, uint32_t width, uint32_t height)
{
    if (on_completion_thread && performing && performing->has_snapshot) { /* a flip performed later (show_later): its copy */
        char error[RENDER_ERROR_BYTES];
        return rt_executor_read_snapshot(executor, &performing->snapshot, frame, width, height, error, sizeof error) == 0;
    }
    rt_renderer_wait_idle(); /* returns at once on the renderer's and the completion thread: the work before their steps has run */
    lock_renderer();
    char error[RENDER_ERROR_BYTES];
    const int drawn = executor && rt_executor_read_target(executor, address, frame, width, height, error, sizeof error) == 0;
    unlock_renderer();
    return drawn;
}
