/* SPDX-License-Identifier: GPL-2.0-or-later */
/* _exit (runtime/syslib/kernel.c): the process ends at once with the status, without running atexit
   handlers; the parent sees the low 8 bits. When rt_exit_observer is set it gets the full status
   first, and the process still ends if it returns. Each case runs in a child process. The boot
   harness's observer (runtime/boot.c, guest_exited) is not reached here: these tests do not run a
   boot. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

typedef void (*exit_fn)(int);

static exit_fn guest_exit_call;
static int observer_pipe = -1;

/* Would end the child with 99: seen only if the call ran the atexit handlers. */
static void atexit_ran(void) { _exit(99); }

/* Sends the status it got to the parent, then returns, so the call must end the process itself. */
static void observe_exit(int status)
{
    if (write(observer_pipe, &status, sizeof status) != sizeof status) _exit(98);
}

/* Runs the call with status in a child; with observe, through the observer, whose status lands in
   *observed (left alone when the observer was not called). Returns the child's wait status. */
static int exit_in_child(int status, int observe, int *observed)
{
    int fds[2];
    if (pipe(fds) != 0) return -1;
    const pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        observer_pipe = fds[1];
        if (atexit(atexit_ran) != 0) _exit(97);
        if (observe) rt_exit_observer = observe_exit;
        guest_exit_call(status);
        _exit(96); /* the call returned */
    }
    close(fds[1]);
    int got;
    if (read(fds[0], &got, sizeof got) == sizeof got) *observed = got;
    close(fds[0]);
    int wait_status = -1;
    if (child < 0 || waitpid(child, &wait_status, 0) != child) return -1;
    return wait_status;
}

static int exited_with(int wait_status, int code)
{
    return wait_status != -1 && WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == code;
}

SYSLIB_TEST(kernel_exit)
{
    guest_exit_call = (exit_fn)fn("libkernel", "_exit");
    CHECK(is_implemented("libkernel", "_exit"));
    if (!guest_exit_call) return;

    /* No observer: the status as given, and the atexit handler (exit 99) does not run. */
    int observed = -1;
    CHECK(exited_with(exit_in_child(7, 0, &observed), 7));
    CHECK(exited_with(exit_in_child(0, 0, &observed), 0));
    CHECK(observed == -1);
    /* The parent sees the low 8 bits of the status. */
    CHECK(exited_with(exit_in_child(0x1ff, 0, &observed), 0xff));

    /* With an observer: it gets the full status, and when it returns the process ends with it. */
    observed = -1;
    CHECK(exited_with(exit_in_child(0x1ff, 1, &observed), 0xff));
    CHECK(observed == 0x1ff);
    observed = -1;
    CHECK(exited_with(exit_in_child(5, 1, &observed), 5));
    CHECK(observed == 5);
}
