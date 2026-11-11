/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_VIDEO_OUT_H
#define RUNTIME_VIDEO_OUT_H

/* Video out (runtime/syslib/video_out.c), for the runtime's other system libraries: the graphics
   driver's submit-and-flip shows a buffer the same way sceVideoOutSubmitFlip does, queued. */

#include <stddef.h>
#include <stdint.h>

/* Byte-backed ABI view: the native setter accepts unaligned writable storage.
   Format and tiling are opaque words here, without a host pixel-layout mapping. */
enum {
    RT_VIDEO_OUT_ATTRIBUTE_WORD_BYTES = sizeof(uint32_t),
    RT_VIDEO_OUT_ATTRIBUTE_RESERVED_BYTES = 16,
};
typedef struct {
    uint8_t format[RT_VIDEO_OUT_ATTRIBUTE_WORD_BYTES];
    uint8_t tiling[RT_VIDEO_OUT_ATTRIBUTE_WORD_BYTES];
    uint8_t aspect[RT_VIDEO_OUT_ATTRIBUTE_WORD_BYTES];
    uint8_t width[RT_VIDEO_OUT_ATTRIBUTE_WORD_BYTES];
    uint8_t height[RT_VIDEO_OUT_ATTRIBUTE_WORD_BYTES];
    uint8_t pixel_pitch[RT_VIDEO_OUT_ATTRIBUTE_WORD_BYTES];
    uint8_t reserved[RT_VIDEO_OUT_ATTRIBUTE_RESERVED_BYTES];
} rt_video_out_buffer_attribute;

#ifdef __cplusplus
#define RT_VIDEO_OUT_LAYOUT_ASSERT static_assert
#else
#define RT_VIDEO_OUT_LAYOUT_ASSERT _Static_assert
#endif
RT_VIDEO_OUT_LAYOUT_ASSERT(offsetof(rt_video_out_buffer_attribute, format) == 0x00, "format");
RT_VIDEO_OUT_LAYOUT_ASSERT(offsetof(rt_video_out_buffer_attribute, tiling) == 0x04, "tiling");
RT_VIDEO_OUT_LAYOUT_ASSERT(offsetof(rt_video_out_buffer_attribute, aspect) == 0x08, "aspect");
RT_VIDEO_OUT_LAYOUT_ASSERT(offsetof(rt_video_out_buffer_attribute, width) == 0x0c, "width");
RT_VIDEO_OUT_LAYOUT_ASSERT(offsetof(rt_video_out_buffer_attribute, height) == 0x10, "height");
RT_VIDEO_OUT_LAYOUT_ASSERT(offsetof(rt_video_out_buffer_attribute, pixel_pitch) == 0x14, "pitch");
RT_VIDEO_OUT_LAYOUT_ASSERT(offsetof(rt_video_out_buffer_attribute, reserved) == 0x18, "reserved");
RT_VIDEO_OUT_LAYOUT_ASSERT(sizeof(rt_video_out_buffer_attribute) == 0x28, "attribute size");
#undef RT_VIDEO_OUT_LAYOUT_ASSERT

#ifdef __cplusplus
extern "C" {
#endif

/* Flips a port to one of its registered buffers, as sceVideoOutSubmitFlip(handle, buffer, mode,
   argument) does; caller names the system function for a stop. Returns 0. */
int32_t rt_video_out_flip(const char *caller, int32_t handle, int32_t buffer, int32_t mode, int64_t argument);

/* The same flip, for the GPU's work: the buffer's frame is taken now, and the flip waits for its
   vertical blank and takes effect on a thread of its own, in order, so the caller goes on at once.
   Returns 0. */
int32_t rt_video_out_queue_flip(const char *caller, int32_t handle, int32_t buffer, int32_t mode, int64_t argument);

#ifdef __cplusplus
}
#endif

#endif
