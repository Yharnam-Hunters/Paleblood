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

void rt_fatal(const char *function, const char *reason)
{
    _exit(strstr(function, "render observer:") && strstr(reason, "missing its index recorder") ? 42 : 43);
}

int main(void)
{
    if (!rt_render_observer_available()) return 1;
    for (unsigned which = 0; which < 5; ++which) {
        const pid_t child = fork();
        if (child < 0) return 1;
        if (!child) {
            switch (which) {
            case 0: rt_observe_index_type(1, 0, 0); break;
            case 1: rt_observe_index_base(1, 0, 0); break;
            case 2: rt_observe_index_buffer_size(1, 0, 0); break;
            case 3: rt_observe_draw_indexed(1, 0, 0, 0, 0, 0, 0); break;
            case 4: rt_observe_draw_indexed_offset(1, 0, 0, 0, 0, 0, 0); break;
            }
            _exit(1);
        }
        int status;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 42) return 1;
    }
    puts("index observer: five missing providers refused by the host fatal route");
    return 0;
}
