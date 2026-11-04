/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_VIDEO_OUT_H
#define RUNTIME_VIDEO_OUT_H

/* Video out (runtime/syslib/video_out.c), for the runtime's other system libraries: the graphics
   driver's submit-and-flip shows a buffer the same way sceVideoOutSubmitFlip does. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flips a port to one of its registered buffers, as sceVideoOutSubmitFlip(handle, buffer, mode,
   argument) does; caller names the system function for a stop. Returns 0. */
int32_t rt_video_out_flip(const char *caller, int32_t handle, int32_t buffer, int32_t mode, int64_t argument);

#ifdef __cplusplus
}
#endif

#endif
