/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GRAPHICS_METAL_H
#define RUNTIME_GRAPHICS_METAL_H

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_metal_offscreen rt_metal_offscreen;
typedef enum {
    RT_METAL_OK,
    RT_METAL_INVALID_ARGUMENT,
    RT_METAL_UNAVAILABLE,
    RT_METAL_ALLOCATION_FAILED,
    RT_METAL_NOT_READY,
    RT_METAL_GPU_FAILED,
    RT_METAL_TIMED_OUT,
    RT_METAL_NO_DRAWABLE
} rt_metal_status;

/* Small native platform boundary: device, queue and one RGBA8Unorm offscreen texture. Current
   CPU-readable storage requires a unified-memory Metal device. Dimensions 1..4096 are this
   primitive's own allocation bound, not a claim about device limits. *out must initially be NULL;
   failures leave it unchanged. Optional error buffers receive a bounded nul-terminated message.
   An error buffer must be disjoint from the context/handle, destination, source and color memory. */
rt_metal_status rt_metal_offscreen_create(uint32_t width, uint32_t height, rt_metal_offscreen **out,
                                         char *error, size_t error_size);

/* Submit an actual render-pass clear/store and wait for completion, bounded by 1..30000 ms.
   Timeout/device failure poisons the context: destroy it; no readback or reuse is then allowed.
   Operations on one context must be serialized by its owner. This does not present a window. */
rt_metal_status rt_metal_offscreen_clear(rt_metal_offscreen *context, const uint8_t rgba[4],
                                        uint32_t timeout_ms, char *error, size_t error_size);

/* Copy a complete RGBA8 frame into the texture, synchronously, with no color conversion or
   vertical flip. Source row zero becomes texture row zero. stride is in bytes and may include
   arbitrary padding; capacity need only cover the final pixel. No source pointer is retained.
   Invalid arguments leave the prior texture/readiness unchanged. A backend error poisons the
   context. The source must be valid caller memory disjoint from the context and error buffer.
   Like clear/read, upload must be serialized by the context owner. */
rt_metal_status rt_metal_offscreen_upload(rt_metal_offscreen *context, const void *source,
                                         size_t stride, size_t capacity, char *error, size_t error_size);

/* Read only after a successful clear or upload. Copy width*4 bytes per row, preserving all destination
   row padding. capacity need only cover the final pixel, not trailing padding. Invalid arguments
   and rejected context states leave destination unchanged. A Metal readback exception poisons
   the context and may partially write a packed destination. Valid caller memory is required. */
rt_metal_status rt_metal_offscreen_read(rt_metal_offscreen *context, void *destination,
                                       size_t stride, size_t capacity, char *error, size_t error_size);

/* Optional diagnostic capture of the actual drawable, before it is presented. Bytes are
   BGRA8Unorm in texture row order, without color-space conversion. capacity may end at the
   final pixel. Invalid arguments/backend errors leave data unchanged. Valid data and descriptor
   memory disjoint from the context, native layer and error buffer is required. */
typedef struct {
    void *data;
    size_t stride;
    size_t capacity;
} rt_metal_bgra_readback;

/* native_layer is a borrowed, valid CAMetalLayer Objective-C object bridged to void*. Main
   thread only. Set its device, BGRA8Unorm format, drawable size equal to this context, finite
   drawable acquisition and non-transactional presentation. Bounds, contentsScale, attachment,
   framebufferOnly, window and application policy remain with the caller. Configuration is not
   restored on destroy; a backend exception may leave it partial and poisons the context. */
rt_metal_status rt_metal_offscreen_configure_layer(rt_metal_offscreen *context, void *native_layer,
                                                  char *error, size_t error_size);

/* Render the completed source to a configured layer and schedule presentation. Main thread
   only; serialize context operations AND all access/mutation of this layer. The layer/view is
   caller-owned. Profile changes are rejected before acquisition. No source/destination scaling,
   flip, channel swap or alpha blending in this render pass; Core Animation can scale to bounds.
   A non-NULL capture requires framebufferOnly=NO; NULL avoids readback allocation/copy.
   timeout_ms (1..30000) bounds the GPU completion wait AFTER commit, not pipeline compilation or
   nextDrawable (which has its own system timeout). OK proves GPU completion and scheduling,
   not on-screen presentation; a caller's drawable presented handler/time can observe that.
   NO_DRAWABLE preserves the context. GPU failure/timeout poisons it; submitted work is not
   cancelled and may present later. No callback retains caller output pointers or the context. */
rt_metal_status rt_metal_offscreen_present(rt_metal_offscreen *context, void *native_layer,
                                          const rt_metal_bgra_readback *capture, uint32_t timeout_ms,
                                          char *error, size_t error_size);

const char *rt_metal_offscreen_device_name(const rt_metal_offscreen *context);
/* NULL-safe and idempotent; clears the caller's handle. This function does not explicitly wait
   for GPU completion; committed command buffers retain references to their resources. */
void rt_metal_offscreen_destroy(rt_metal_offscreen **context);

#ifdef __cplusplus
}
#endif
#endif
