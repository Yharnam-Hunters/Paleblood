/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libSceMouse with no mouse connected (runtime/syslib/mouse.c): one record, not connected, every
   field zero; one handle at a time; what is not established stops by name. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef int32_t (*f_none)(void);
typedef int32_t (*f_open)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*f_read)(int32_t, void *, int32_t);
typedef int32_t (*f_handle)(int32_t);

static f_open mouse_open;
static f_read mouse_read;
static f_handle mouse_close;
static int32_t handle;

static void open_before_init(void) { mouse_open(1, 0, 0, NULL); }
static void open_other_user(void) { mouse_open(2, 0, 0, NULL); }
static void open_other_index(void) { mouse_open(1, 0, 1, NULL); }
static void open_other_type(void) { mouse_open(1, 1, 0, NULL); }
static void open_with_param(void) { const uint8_t param[8] = {1}; mouse_open(1, 0, 0, param); }
static void read_no_record(void) { mouse_read(handle, NULL, 1); }
static void read_zero(void) { uint8_t d[40]; mouse_read(handle, d, 0); }
static void read_65(void) { static uint8_t d[40 * 65]; mouse_read(handle, d, 65); }
static void read_unknown_handle(void) { uint8_t d[40]; mouse_read(handle + 1, d, 1); }
static void close_unknown_handle(void) { mouse_close(handle + 1); }
static void close_twice(void) { mouse_close(handle); mouse_close(handle); }

static const char *expected_function;
static void observe_stop(const char *function, const char *reason)
{
    (void)reason;
    _exit(!strcmp(function, expected_function) ? 0 : 1);
}
static int stops_in(void (*call)(void), const char *function)
{
    const pid_t child = fork();
    if (child == 0) {
        expected_function = function;
        rt_fatal_observer = observe_stop;
        call();
        _exit(2); /* the call returned, so it did not stop */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

SYSLIB_TEST(mouse_not_connected)
{
    const f_none mouse_init = (f_none)fn("libSceMouse", "sceMouseInit");
    mouse_open = (f_open)fn("libSceMouse", "sceMouseOpen");
    mouse_read = (f_read)fn("libSceMouse", "sceMouseRead");
    mouse_close = (f_handle)fn("libSceMouse", "sceMouseClose");
    CHECK(mouse_init && mouse_open && mouse_read && mouse_close);
    if (!(mouse_init && mouse_open && mouse_read && mouse_close)) return;
    CHECK(stops_in(open_before_init, "sceMouseOpen"));
    CHECK(mouse_init() == 0 && mouse_init() == 0); /* again: the title calls it from several places */
    handle = mouse_open(1, 0, 0, NULL);
    CHECK(handle == 1);
    CHECK(mouse_open(1, 0, 0, NULL) == (int32_t)0x80DF0004u); /* already open */
    uint8_t data[2 * 40];
    memset(data, 0xee, sizeof data);
    CHECK(mouse_read(handle, data, 2) == 1);
    int all_zero = 1;
    for (int i = 0; i < 40; i++) all_zero &= data[i] == 0;
    CHECK(all_zero && data[40] == 0xee); /* one record, not connected; the second left alone */
    CHECK(stops_in(open_other_user, "sceMouseOpen"));
    CHECK(stops_in(open_other_index, "sceMouseOpen"));
    CHECK(stops_in(open_other_type, "sceMouseOpen"));
    CHECK(stops_in(open_with_param, "sceMouseOpen"));
    CHECK(stops_in(read_no_record, "sceMouseRead"));
    CHECK(stops_in(read_zero, "sceMouseRead"));
    CHECK(stops_in(read_65, "sceMouseRead"));
    CHECK(stops_in(read_unknown_handle, "sceMouseRead"));
    CHECK(stops_in(close_unknown_handle, "sceMouseClose"));
    CHECK(stops_in(close_twice, "sceMouseClose"));
    uint8_t most[64 * 40];
    CHECK(mouse_read(handle, most, 64) == 1);
    CHECK(mouse_close(handle) == 0);
    CHECK(mouse_open(1, 0, 0, NULL) == 1 && mouse_close(1) == 0); /* free again after close */
    CHECK(is_implemented("libSceMouse", "sceMouseInit") && is_implemented("libSceMouse", "sceMouseOpen") &&
          is_implemented("libSceMouse", "sceMouseRead") && is_implemented("libSceMouse", "sceMouseClose"));
}
