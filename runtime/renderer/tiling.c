/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The PS4 GPU's (GFX7) tiled image layouts (runtime/tiling.h).

   Facts used, from public sources (our own code; none of theirs is copied):
   - The addressing formulas are AMD's address library ("addrlib", MIT licensed, in Mesa as
     src/amd/addrlib: EgBasedLib's ComputeSurfaceAddrFromCoordMicroTiled and ...MacroTiled,
     ComputePixelIndexWithinMicroTile, ComputePipeFromCoord and ComputeBankFromCoord, and
     CiLib for GFX7), which shadPS4's video_core/host_shaders/tiling.comp also follows.
   - A tiling index names one of the GPU's 32 tile modes (the GB_TILE_MODEn registers, which the
     console sets once). Their values on the PS4, as shadPS4's video_core/amdgpu/tiling.cpp lists
     them (TileMode, GetArrayMode, GetMicroTileMode, GetPipeConfig, GetSampleSplit):
       8 DisplayLinearAligned, 31 DisplayLinearGeneral: linear;
       9 Display1DThin, 13 Thin1DThin: 1D-tiled thin, display or thin micro tiles;
       10 Display2DThin, 14 Thin2DThin: 2D-tiled thin, pipe configuration P8_32x32_16x16 (8 pipes),
          sample split 2;
       0 Depth2DThin64: 2D-tiled thin, depth micro tiles (the thin order), P8_32x32_16x16, tile split
          64 bytes: a micro tile's bytes past 64 go to further slices, each piece's bank rotated by
          (banks / 2 + 1) x its piece number, and the macro tile mode follows the split (addrlib's
          ComputeSurfaceAddrFromCoordMacroTiled and ComputeBankFromCoord with tileSplitSlice, as
          shadPS4's tiling.comp has them; its tiling.cpp's GetTileSplitHw and CalculateTileSplit).
   - The macro tile modes (GB_MACROTILE_MODEn) the 2D modes pick from, the same file's
     GetBankWidth, GetBankHeight, GetNumBanks and GetMacrotileAspect: mode 0 1x4_16 (bank width 1,
     bank height 4, 16 banks, aspect 4), 1 1x2_16 (1, 2, 16, 2), 2 1x1_16 (1, 1, 16, 2),
     3 1x1_16_Dup (1, 1, 16, 2), 4 1x1_8 (1, 1, 8, 1). A colour image's mode is log2(tile bytes /
     64), with tile bytes the smaller of its micro tile (64 texels) and the tile split, max(256,
     sample split x micro tile) capped at the 1 KiB DRAM row (CalculateMacrotileMode); for one
     sample that is log2 of the element size.
   - The pipe interleave is 256 bytes (shadPS4's hw_pipe_interleave, the GPU's GB_ADDR_CONFIG). The
     base PS4 has no bank swizzle in its image descriptors (the alternate tile mode is the PS4 Pro's).
   - Cross-check: shadPS4's texture_cache/tile.h tabulates each mode's macro tile size; ours agree
     for the modes done here (test/test_tiling.c). */
#include "runtime/tiling.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { MICRO = 8, MICRO_TEXELS = MICRO * MICRO, PIPE_INTERLEAVE_BITS = 8, PIPE_INTERLEAVE = 1 << PIPE_INTERLEAVE_BITS };
enum { DEPTH_2D_THIN_64 = 0, DEPTH_TILE_SPLIT = 64, DISPLAY_LINEAR_ALIGNED = 8, DISPLAY_1D_THIN = 9, DISPLAY_2D_THIN = 10, THIN_1D_THIN = 13, THIN_2D_THIN = 14,
       DISPLAY_LINEAR_GENERAL = 31 };
enum { P8_PIPES = 8 };

static int failed(char *error, size_t error_size, const char *format, ...) __attribute__((format(printf, 3, 4)));
static int failed(char *error, size_t error_size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
    return -1;
}

static unsigned log2_of(uint32_t power_of_two)
{
    unsigned n = 0;
    while (power_of_two >>= 1) n++;
    return n;
}

static uint32_t bit(uint32_t value, unsigned n) { return value >> n & 1; }

int rt_tiling_layout(unsigned tiling_index, unsigned element_bytes, uint32_t pitch, uint32_t height, rt_tiling *layout, char *error,
                     size_t error_size)
{
    if (element_bytes != 1 && element_bytes != 2 && element_bytes != 4 && element_bytes != 8 && element_bytes != 16)
        return failed(error, error_size, "a tiled image of %u bytes per element: only 1, 2, 4, 8 and 16 are done", element_bytes);
    if (!pitch || !height) return failed(error, error_size, "an image of pitch %u and height %u", pitch, height);
    rt_tiling t = {.tiling_index = tiling_index, .element_bytes = element_bytes, .pitch = pitch, .height = height};
    switch (tiling_index) {
    case DISPLAY_LINEAR_ALIGNED: case DISPLAY_LINEAR_GENERAL: t.kind = RT_TILING_LINEAR; break;
    case DISPLAY_1D_THIN: t.kind = RT_TILING_1D, t.micro = RT_MICRO_DISPLAY; break;
    case THIN_1D_THIN: t.kind = RT_TILING_1D, t.micro = RT_MICRO_THIN; break;
    case DISPLAY_2D_THIN: t.kind = RT_TILING_2D, t.micro = RT_MICRO_DISPLAY; break;
    case THIN_2D_THIN: t.kind = RT_TILING_2D, t.micro = RT_MICRO_THIN; break;
    case DEPTH_2D_THIN_64: t.kind = RT_TILING_2D, t.micro = RT_MICRO_THIN, t.tile_split = DEPTH_TILE_SPLIT; break;
    default: return failed(error, error_size, "tiling index %u: not done yet", tiling_index);
    }
    if (t.kind == RT_TILING_1D) {
        if (pitch % MICRO) return failed(error, error_size, "a 1D-tiled image of pitch %u, not a whole number of micro tiles", pitch);
        t.height = (height + MICRO - 1) / MICRO * MICRO;
    }
    if (t.kind == RT_TILING_2D) {
        static const struct { unsigned bank_height, banks, aspect; } macro_modes[] = {{4, 16, 4}, {2, 16, 2}, {1, 16, 2}, {1, 16, 2}, {1, 8, 1}};
        /* the macro tile mode: log2(tile bytes / 64), the tile bytes the micro tile's or the split's */
        const unsigned micro_bytes = MICRO_TEXELS * element_bytes;
        const unsigned tile_bytes = t.tile_split && t.tile_split < micro_bytes ? t.tile_split : micro_bytes > 1024 ? 1024 : micro_bytes;
        const unsigned mode = log2_of(tile_bytes / 64);
        if (t.tile_split >= micro_bytes) t.tile_split = 0; /* nothing to split */
        t.pipes = P8_PIPES, t.bank_width = 1, t.bank_height = macro_modes[mode].bank_height, t.banks = macro_modes[mode].banks;
        t.macro_aspect = macro_modes[mode].aspect;
        t.macro_width = MICRO * t.bank_width * t.pipes * t.macro_aspect;
        t.macro_height = MICRO * t.bank_height * t.banks / t.macro_aspect;
        if (pitch % t.macro_width)
            return failed(error, error_size, "a 2D-tiled image of pitch %u, not a whole number of %u-texel macro tiles", pitch, t.macro_width);
        t.height = (height + t.macro_height - 1) / t.macro_height * t.macro_height;
    }
    t.slice_bytes = (uint64_t)t.pitch * t.height * element_bytes;
    *layout = t;
    return 0;
}

/* The texel's place (0-63) in its 8x8 micro tile: which coordinate bit each of the index's six bits
   takes, x0-x2 as 0-2 and y0-y2 as 3-5 (addrlib's ComputePixelIndexWithinMicroTile). The display
   order depends on the element size; the thin (and depth) order does not. */
static unsigned micro_index(const rt_tiling *t, uint32_t x, uint32_t y)
{
    enum { X0, X1, X2, Y0, Y1, Y2 };
    static const unsigned char thin[6] = {X0, Y0, X1, Y1, X2, Y2};
    static const unsigned char display[5][6] = {
        {X0, X1, X2, Y1, Y0, Y2}, /* 1 byte */
        {X0, X1, X2, Y0, Y1, Y2}, /* 2 bytes */
        {X0, X1, Y0, X2, Y1, Y2}, /* 4 bytes */
        {X0, Y0, X1, X2, Y1, Y2}, /* 8 bytes */
        {X0, Y0, X1, Y1, X2, Y2}, /* 16 bytes */
    };
    const unsigned char *order = t->micro == RT_MICRO_THIN ? thin : display[log2_of(t->element_bytes)];
    const uint32_t coordinate_bits = (x & 7) | (y & 7) << 3;
    unsigned index = 0;
    for (unsigned b = 0; b < 6; b++) index |= bit(coordinate_bits, order[b]) << b;
    return index;
}

/* P8_32x32_16x16 (ComputePipeFromCoord), on the pixel coordinates' bits 3-5. */
static unsigned pipe_of(uint32_t x, uint32_t y)
{
    const unsigned p0 = bit(x, 3) ^ bit(y, 3) ^ bit(x, 4), p1 = bit(x, 4) ^ bit(y, 4), p2 = bit(x, 5) ^ bit(y, 5);
    return p0 | p1 << 1 | p2 << 2;
}

/* ComputeBankFromCoord, with no bank swizzle: on the macro-tile column (in bank-width units of all the
   pipes) and row (in bank-height units), then rotated per slice for the 2D thin modes. */
static unsigned bank_of(const rt_tiling *t, uint32_t x, uint32_t y, uint32_t slice)
{
    const uint32_t tx = x / MICRO / (t->bank_width * t->pipes), ty = y / MICRO / t->bank_height;
    unsigned bank;
    if (t->banks == 16)
        bank = (bit(tx, 0) ^ bit(ty, 3)) | (bit(tx, 1) ^ bit(ty, 2) ^ bit(ty, 3)) << 1 | (bit(tx, 2) ^ bit(ty, 1)) << 2 |
               (bit(tx, 3) ^ bit(ty, 0)) << 3;
    else /* 8 */
        bank = (bit(tx, 0) ^ bit(ty, 2)) | (bit(tx, 1) ^ bit(ty, 1) ^ bit(ty, 2)) << 1 | (bit(tx, 2) ^ bit(ty, 0)) << 2;
    const uint32_t slice_rotation = (t->banks / 2 - 1) * slice;
    return (bank ^ slice_rotation) & (t->banks - 1);
}

uint64_t rt_tiling_offset(const rt_tiling *t, uint32_t x, uint32_t y, uint32_t slice)
{
    const uint64_t element = t->element_bytes;
    if (t->kind == RT_TILING_LINEAR) return (((uint64_t)slice * t->height + y) * t->pitch + x) * element;
    const uint64_t micro_bytes = MICRO_TEXELS * element;
    const uint64_t in_micro = micro_index(t, x, y) * element;
    if (t->kind == RT_TILING_1D)
        return slice * t->slice_bytes + ((uint64_t)(y / MICRO) * (t->pitch / MICRO) + x / MICRO) * micro_bytes + in_micro;
    /* 2D: an offset within one channel (a pipe and bank), whose low 8 bits stay in place, with the pipe
       and the bank above them */
    const unsigned channels = t->pipes * t->banks, pipe_bits = log2_of(t->pipes), bank_bits = log2_of(t->banks);
    /* a split micro tile: its piece (in_micro / split) lies in a further slice, its bank rotated */
    const uint64_t piece_bytes = t->tile_split ? t->tile_split : micro_bytes, pieces = micro_bytes / piece_bytes;
    const uint64_t piece = in_micro / piece_bytes, in_piece = in_micro % piece_bytes;
    const uint64_t macro_bytes = piece_bytes * (t->macro_width / MICRO) * (t->macro_height / MICRO) / channels;
    const uint64_t macros_per_row = t->pitch / t->macro_width;
    const uint64_t slice_channel_bytes = macros_per_row * (t->height / t->macro_height) * macro_bytes;
    const uint64_t macro = (uint64_t)(y / t->macro_height) * macros_per_row + x / t->macro_width;
    const uint64_t tile = (uint64_t)(y / MICRO % t->bank_height) * t->bank_width + (x / MICRO / t->pipes) % t->bank_width;
    const uint64_t in_channel = (piece + pieces * slice) * slice_channel_bytes + macro * macro_bytes + tile * piece_bytes + in_piece;
    const unsigned bank = (bank_of(t, x, y, slice) ^ (unsigned)((t->banks / 2 + 1) * piece)) & (t->banks - 1);
    return (in_channel & (PIPE_INTERLEAVE - 1)) | (uint64_t)pipe_of(x, y) << PIPE_INTERLEAVE_BITS |
           (uint64_t)bank << (PIPE_INTERLEAVE_BITS + pipe_bits) |
           (in_channel >> PIPE_INTERLEAVE_BITS) << (PIPE_INTERLEAVE_BITS + pipe_bits + bank_bits);
}

uint64_t rt_tiling_size(const rt_tiling *t, uint32_t slices) { return slices * t->slice_bytes; }

static uint32_t next_power_of_two(uint32_t v)
{
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

static uint32_t align_up(uint32_t v, uint32_t to) { return (v + to - 1) / to * to; }

/* Mip chains, as AMD's address library lays them out for GFX7 and shadPS4's texture_cache/tile.h
   (ComputeImageSize) follows: level m spans (pitch >> m) x (height >> m) texels, in 4x4 blocks for
   block-compressed elements (rounded up), at least 1, each rounded up to a power of two with
   POW2_PAD; the levels lie one after another from the base, each level's slices together. A level
   keeps a 2D-tiled image's macro tiling while it spans at least one macro tile (level 0 always);
   a smaller level, and every level of a 1D-tiled image, is micro tiled: its pitch and height
   rounded up to 8 elements, the pitch grown by 8 until a slice is a whole number of 256 bytes. A
   linear level's pitch is rounded up to max(8, 64 / element bytes) elements and grown by that until
   a slice's elements are a multiple of max(64, 256 / element bytes). The 1D layout a 2D image's
   small levels use keeps its micro tile order (9 for display, 13 for thin). */
int rt_tiling_mip_chain(unsigned tiling_index, unsigned element_bytes, unsigned block, int pow2, uint32_t pitch, uint32_t height,
                        uint32_t width, uint32_t visible_height, unsigned levels, uint32_t slices, rt_tiling_level *out, char *error,
                        size_t error_size)
{
    if (!levels || levels > RT_TILING_MAX_LEVELS) return failed(error, error_size, "an image of %u levels", levels);
    if (block != 1 && block != 4) return failed(error, error_size, "elements of %ux%u texels", block, block);
    /* the mode's kind, micro tile order and macro tile size (a pitch every mode takes) */
    enum { ANY_PITCH = 4096 };
    rt_tiling top;
    if (rt_tiling_layout(tiling_index, element_bytes, ANY_PITCH, MICRO, &top, error, error_size)) return -1;
    top.pitch = pitch / block;
    uint64_t offset = 0;
    for (unsigned m = 0; m < levels; m++) {
        uint32_t w = pitch >> m, h = height >> m;
        if (block > 1) w = (w + block - 1) / block, h = (h + block - 1) / block;
        if (!w) w = 1;
        if (!h) h = 1;
        if (pow2) w = next_power_of_two(w), h = next_power_of_two(h);
        rt_tiling_level *const level = &out[m];
        uint32_t level_pitch, level_height;
        unsigned index = tiling_index;
        if (top.kind == RT_TILING_LINEAR) {
            const uint32_t pitch_align = element_bytes >= 8 ? MICRO : 64 / element_bytes;
            const uint32_t slice_align = element_bytes >= 4 ? 64 : 256 / element_bytes;
            level_pitch = align_up(w, pitch_align), level_height = h;
            while ((uint64_t)level_pitch * level_height % slice_align) level_pitch += pitch_align;
        } else if (top.kind == RT_TILING_2D && (m == 0 || (w >= top.macro_width && h >= top.macro_height))) {
            level_pitch = m == 0 ? top.pitch : align_up(w, top.macro_width), level_height = h;
        } else {
            index = top.micro == RT_MICRO_THIN ? THIN_1D_THIN : DISPLAY_1D_THIN;
            level_pitch = align_up(w, MICRO), level_height = align_up(h, MICRO);
            while ((uint64_t)level_pitch * level_height * element_bytes % PIPE_INTERLEAVE) level_pitch += MICRO;
        }
        if (m == 0 && top.kind != RT_TILING_2D) level_pitch = top.pitch > level_pitch ? top.pitch : level_pitch;
        if (rt_tiling_layout(index, element_bytes, level_pitch, level_height, &level->layout, error, error_size)) return -1;
        level->offset = offset;
        const uint32_t visible_w = width >> m, visible_h = visible_height >> m;
        level->columns = ((visible_w ? visible_w : 1) + block - 1) / block;
        level->rows = ((visible_h ? visible_h : 1) + block - 1) / block;
        offset += rt_tiling_size(&level->layout, slices);
    }
    return 0;
}

/* Within a micro tile, a texel's place is the micro tile's first texel's plus its element offset
   (micro_index x element size), the part past the 256-byte pipe interleave moving up to the channel
   rows of a 2D layout. That holds because a micro tile starts on a 256-byte boundary of its channel
   (2D: its offset there is a whole number of micro tiles of 256 bytes or more, or of ones that divide
   256 and so never cross it) and keeps one pipe and bank throughout (they depend on coordinate bits 3
   and up). A linear layout's rows are pitch texels apart. test/test_tiling.c checks it against
   rt_tiling_offset for every mode and element size. */
void rt_tiling_micro_spread(const rt_tiling *t, uint64_t spread[RT_TILING_MICRO_TEXELS])
{
    const uint64_t element = t->element_bytes;
    const unsigned channel_shift = t->kind == RT_TILING_2D ? PIPE_INTERLEAVE_BITS + log2_of(t->pipes) + log2_of(t->banks) : PIPE_INTERLEAVE_BITS;
    for (uint32_t j = 0; j < MICRO; j++)
        for (uint32_t i = 0; i < MICRO; i++) {
            if (t->kind == RT_TILING_LINEAR) {
                spread[j * MICRO + i] = ((uint64_t)j * t->pitch + i) * element;
                continue;
            }
            const uint64_t m = micro_index(t, i, j) * element;
            spread[j * MICRO + i] = t->kind == RT_TILING_2D ? (m & (PIPE_INTERLEAVE - 1)) + ((m >> PIPE_INTERLEAVE_BITS) << channel_shift) : m;
        }
}

int rt_tiling_uniform_micro(const rt_tiling *t) { return !t->tile_split; }

/* Copies between the tiled image and linear rows, a micro tile at a time (rt_tiling_micro_spread), or
   texel by texel for a split depth tile. */
static void copy_texels(const rt_tiling *t, uint8_t *tiled, uint8_t *linear, uint32_t width, uint32_t height, uint32_t slices, int to_tiled)
{
    const size_t element = t->element_bytes, row = (size_t)width * element;
    if (!rt_tiling_uniform_micro(t)) {
        for (uint32_t s = 0; s < slices; s++)
            for (uint32_t y = 0; y < height; y++)
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t *at = tiled + rt_tiling_offset(t, x, y, s), *texel = linear + ((size_t)s * height + y) * row + x * element;
                    if (to_tiled) memcpy(at, texel, element);
                    else memcpy(texel, at, element);
                }
        return;
    }
    if (t->kind == RT_TILING_LINEAR) {
        for (uint32_t s = 0; s < slices; s++)
            for (uint32_t y = 0; y < height; y++, linear += row) {
                uint8_t *at = tiled + rt_tiling_offset(t, 0, y, s);
                if (to_tiled) memcpy(at, linear, row);
                else memcpy(linear, at, row);
            }
        return;
    }
    uint64_t spread[MICRO_TEXELS];
    rt_tiling_micro_spread(t, spread);
    for (uint32_t s = 0; s < slices; s++, linear += row * height)
        for (uint32_t ty = 0; ty < height; ty += MICRO)
            for (uint32_t tx = 0; tx < width; tx += MICRO) {
                uint8_t *const tile = tiled + rt_tiling_offset(t, tx, ty, s);
                for (uint32_t j = 0; j < MICRO && ty + j < height; j++)
                    for (uint32_t i = 0; i < MICRO && tx + i < width; i++) {
                        uint8_t *at = tile + spread[j * MICRO + i], *texel = linear + (ty + j) * row + (tx + i) * element;
                        if (to_tiled) memcpy(at, texel, element);
                        else memcpy(texel, at, element);
                    }
            }
}

void rt_tiling_detile(const rt_tiling *t, const uint8_t *tiled, uint8_t *linear, uint32_t width, uint32_t height, uint32_t slices)
{
    copy_texels(t, (uint8_t *)(uintptr_t)tiled, linear, width, height, slices, 0);
}

void rt_tiling_tile(const rt_tiling *t, const uint8_t *linear, uint8_t *tiled, uint32_t width, uint32_t height, uint32_t slices)
{
    copy_texels(t, tiled, (uint8_t *)(uintptr_t)linear, width, height, slices, 1);
}
