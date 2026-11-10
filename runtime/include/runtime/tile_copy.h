/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_TILE_COPY_H
#define RUNTIME_TILE_COPY_H

/* The renderer's GPU copy between a tiled image in guest memory and linear rows
   (runtime/renderer/tile_copy.c): the same placement as rt_tiling_detile and rt_tiling_tile
   (runtime/tiling.h), run as a compute shader so render targets and textures move between their
   Vulkan images and guest memory without the CPU touching the texels.

   Each invocation moves one element (a texel, or a block of a block-compressed image) of `dwords`
   32-bit words: element i of the linear rows (rows of `width` elements, `height` rows a slice, slice
   after slice) and the element at the tiled image's base plus table[64 + tile] + table[place], where
   tile = slice * tiles_per_slice + (y / 8) * tiles_per_row + x / 8 and place = (y % 8) * 8 + x % 8.
   The table holds 32-bit byte offsets: rt_tiling_micro_spread's 64 places, then the offset of every
   micro tile's first element (rt_tiling_offset). Addresses are GPU addresses (buffer device
   addresses), in the push constants as 32-bit words, low word first. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { RT_TILE_COPY_GROUP = 64 };

/* The push constants, one 32-bit word each. Workgroup (x, y) covers elements
   (y * groups_x + x) * 64 to that plus 63; elements past `elements` do nothing. */
enum {
    RT_TILE_COPY_TILED_LO, RT_TILE_COPY_TILED_HI, RT_TILE_COPY_LINEAR_LO, RT_TILE_COPY_LINEAR_HI, RT_TILE_COPY_TABLE_LO,
    RT_TILE_COPY_TABLE_HI, RT_TILE_COPY_WIDTH, RT_TILE_COPY_HEIGHT, RT_TILE_COPY_TILES_PER_ROW, RT_TILE_COPY_TILES_PER_SLICE,
    RT_TILE_COPY_ELEMENTS, RT_TILE_COPY_GROUPS_X, RT_TILE_COPY_PUSH_WORDS
};

/* The compute shader (SPIR-V 1.5) moving elements of `dwords` words (1, 2 or 4) to the tiled image
   (to_tiled) or from it. Returns 0 with a new array the caller frees, or -1. */
int rt_tile_copy_shader(unsigned dwords, int to_tiled, uint32_t **spirv, size_t *word_count);

#ifdef __cplusplus
}
#endif

#endif
