/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_DISPLAY_H
#define RUNTIME_DISPLAY_H

/* The host display (runtime/display/): what the player sees. Video out (runtime/syslib/video_out.c)
   attaches each port's frame buffers here and presents one at each flip. Every guest frame buffer
   has a host frame, BGRA, 8 bits a channel, which the renderer (docs/RENDERER.md) draws into; the
   guest's own buffer memory is never read, since the PS4's GPU would have drawn it.

   Modes, chosen with pbboot --display:
     none      no frames are kept and nothing is shown (the default, so boots are unchanged);
     headless  frames are kept and counted; with --display-capture DIR each presented frame is
               also written there as frame-NNNNNN.ppm (by a writer thread, off the flip), for tests and
               for looking at a run;
     window    frames are shown in a window, with Vulkan (when the runtime was built with Vulkan
               and SDL3). */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { RT_DISPLAY_NONE, RT_DISPLAY_HEADLESS, RT_DISPLAY_WINDOW } rt_display_mode;

/* Chooses the mode before the guest runs; capture_dir may be NULL. Returns 0, or -1 when the mode
   is not available in this build (window without Vulkan and SDL3). */
int rt_display_configure(rt_display_mode mode, const char *capture_dir);

/* Video out registered count frame buffers of width x height pixels on a port. */
void rt_display_attach(int port, uint32_t width, uint32_t height, int count);

/* The host frame behind one of a port's buffers, width x height BGRA pixels, or NULL in mode none. */
uint32_t *rt_display_frame(int port, int buffer);

/* Shows one of a port's buffers: video out calls it when a flip takes effect. */
void rt_display_present(int port, int buffer);

/* How many frames a port has presented. */
uint64_t rt_display_presented(int port);

/* Waits until every captured frame so far is written (frames are written by a thread of the
   display's own, so a flip never waits on a file). */
void rt_display_capture_wait(void);

#ifdef __cplusplus
}
#endif

#endif
