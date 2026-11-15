/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_DISPLAY_WINDOW_H
#define RUNTIME_DISPLAY_WINDOW_H

/* The display's window backend: AppKit/Metal on selected macOS builds, Vulkan/SDL3 when
   available elsewhere, or none. Open/show are called with the display's lock held. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int rt_display_window_available(void);
/* Opens a window for frames of width x height pixels; 0 on success. */
int rt_display_window_open(uint32_t width, uint32_t height);
/* Shows a frame (BGRA, width x height) in the window. */
void rt_display_window_show(const uint32_t *pixels, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
#endif
