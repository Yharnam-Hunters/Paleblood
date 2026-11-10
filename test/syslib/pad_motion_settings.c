/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libScePad's motion settings (runtime/syslib/pad.c): resetting the orientation and switching the
   angular velocity deadband and tilt correction answer 0 for the open pad and leave the state's
   motion as it was; an unknown handle and a setting other than 0 or 1 stop by name. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef int32_t (*f_pad_open)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*f_pad_handle)(int32_t);
typedef int32_t (*f_pad_setting)(int32_t, uint32_t);
typedef int32_t (*f_pad_block)(int32_t, void *);

enum { OFFSET_ORIENTATION = 12, MOTION_BYTES = 40 }; /* orientation, acceleration, angular velocity */

static f_pad_handle reset;
static f_pad_setting deadband, tilt;
static int32_t open_handle;

static void reset_unknown_handle(void) { reset(open_handle + 1); }
static void deadband_unknown_handle(void) { deadband(open_handle + 1, 1); }
static void tilt_unknown_handle(void) { tilt(open_handle + 1, 0); }
static void deadband_two(void) { deadband(open_handle, 2); }
static void tilt_two(void) { tilt(open_handle, 2); }

/* Which function a child stopped in (rt_fatal_observer), so each stop is checked by its name. */
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

SYSLIB_TEST(pad_motion_settings)
{
    const f_pad_open pad_open = (f_pad_open)fn("libScePad", "scePadOpen");
    const f_pad_handle pad_close = (f_pad_handle)fn("libScePad", "scePadClose");
    const f_pad_block pad_read = (f_pad_block)fn("libScePad", "scePadReadState");
    reset = (f_pad_handle)fn("libScePad", "scePadResetOrientation");
    deadband = (f_pad_setting)fn("libScePad", "scePadSetAngularVelocityDeadbandState");
    tilt = (f_pad_setting)fn("libScePad", "scePadSetTiltCorrectionState");
    CHECK(pad_open && pad_close && pad_read && reset && deadband && tilt);
    if (!(pad_open && pad_close && pad_read && reset && deadband && tilt)) return;
    open_handle = pad_open(1, 0, 0, NULL); /* main's tests initialized the library */
    CHECK(open_handle >= 0);
    unsigned char before[120], after[120];
    CHECK(pad_read(open_handle, before) == 0);
    CHECK(reset(open_handle) == 0);
    CHECK(deadband(open_handle, 1) == 0 && deadband(open_handle, 0) == 0);
    CHECK(tilt(open_handle, 1) == 0 && tilt(open_handle, 0) == 0);
    CHECK(deadband(open_handle, 0xffffff01u) == 0); /* a bool: only the low byte is the argument */
    CHECK(pad_read(open_handle, after) == 0);
    CHECK(!memcmp(before + OFFSET_ORIENTATION, after + OFFSET_ORIENTATION, MOTION_BYTES));
    CHECK(stops_in(reset_unknown_handle, "scePadResetOrientation"));
    CHECK(stops_in(deadband_unknown_handle, "scePadSetAngularVelocityDeadbandState"));
    CHECK(stops_in(tilt_unknown_handle, "scePadSetTiltCorrectionState"));
    CHECK(stops_in(deadband_two, "scePadSetAngularVelocityDeadbandState"));
    CHECK(stops_in(tilt_two, "scePadSetTiltCorrectionState"));
    CHECK(pad_close(open_handle) == 0);
    CHECK(is_implemented("libScePad", "scePadResetOrientation") &&
          is_implemented("libScePad", "scePadSetAngularVelocityDeadbandState") &&
          is_implemented("libScePad", "scePadSetTiltCorrectionState"));
}
