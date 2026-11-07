/* SPDX-License-Identifier: GPL-2.0-or-later */
/* No window back end in this build (Vulkan or SDL3 was not found): --display window is refused. */
#include "runtime/display_window.h"
#include "runtime/process.h"

int rt_display_window_available(void) { return 0; }

int rt_display_window_open(uint32_t width, uint32_t height)
{
    (void)width, (void)height;
    return -1;
}

void rt_display_window_show(const uint32_t *pixels, uint32_t width, uint32_t height)
{
    (void)pixels, (void)width, (void)height;
    rt_fatal("rt_display_window_show", "this build has no window back end");
}
