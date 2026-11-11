/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_TILE_COPY_H
#define RUNTIME_TILE_COPY_H

/* The renderer's GPU copy between a tiled image in guest memory and linear rows
   (runtime/renderer/tile_copy.c): the same placement as rt_tiling_detile and rt_tiling_tile
   (runtime/tiling.h), run as a compute shader so render targets and textures move between their
   Vulkan images and guest memory without the CPU touching the texels.

   Each invocation moves one element (a texel, or a block of a block-compressed image) of `dwords`
   32-bit words. Invocation i takes element k = i % 64, in memory order, of micro tile i / 64 (micro
   tiles counted along each row of them, row after row, slice after slice), so neighbouring lanes
   touch neighbouring guest memory. The table holds 32-bit words: the 64 elements' byte offsets in
   their micro tile in increasing order (rt_tiling_micro_spread's values, sorted), the texel place
   ((y % 8) * 8 + x % 8) of each, then each micro tile's first element's offset from the image's base
   (rt_tiling_offset). A depth layout with a tile split (runtime/tiling.h: a micro tile's bytes past the
   split lie in further pieces, each placed on its own) gives each micro tile 2^pieces_log2 first
   offsets, one per piece, in order, and the 64 offsets are then each element's offset in its piece:
   element k is in piece k >> (6 - pieces_log2). Elements past the image's width or height are skipped. Each element moves in
   one access of its whole size, so the tiled image's address must be a multiple of the element's
   bytes (a guest image's base always is: descriptors hold it in units of 256 bytes). The linear rows hold
   `width` elements a row, `height` rows a slice, slice after slice. Addresses are GPU addresses
   (buffer device addresses), in the push constants as 32-bit words, low word first. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { RT_TILE_COPY_GROUP = 64 };

/* The table's parts (word indices): offsets in a micro tile, texel places, micro tiles' offsets. */
enum { RT_TILE_COPY_OFFSETS = 0, RT_TILE_COPY_PLACES = 64, RT_TILE_COPY_TILE_BASES = 128 };

/* The push constants, one 32-bit word each. Workgroup (x, y) covers invocations
   (y * groups_x + x) * 64 to that plus 63, one micro tile; those past `tiles` micro tiles do nothing. */
enum {
    RT_TILE_COPY_TILED_LO, RT_TILE_COPY_TILED_HI, RT_TILE_COPY_LINEAR_LO, RT_TILE_COPY_LINEAR_HI, RT_TILE_COPY_TABLE_LO,
    RT_TILE_COPY_TABLE_HI, RT_TILE_COPY_WIDTH, RT_TILE_COPY_HEIGHT, RT_TILE_COPY_TILES_PER_ROW, RT_TILE_COPY_TILES_PER_SLICE,
    RT_TILE_COPY_TILES, RT_TILE_COPY_GROUPS_X, RT_TILE_COPY_PIECES_LOG2, RT_TILE_COPY_PUSH_WORDS
};

/* The compute shader (SPIR-V 1.5) moving elements of `dwords` words (1, 2 or 4) to the tiled image
   (to_tiled) or from it. Returns 0 with a new array the caller frees, or -1. */
int rt_tile_copy_shader(unsigned dwords, int to_tiled, uint32_t **spirv, size_t *word_count);

#ifdef __cplusplus
}
#endif

#endif
