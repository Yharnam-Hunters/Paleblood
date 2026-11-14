/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_TILING_H
#define RUNTIME_TILING_H

/* Where an image's texels lie in guest memory (runtime/renderer/tiling.c): the PS4 GPU's (GFX7)
   tiled layouts, for the renderer to read images the CPU or a shader's buffer stores wrote
   (docs/RENDERER.md). An image descriptor names its layout by a tiling index, one of the GPU's 32
   tile modes; only the ones the game's images use are done, and the others are refused by name.

   Done: linear (indices 8 and 31), 1D-tiled thin (9 display, 13 thin) and 2D-tiled thin
   (10 display, 14 thin), for one sample and 1, 2, 4, 8 or 16 bytes per element. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { RT_TILING_LINEAR, RT_TILING_1D, RT_TILING_2D } rt_tiling_kind;
typedef enum { RT_MICRO_DISPLAY, RT_MICRO_THIN } rt_micro_order;

/* One image level's layout, worked out by rt_tiling_layout. */
typedef struct {
    unsigned tiling_index;
    rt_tiling_kind kind;
    rt_micro_order micro;            /* the order of texels in an 8x8 micro tile (tiled kinds) */
    unsigned element_bytes;
    uint32_t pitch, height;          /* texels per row and rows, padded as the layout lays them out */
    /* 2D-tiled only: the channels (pipes and banks) a macro tile spreads over */
    unsigned pipes, banks, bank_width, bank_height, macro_aspect;
    uint32_t macro_width, macro_height;
    uint32_t tile_split;             /* 2D depth modes: a micro tile's bytes past this go to further slices (0: none) */
    uint64_t slice_bytes;            /* the bytes of one slice (array layer) */
} rt_tiling;

/* Whether each micro tile of the layout keeps its 64 elements at one spread of offsets from its first
   (rt_tiling_micro_spread): every layout but a split depth tile's, whose pieces change bank. */
int rt_tiling_uniform_micro(const rt_tiling *t);

/* The layout of an image with this tiling index and element size whose rows are pitch texels apart
   (the descriptor's PITCH) and height rows high. 0, or -1 with the reason: a tiling index or element
   size not done yet, or a pitch the layout cannot have. */
int rt_tiling_layout(unsigned tiling_index, unsigned element_bytes, uint32_t pitch, uint32_t height, rt_tiling *layout, char *error,
                     size_t error_size);

/* A mip-mapped image's levels (runtime/renderer/tiling.c, rt_tiling_mip_chain): each level's own
   layout, its offset from the image's base and its element counts. */
enum { RT_TILING_MAX_LEVELS = 16 };
typedef struct {
    rt_tiling layout;
    uint64_t offset;                 /* from the image's base: the levels follow one another */
    uint32_t columns, rows;          /* the level's elements (texels, or 4x4 blocks) across and down */
} rt_tiling_level;

/* The levels 0 to levels - 1 of an image with this tiling index whose level 0 is pitch x height texels
   (the descriptor's PITCH and HEIGHT; `block` 4 for block-compressed elements of 4x4 texels, else 1),
   with `pow2` its POW2_PAD and `slices` slices per level. 0, or -1 with the reason. */
int rt_tiling_mip_chain(unsigned tiling_index, unsigned element_bytes, unsigned block, int pow2, uint32_t pitch, uint32_t height,
                        uint32_t width, uint32_t visible_height, unsigned levels, uint32_t slices, rt_tiling_level *out, char *error,
                        size_t error_size);

/* The byte offset from the image's base of texel (x, y) of slice `slice`. */
uint64_t rt_tiling_offset(const rt_tiling *layout, uint32_t x, uint32_t y, uint32_t slice);

enum { RT_TILING_MICRO = 8, RT_TILING_MICRO_TEXELS = RT_TILING_MICRO * RT_TILING_MICRO };

/* Where each texel of a micro tile (8x8 texels, x fastest) lies from the micro tile's first texel, in
   bytes: texel (x, y) is at rt_tiling_offset of (x & ~7, y & ~7) plus spread[(y & 7) * 8 + (x & 7)].
   Every layout here, linear included, keeps that form, so copies can go a micro tile at a time (the
   renderer's GPU copies take this table). */
void rt_tiling_micro_spread(const rt_tiling *layout, uint64_t spread[RT_TILING_MICRO_TEXELS]);

/* The bytes from the image's base the layout spans for `slices` slices. */
uint64_t rt_tiling_size(const rt_tiling *layout, uint32_t slices);

/* Copies texels 0 .. width - 1, 0 .. height - 1 of each of `slices` slices from the tiled image at
   `tiled` (rt_tiling_size bytes) into `linear`, rows of width texels one after another, slice after
   slice. */
void rt_tiling_detile(const rt_tiling *layout, const uint8_t *tiled, uint8_t *linear, uint32_t width, uint32_t height, uint32_t slices);

/* The other way: texels from `linear` (rows of width texels, slice after slice) to their places in the
   tiled image at `tiled`; the layout's padding is left as it is. */
void rt_tiling_tile(const rt_tiling *layout, const uint8_t *linear, uint8_t *tiled, uint32_t width, uint32_t height, uint32_t slices);

#ifdef __cplusplus
}
#endif

#endif
