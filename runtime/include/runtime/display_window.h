/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_DISPLAY_WINDOW_H
#define RUNTIME_DISPLAY_WINDOW_H

/* The display's window back end: display_vulkan.c (Vulkan and SDL3) when the build found them,
   otherwise display_window_none.c, which has none. Called with the display's lock held. */

#include <stdint.h>

int rt_display_window_available(void);
/* Opens a window for frames of width x height pixels; 0 on success. The display opens it on the
   thread that shows the first frame, which the window system (SDL) then counts as its main one. */
int rt_display_window_open(uint32_t width, uint32_t height);
/* Shows a frame (BGRA, width x height) in the window. */
void rt_display_window_show(const uint32_t *pixels, uint32_t width, uint32_t height);
/* Shows status after the name in the window's title; an empty status: the name alone. The title
   changes only on the window system's main thread (SDL3 allows SDL_SetWindowTitle nowhere else):
   called on another thread it changes nothing and returns 0, so the caller keeps the status for a
   frame shown on that thread. Otherwise returns 1, also when the title could not change (told once
   on stderr). */
int rt_display_window_title(const char *status);

#endif
