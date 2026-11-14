/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PB_VOLUME_TILING_H
#define PB_VOLUME_TILING_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A single-mip, uncompressed GFX7 1D-thick volume in AMD's index-19 profile.
   Depth is spatial Z; it is not an array-layer count. */
enum { RT_VOLUME_1D_THICK = 19 };
typedef struct {
    unsigned element_bytes;
    uint32_t width, height, depth, pitch;
    uint32_t padded_height, padded_depth;
    uint64_t size_bytes;
} rt_volume_tiling;

/* Pitch is the descriptor's physical row pitch, in texels. The admitted thick
   profile aligns pitch/height to 8 and depth to 4. POW2_PAD is admitted only when
   all supplied dimensions and pitch are already powers of two; other padding
   and mip profiles remain unsupported. Failure leaves output unchanged. */
int rt_volume_layout(unsigned tiling_index, unsigned element_bytes, uint32_t pitch,
                     uint32_t width, uint32_t height, uint32_t depth, int pow2_pad,
                     rt_volume_tiling *output, char *error, size_t error_size);

/* Coordinates must be inside pitch x padded_height x padded_depth of a layout
   returned by rt_volume_layout. Padding coordinates are addressable too. */
uint64_t rt_volume_offset(const rt_volume_tiling *layout, uint32_t x, uint32_t y, uint32_t z);

/* Linear memory holds width*height*depth visible texels, X fastest. Tiled memory
   holds size_bytes. Buffers must not overlap. Tiling preserves allocation padding. */
void rt_volume_detile(const rt_volume_tiling *layout, const uint8_t *tiled, uint8_t *linear);
void rt_volume_tile(const rt_volume_tiling *layout, const uint8_t *linear, uint8_t *tiled);
#ifdef __cplusplus
}
#endif
#endif
