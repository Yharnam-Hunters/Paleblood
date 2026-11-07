/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_DISPLAY_WINDOW_H
#define RUNTIME_DISPLAY_WINDOW_H

/* The display's window back end: display_vulkan.c (Vulkan and SDL3) when the build found them,
   otherwise display_window_none.c, which has none. Called with the display's lock held. */

#include <stdint.h>

int rt_display_window_available(void);
/* Opens a window for frames of width x height pixels; 0 on success. */
int rt_display_window_open(uint32_t width, uint32_t height);
/* Shows a frame (BGRA, width x height) in the window. */
void rt_display_window_show(const uint32_t *pixels, uint32_t width, uint32_t height);

#endif
