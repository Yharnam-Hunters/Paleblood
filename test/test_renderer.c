/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer behind the runtime's GPU calls (runtime/renderer/renderer.c), as sceGnmSubmit* and a
   video-out flip use it: off, it does nothing; on, a submitted buffer with recorded drawing calls
   runs on the GPU and the flip's frame gets what was drawn; a buffer without calls opens nothing;
   a call the executor cannot run stops by the submitting function's name.

   The runtime's memory module and fatal stop are stood in for here: the GPU-visible memory is the
   fixture's (render_fixture.h), and rt_fatal ends the process with a code the test checks. Without a
   suitable Vulkan device the test reports itself skipped (exit 77). */
#define _GNU_SOURCE /* memfd_create, MAP_FIXED_NOREPLACE (render_fixture.h) */
#include "runtime/gpu_memory.h"
#include "runtime/process.h"
#include "runtime/render_calls.h"
#include "runtime/render_executor.h"
#include "runtime/renderer.h"

#include "gcn_programs.h"
#include "render_fixture.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>

enum { FATAL_EXIT = 3 };
static int guest_fd = -1;

uint64_t rt_gpu_memory_base(void) { return GUEST_BASE; }

int rt_gpu_memory_pool(int *fd, uint64_t *size)
{
    if (guest_fd < 0) return -1;
    *fd = guest_fd, *size = GUEST_SIZE;
    return 0;
}

_Noreturn void rt_fatal(const char *function, const char *message)
{
    fprintf(stderr, "stopped (as expected): %s: %s\n", function, message);
    exit(FATAL_EXIT);
}

/* GPU steps for the tests: each appends its words[0] to the order they ran in. The gate step holds
   the renderer's thread until the test opens it; the frame step reads what has been drawn into the
   target at words[1] (only possible once the calls before it have run). */
static pthread_mutex_t step_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t step_changed = PTHREAD_COND_INITIALIZER;
static uint64_t step_order[8];
static int step_count, gate_open, step_saw_triangle;

static void note_step(const rt_gpu_step *step)
{
    pthread_mutex_lock(&step_lock);
    if (step_count < 8) step_order[step_count++] = step->words[0];
    pthread_cond_broadcast(&step_changed);
    pthread_mutex_unlock(&step_lock);
}

static void gate_step(const rt_gpu_step *step)
{
    pthread_mutex_lock(&step_lock);
    while (!gate_open) pthread_cond_wait(&step_changed, &step_lock);
    pthread_mutex_unlock(&step_lock);
    note_step(step);
}

static void frame_step(const rt_gpu_step *step)
{
    uint32_t frame[SIZE * SIZE];
    step_saw_triangle = rt_renderer_frame(step->words[1], frame, SIZE, SIZE) == 1 && frame[0] != 0x5a5a5a5au;
    note_step(step);
}

static int already_holds(const rt_gpu_step *step, const rt_gpu_step *pending)
{
    (void)step, (void)pending;
    return 1;
}

/* A wait that holds only once a pending step with words[0] 11 has been performed. */
static int held_by_eleven(const rt_gpu_step *step, const rt_gpu_step *pending)
{
    (void)step;
    return pending && pending->words[0] == 11;
}

static int steps_seen(void)
{
    pthread_mutex_lock(&step_lock);
    const int n = step_count;
    pthread_mutex_unlock(&step_lock);
    return n;
}

/* A submit that must stop: runs it in a child and checks it ended through rt_fatal. */
static int stops(uint64_t buffer, uint32_t size)
{
    const pid_t child = fork();
    if (child == 0) {
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)buffer, size, NULL, 0);
        rt_renderer_wait_idle(); /* the stop comes from the renderer's thread */
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == FATAL_EXIT;
}

int main(void)
{
    guest_fd = map_guest_memory();
    if (guest_fd < 0) {
        fprintf(stderr, "cannot map the guest memory at %#llx\n", GUEST_BASE);
        return 1;
    }
    { /* skipped without a device that can run the executor */
        rt_executor *probe = NULL;
        char error[256];
        const rt_executor_memory memory = {guest_fd, GUEST_SIZE, GUEST_BASE};
        if (rt_executor_open(&memory, &probe, error, sizeof error)) {
            fprintf(stderr, "skipped: %s\n", error);
            return 77;
        }
        rt_executor_close(probe);
    }
    enum { BUFFER = 0xc0de0000, EMPTY = 0xc0df0000, BAD = 0xc0e00000 };
    uint32_t frame[SIZE * SIZE];
    rt_render_forget();
    const uint32_t size = record_draw(BUFFER, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);

    /* Off: the submit draws nothing and performs its steps at once; the flip's frame is left as it was. */
    rt_renderer_configure(0);
    const rt_gpu_step at_once = {note_step, "sceGnmSubmitCommandBuffers", 0, {7}, 0, NULL};
    rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)BUFFER, size, &at_once, 1);
    CHECK(steps_seen() == 1 && step_order[0] == 7);
    step_count = 0;
    memset(frame, 0x5a, sizeof frame);
    CHECK(rt_renderer_frame(at(TARGET), frame, SIZE, SIZE) == 0 && frame[0] == 0x5a5a5a5au);

    /* On, but a buffer with no recorded calls: nothing opens, nothing is drawn. */
    rt_renderer_configure(1);
    rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)EMPTY, 0x100, NULL, 0);
    CHECK(rt_renderer_frame(at(TARGET), frame, SIZE, SIZE) == 0 && frame[0] == 0x5a5a5a5au);

    /* A call the executor cannot run stops by the submitting function's name. */
    const uint32_t bad_size = record_draw(BAD, at(TARGET) + 0x400000, COLOR_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(stops(BAD, bad_size));

    /* On, with a drawn buffer: the flip's frame for the target's address gets the triangle. */
    rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)BUFFER, size, NULL, 0);
    CHECK(rt_renderer_frame(at(TARGET), frame, SIZE, SIZE) == 1);
    check_triangle_pixels(frame);
    CHECK(rt_renderer_frame(at(OTHER_TARGET), frame, SIZE, SIZE) == 0); /* nothing drawn there (yet) */

    /* Submissions run on the renderer's thread, in order, and the submit returns at once: a first
       submission's step holds the thread (gate), a second's step (1) cannot run before it, and both
       submits have returned while neither step has. Opened, the steps run gate first. A step after a
       drawn buffer's calls sees what they drew (the calls ran before it). */
    {
        enum { GATED = 0xc0e90000 };
        const rt_gpu_step gate = {gate_step, "sceGnmSubmitCommandBuffers", 0, {9}, 0, NULL};
        const rt_gpu_step after = {note_step, "sceGnmSubmitCommandBuffers", 0, {1}, 0, NULL};
        const rt_gpu_step drawn = {frame_step, "sceGnmSubmitCommandBuffers", RT_GPU_STEP_AFTER_ALL, {2, at(TARGET)}, 0, NULL};
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)GATED, 0x100, &gate, 1);
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)(GATED + 0x1000), 0x100, &after, 1);
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)BUFFER, size, &drawn, 1);
        CHECK(steps_seen() == 0); /* returned before the work */
        pthread_mutex_lock(&step_lock);
        gate_open = 1;
        pthread_cond_broadcast(&step_changed);
        pthread_mutex_unlock(&step_lock);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 3 && step_order[0] == 9 && step_order[1] == 1 && step_order[2] == 2);
        CHECK(step_saw_triangle);
        /* A step at the start of a buffer runs before its calls (a wait holds before the draws after
           it): the frame there is not drawn yet; a step after all of them sees it drawn. */
        enum { FRESH = 0xc0ea0000 };
        const uint32_t fresh_size = record_draw(FRESH, at(OTHER_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const rt_gpu_step around[] = {{frame_step, "sceGnmSubmitCommandBuffers", 0, {3, at(OTHER_TARGET)}, 0, NULL},
                                      {note_step, "sceGnmSubmitCommandBuffers", RT_GPU_STEP_AFTER_ALL, {4}, 0, NULL}};
        step_saw_triangle = 1;
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)FRESH, fresh_size, around, 2);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 5 && step_order[3] == 3 && step_order[4] == 4 && !step_saw_triangle);
        CHECK(rt_renderer_frame(at(OTHER_TARGET), frame, SIZE, SIZE) == 1);
    }

    /* Deferrable steps ("work done" writes) are performed on the completion thread once the GPU has
       run the work before them, in order with the other steps: one after a drawn buffer's calls sees
       them drawn; a deferrable step held closed (gate) keeps a later wait-like step (6) and a later
       deferrable one (7) behind it; rt_renderer_wait_idle waits for them all. */
    {
        enum { DEFERRED = 0xc0eb0000, THIRD_TARGET = 0x300000 };
        step_count = 0, step_saw_triangle = 0;
        gate_open = 0;
        const uint32_t deferred_size = record_draw(DEFERRED, at(THIRD_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const rt_gpu_step seen = {frame_step, "sceGnmSubmitCommandBuffers", RT_GPU_STEP_AFTER_ALL, {5, at(THIRD_TARGET)}, RT_GPU_STEP_DEFERRABLE, NULL};
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)DEFERRED, deferred_size, &seen, 1);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 1 && step_order[0] == 5 && step_saw_triangle);
        const rt_gpu_step held = {gate_step, "sceGnmSubmitCommandBuffers", 0, {8}, RT_GPU_STEP_DEFERRABLE, NULL};
        const rt_gpu_step waiting = {note_step, "sceGnmSubmitCommandBuffers", 0, {6}, 0, NULL};
        const rt_gpu_step later = {note_step, "sceGnmSubmitCommandBuffers", 0, {7}, RT_GPU_STEP_DEFERRABLE, NULL};
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)DEFERRED, deferred_size, &held, 1);
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)(DEFERRED + 0x1000), 0x100, &waiting, 1);
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)(DEFERRED + 0x2000), 0x100, &later, 1);
        CHECK(steps_seen() == 1);
        pthread_mutex_lock(&step_lock);
        gate_open = 1;
        pthread_cond_broadcast(&step_changed);
        pthread_mutex_unlock(&step_lock);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 4 && step_order[1] == 8 && step_order[2] == 6 && step_order[3] == 7);
        /* A wait that holds already (its holds says so) is performed at once, ahead of a deferrable step
           still held: the GPU would pass it, so nothing waits for the work or the steps before it. */
        gate_open = 0;
        const rt_gpu_step passing = {note_step, "sceGnmSubmitCommandBuffers", 0, {10}, 0, already_holds};
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)DEFERRED, deferred_size, &held, 1);
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)(DEFERRED + 0x1000), 0x100, &passing, 1);
        for (int tries = 0; tries < 2000 && steps_seen() < 5; tries++) usleep(1000);
        CHECK(steps_seen() == 5 && step_order[4] == 10);
        pthread_mutex_lock(&step_lock);
        gate_open = 1;
        pthread_cond_broadcast(&step_changed);
        pthread_mutex_unlock(&step_lock);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 6 && step_order[5] == 8);
        /* A wait that a still-held deferrable step (11) will satisfy is passed: a step after it (12) is
           not held up by the gate; the wait itself is never performed. */
        step_count = 0, gate_open = 0;
        const rt_gpu_step eleven = {gate_step, "sceGnmSubmitCommandBuffers", 0, {11}, RT_GPU_STEP_DEFERRABLE, NULL};
        const rt_gpu_step satisfied = {note_step, "sceGnmSubmitCommandBuffers", 0, {13}, 0, held_by_eleven};
        const rt_gpu_step twelve = {note_step, "sceGnmSubmitCommandBuffers", 0, {12}, 0, already_holds};
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)DEFERRED, deferred_size, &eleven, 1);
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)(DEFERRED + 0x1000), 0x100, &satisfied, 1);
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)(DEFERRED + 0x2000), 0x100, &twelve, 1);
        for (int tries = 0; tries < 2000 && steps_seen() < 1; tries++) usleep(1000);
        CHECK(steps_seen() == 1 && step_order[0] == 12);
        pthread_mutex_lock(&step_lock);
        gate_open = 1;
        pthread_cond_broadcast(&step_changed);
        pthread_mutex_unlock(&step_lock);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 2 && step_order[1] == 11);
    }

    /* A compute queue's buffer (sceGnmDingDong submits each indirect buffer the ring points to) runs
       like a graphics one: its dispatch (scale_add: out[i] = in[i] * 2.5 + 1, one workgroup of 64)
       has stored its results by the time its completion step is performed. Its calls are taken, so
       a graphics buffer submitted later over the same memory does not pick up its shader. */
    {
        enum { QUEUE_BUFFER = 0xc0e80000, QUEUE_BYTES = 0x100, COMPUTE_CODE = 0x3000, INPUT = 0x20000, OUTPUT = 0x21000, LANES = 64 };
        char error[256] = "";
        memcpy((void *)(uintptr_t)at(COMPUTE_CODE), scale_add, sizeof scale_add);
        float *input = (float *)(uintptr_t)at(INPUT), *output = (float *)(uintptr_t)at(OUTPUT);
        for (unsigned i = 0; i < LANES; i++) input[i] = (float)i, output[i] = -1;
        const uint64_t code = at(COMPUTE_CODE);
        const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, 8 << 1, LANES, 1, 1};
        uint32_t user[8];
        descriptor(user, at(INPUT), 4);
        descriptor(user + 4, at(OUTPUT), 4);
        const uint32_t one[3] = {1, 1, 1};
        CHECK(rt_render_record_compute_shader_at(QUEUE_BUFFER + 0x10, shader) == 0);
        CHECK(rt_render_record_user_data(QUEUE_BUFFER, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_dispatch(QUEUE_BUFFER, 0x30, one, error, sizeof error) == 0);
        step_count = 0;
        const rt_gpu_step done = {note_step, "sceGnmDingDong", 0x40, {21}, 0, NULL};
        rt_renderer_submit("sceGnmDingDong", (const void *)(uintptr_t)QUEUE_BUFFER, QUEUE_BYTES, &done, 1);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 1 && step_order[0] == 21);
        int right = 1;
        for (unsigned i = 0; i < LANES; i++) right &= output[i] == (float)i * 2.5f + 1.0f;
        CHECK(right);
        size_t calls = 1;
        rt_render_submitted(QUEUE_BUFFER + 0x08, QUEUE_BYTES, &calls);
        CHECK(calls == 0);
    }

    rt_render_forget();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
