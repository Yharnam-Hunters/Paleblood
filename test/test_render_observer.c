/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The game library's observer calls (runtime/render_observer.c): each one reaches the host's record
   function of the same kind when the host exports it, does nothing when it does not, and a call the
   record refuses stops through the host's rt_fatal. This program stands in for the host: it exports
   a draw and a register record and rt_fatal, and no viewport record. */
#include "runtime/render_observer.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint64_t seen_buffer;
static uint32_t seen_offset, seen_count, seen_address, seen_value;

__attribute__((visibility("default"))) int rt_render_record_draw_auto(uint64_t buffer, uint32_t offset, uint32_t index_count,
                                                                      char *error, size_t error_size)
{
    if (index_count == 0) {
        snprintf(error, error_size, "a draw of no indices");
        return -1;
    }
    seen_buffer = buffer, seen_offset = offset, seen_count = index_count;
    return 0;
}

__attribute__((visibility("default"))) int rt_render_record_register(uint64_t buffer, uint32_t offset, uint32_t address,
                                                                     uint32_t value, char *error, size_t error_size)
{
    (void)error, (void)error_size;
    seen_buffer = buffer, seen_offset = offset, seen_address = address, seen_value = value;
    return 0;
}

__attribute__((visibility("default"))) void rt_fatal(const char *function, const char *message)
{
    fprintf(stderr, "runtime: %s: %s\n", function, message);
    _exit(42); /* the host's stop, told apart from abort() */
}

int main(void)
{
    rt_observe_draw_auto(0x12340000ull, 0x80, 3);
    CHECK(seen_buffer == 0x12340000ull && seen_offset == 0x80 && seen_count == 3);
    rt_observe_register(0x56780000ull, 0x10, 0xa1e0, 0x20010001u);
    CHECK(seen_buffer == 0x56780000ull && seen_offset == 0x10 && seen_address == 0xa1e0 && seen_value == 0x20010001u);

    /* no viewport record in this host: nothing happens */
    const float scale[3] = {1, 1, 1}, translate[3] = {0, 0, 0};
    rt_observe_viewport(0x9abc0000ull, 0x20, 0, 0, 1, scale, translate);
    CHECK(seen_buffer == 0x56780000ull);

    /* a refused call stops through the host's rt_fatal */
    const pid_t child = fork();
    if (child == 0) {
        rt_observe_draw_auto(0x12340000ull, 0x90, 0);
        _exit(0);
    }
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 42);

    if (!failures) puts("render observer: ok");
    return failures != 0;
}
