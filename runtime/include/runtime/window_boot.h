/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_WINDOW_BOOT_H
#define RUNTIME_WINDOW_BOOT_H
#include <signal.h>
#ifdef __cplusplus
extern "C" {
#endif
/* AppKit owns the process main thread. The callback owns all boot/recovery/TLS work. */
typedef int (*rt_window_boot_fn)(void *options, const sigset_t *pre_gui_mask);
int rt_display_window_run(rt_window_boot_fn run, void *options);
/* Returning GUI callback: publish the first stop; never unwind or park the GUI thread. */
void rt_boot_window_request_stop(const char *function, const char *reason);
#ifdef __cplusplus
}
#endif
#endif
