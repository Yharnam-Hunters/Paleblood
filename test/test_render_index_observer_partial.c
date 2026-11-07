/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A legacy host with a recorder must not silently lose a new indexed event. */
#include "runtime/render_observer.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int rt_render_record_draw_auto(uint64_t buffer, uint32_t offset, uint32_t count, char *error, size_t capacity)
{
    (void)buffer; (void)offset; (void)count; (void)error; (void)capacity;
    return 0;
}

int rt_render_record_draw_index_offset(uint64_t buffer, uint32_t offset, uint32_t first, uint32_t count,
                                       char *error, size_t capacity)
{
    (void)buffer; (void)offset; (void)first; (void)count; (void)error; (void)capacity;
    _exit(44); /* A raw call reaching the legacy export is a test failure. */
}

void rt_fatal(const char *function, const char *reason)
{
    _exit(strstr(function, "render observer:") && strstr(reason, "missing its raw index recorder") ? 42 : 43);
}

int main(void)
{
    if (!rt_render_observer_available()) return 1;
    /* An old host may still expose the legacy first/count entrypoint. Raw calls must not
       fall back to it, even when a raw header was overwritten with zero. */
    for (unsigned which = 0; which < 2; ++which) {
        const pid_t child = fork();
        if (child < 0) return 1;
        if (!child) {
            if (which) rt_observe_draw_index_offset_packet(1, 0, 0, 0, 0, 0, 0);
            else rt_observe_draw_indexed(1, 0, 0, 0, 0, 0, 0);
            _exit(1);
        }
        int status;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 42) return 1;
    }
    puts("index observer: two missing raw providers refused by the host fatal route");
    return 0;
}
