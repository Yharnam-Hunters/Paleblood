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
    const rt_gpu_step at_once = {note_step, "sceGnmSubmitCommandBuffers", 0, {7}};
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
        const rt_gpu_step gate = {gate_step, "sceGnmSubmitCommandBuffers", 0, {9}};
        const rt_gpu_step after = {note_step, "sceGnmSubmitCommandBuffers", 0, {1}};
        const rt_gpu_step drawn = {frame_step, "sceGnmSubmitCommandBuffers", RT_GPU_STEP_AFTER_ALL, {2, at(TARGET)}};
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
        const rt_gpu_step around[] = {{frame_step, "sceGnmSubmitCommandBuffers", 0, {3, at(OTHER_TARGET)}},
                                      {note_step, "sceGnmSubmitCommandBuffers", RT_GPU_STEP_AFTER_ALL, {4}}};
        step_saw_triangle = 1;
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)FRESH, fresh_size, around, 2);
        rt_renderer_wait_idle();
        CHECK(steps_seen() == 5 && step_order[3] == 3 && step_order[4] == 4 && !step_saw_triangle);
        CHECK(rt_renderer_frame(at(OTHER_TARGET), frame, SIZE, SIZE) == 1);
    }

    /* A compute queue's buffer is skipped (sceGnmDingDong): a shader bound in it goes with it, so a
       graphics buffer submitted later over the same memory (from another start) does not pick it up.
       Without the skip it would. */
    {
        enum { QUEUE_BUFFER = 0xc0e80000, QUEUE_BYTES = 0x100, BIND_OFFSET = 0x40, LATER_START = 0x20 };
        static const uint32_t compute_words[RT_COMPUTE_SHADER_WORDS];
        const void *const bind_at = (const void *)(uintptr_t)(QUEUE_BUFFER + BIND_OFFSET);
        size_t calls = 1;
        rt_renderer_bind_shader("sceGnmSetCsShader", RT_RENDERER_COMPUTE_SHADER, bind_at, compute_words);
        rt_renderer_skip("sceGnmDingDong", (const void *)(uintptr_t)QUEUE_BUFFER, QUEUE_BYTES);
        rt_render_submitted(QUEUE_BUFFER + LATER_START, QUEUE_BYTES, &calls);
        CHECK(calls == 0);
        rt_renderer_bind_shader("sceGnmSetCsShader", RT_RENDERER_COMPUTE_SHADER, bind_at, compute_words);
        rt_render_submitted(QUEUE_BUFFER + LATER_START, QUEUE_BYTES, &calls);
        CHECK(calls == 1); /* the leak the skip prevents */
    }

    rt_render_forget();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
