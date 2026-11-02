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
    RT_METAL_TIMED_OUT
} rt_metal_status;

/* Small native platform boundary: device, queue and one RGBA8Unorm offscreen texture. Current
   CPU-readable storage requires a unified-memory Metal device. Dimensions1..4096 are this
   primitive's own allocation bound, not a claim about device limits. *out must initially be NULL;
   failures leave it unchanged. Optional error buffers receive a bounded nul-terminated message.
   An error buffer must be disjoint from the context/handle, destination and color memory. */
rt_metal_status rt_metal_offscreen_create(uint32_t width, uint32_t height, rt_metal_offscreen **out,
                                         char *error, size_t error_size);

/* Submit an actual render-pass clear/store and wait for completion, bounded by1..30000ms.
   Timeout/device failure poisons the context: destroy it; no readback or reuse is then allowed.
   Operations on one context must be serialized by its owner. This does not present a window. */
rt_metal_status rt_metal_offscreen_clear(rt_metal_offscreen *context, const uint8_t rgba[4],
                                        uint32_t timeout_ms, char *error, size_t error_size);

/* Read only after a successful clear. Copy width*4 bytes per row, preserving all destination
   row padding. capacity need only cover the final pixel, not trailing padding. Invalid arguments
   and backend/state errors leave destination unchanged. Valid caller memory is required. */
rt_metal_status rt_metal_offscreen_read(rt_metal_offscreen *context, void *destination,
                                       size_t stride, size_t capacity, char *error, size_t error_size);

const char *rt_metal_offscreen_device_name(const rt_metal_offscreen *context);
/* NULL-safe and idempotent; clears the caller's handle. This function does not explicitly wait
   for GPU completion; committed command buffers retain references to their resources. */
void rt_metal_offscreen_destroy(rt_metal_offscreen **context);

#ifdef __cplusplus
}
#endif
#endif
