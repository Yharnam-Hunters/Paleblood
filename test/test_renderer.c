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
#include "runtime/render_executor.h"
#include "runtime/renderer.h"

#include "render_fixture.h"

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

/* A submit that must stop: runs it in a child and checks it ended through rt_fatal. */
static int stops(uint64_t buffer, uint32_t size)
{
    const pid_t child = fork();
    if (child == 0) {
        rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)buffer, size);
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

    /* Off: the submit does nothing, and the flip's frame is left as it was. */
    rt_renderer_configure(0);
    rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)BUFFER, size);
    memset(frame, 0x5a, sizeof frame);
    CHECK(rt_renderer_frame(at(TARGET), frame, SIZE, SIZE) == 0 && frame[0] == 0x5a5a5a5au);

    /* On, but a buffer with no recorded calls: nothing opens, nothing is drawn. */
    rt_renderer_configure(1);
    rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)EMPTY, 0x100);
    CHECK(rt_renderer_frame(at(TARGET), frame, SIZE, SIZE) == 0 && frame[0] == 0x5a5a5a5au);

    /* A call the executor cannot run stops by the submitting function's name. */
    const uint32_t bad_size = record_draw(BAD, at(TARGET) + 0x400000, COLOR_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(stops(BAD, bad_size));

    /* On, with a drawn buffer: the flip's frame for the target's address gets the triangle. */
    rt_renderer_submit("sceGnmSubmitCommandBuffers", (const void *)(uintptr_t)BUFFER, size);
    CHECK(rt_renderer_frame(at(TARGET), frame, SIZE, SIZE) == 1);
    check_triangle_pixels(frame);
    CHECK(rt_renderer_frame(at(OTHER_TARGET), frame, SIZE, SIZE) == 0); /* nothing drawn there */

    rt_render_forget();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
