/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GPU's tiled image layouts (runtime/renderer/tiling.c). Each expected offset below is worked out
   by hand from the published formulas (AMD's addrlib, as runtime/renderer/tiling.c cites): the texel's
   place in its micro tile, then for 2D tiling its pipe (bits 8-10) and bank (bits 11-14) and the
   offset within that channel above them. Mutated layouts must miss some of them, so the expectations
   tell a wrong order of bits from a right one. */
#include "runtime/tiling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { LINEAR_ALIGNED = 8, DISPLAY_1D = 9, DISPLAY_2D = 10, THIN_1D = 13, THIN_2D = 14, LINEAR_GENERAL = 31 };

typedef struct {
    uint32_t x, y, slice;
    uint64_t offset;
} expectation;

static unsigned misses(const rt_tiling *t, const expectation *e, size_t count, int report)
{
    unsigned missed = 0;
    for (size_t i = 0; i < count; i++) {
        const uint64_t got = rt_tiling_offset(t, e[i].x, e[i].y, e[i].slice);
        if (got == e[i].offset) continue;
        missed++;
        if (report)
            fprintf(stderr, "tiling %u: texel (%u, %u) slice %u at %llu, expected %llu\n", t->tiling_index, e[i].x, e[i].y, e[i].slice,
                    (unsigned long long)got, (unsigned long long)e[i].offset);
    }
    return missed;
}

static rt_tiling layout(unsigned index, unsigned bytes, uint32_t pitch, uint32_t height)
{
    rt_tiling t;
    char error[160] = "";
    memset(&t, 0, sizeof t);
    if (rt_tiling_layout(index, bytes, pitch, height, &t, error, sizeof error)) {
        fprintf(stderr, "tiling %u, %u bytes, %ux%u: %s\n", index, bytes, pitch, height, error);
        failures++;
    }
    return t;
}

/* Thin2DThin, 4-byte texels: 8 pipes, macro mode 1x1_16 (16 banks, bank height 1, aspect 2), so a
   macro tile is 128 x 64 texels and 256 bytes of it go to each of the 128 channels. */
static const expectation thin_2d_32[] = {
    {0, 0, 0, 0},
    {1, 0, 0, 4},       /* x0: index bit 0 */
    {0, 1, 0, 8},       /* y0: bit 1 */
    {2, 0, 0, 16},      /* x1: bit 2 */
    {0, 2, 0, 32},      /* y1: bit 3 */
    {4, 0, 0, 64},      /* x2: bit 4 */
    {0, 4, 0, 128},     /* y2: bit 5 */
    {7, 7, 0, 252},     /* index 63 */
    {8, 0, 0, 256},     /* x3: pipe bit 0 (x3 ^ y3 ^ x4) */
    {16, 0, 0, 768},    /* x4: pipe bits 0 and 1 (x4 ^ y4) */
    {32, 0, 0, 1024},   /* x5: pipe bit 2 (x5 ^ y5) */
    {64, 0, 0, 2048},   /* macro column 1 (x / 64): bank bit 0 */
    {0, 8, 0, 16640},   /* y3: pipe 1, and bank row 1: bank bit 3 (bank 8) */
    {0, 16, 0, 8704},   /* y4: pipe 2, bank row 2: bank bit 2 (bank 4) */
    {8, 8, 0, 16384},   /* x3 ^ y3 cancel in the pipe: bank 8 only */
};

/* The same with two macro tiles across (pitch 256) and two down (height 128), and a second slice. */
static const expectation thin_2d_32_wide[] = {
    {128, 0, 0, 36864}, /* macro tile 1: channel offset 256 (bit 15 on), bank column 2: bank 2 */
    {0, 64, 0, 71680},  /* macro tile 2: channel offset 512 (bit 16), bank row 8: banks bits 0 and 1 (bank 3) */
};
static const expectation thin_2d_32_slices[] = {
    {0, 0, 1, 47104},   /* a 128 x 64 slice is 256 bytes per channel (bit 15), and bank 0 rotated by 7 */
};

/* Display2DThin, 4-byte texels: the display micro order x0 x1 y0 x2 y1 y2. */
static const expectation display_2d_32[] = {
    {1, 0, 0, 4}, {2, 0, 0, 8}, {0, 1, 0, 16}, {4, 0, 0, 32}, {0, 2, 0, 64}, {0, 4, 0, 128}, {8, 0, 0, 256},
};

/* Thin1DThin, 4-byte texels, pitch 16: micro tiles of 256 bytes in rows of two. (3, 5) is index
   x0 + y0 + x1 + y2 = 1 + 2 + 4 + 32 = 39. */
static const expectation thin_1d_32[] = {
    {8, 0, 0, 256}, {0, 8, 0, 512}, {3, 5, 0, 156}, {8, 8, 0, 768}, {0, 0, 1, 1024},
};

/* Display1DThin, 1-byte texels: the order x0 x1 x2 y1 y0 y2; 8-byte texels: x0 y0 x1 x2 y1 y2. */
static const expectation display_1d_8[] = {{0, 1, 0, 16}, {0, 2, 0, 8}, {4, 0, 0, 4}, {0, 4, 0, 32}};
static const expectation display_1d_64[] = {{0, 1, 0, 16}, {2, 0, 0, 32}, {4, 0, 0, 64}, {0, 2, 0, 128}};

/* Thin2DThin, 16-byte texels: macro mode 1x1_8 (8 banks, aspect 1), 64 x 64 texels. A micro tile is
   1 KiB, so its texels past 256 bytes go to the next channel row (bit 14 = 8 + 3 pipe + 3 bank bits). */
static const expectation thin_2d_128[] = {
    {1, 0, 0, 16},
    {0, 4, 0, 2 << 14}, /* index 32: 512 bytes in, channel offset 512: 0 low, 2 above */
    {8, 0, 0, 256},
};

#define COUNT(a) (sizeof(a) / sizeof *(a))

static void hand_worked(void)
{
    rt_tiling t = layout(THIN_2D, 4, 128, 64);
    CHECK(t.kind == RT_TILING_2D && t.macro_width == 128 && t.macro_height == 64 && t.banks == 16 && t.pipes == 8);
    CHECK(misses(&t, thin_2d_32, COUNT(thin_2d_32), 1) == 0);
    CHECK(misses(&t, thin_2d_32_slices, COUNT(thin_2d_32_slices), 1) == 0);
    rt_tiling wide = layout(THIN_2D, 4, 256, 128);
    CHECK(misses(&wide, thin_2d_32_wide, COUNT(thin_2d_32_wide), 1) == 0);
    rt_tiling display = layout(DISPLAY_2D, 4, 128, 64);
    CHECK(misses(&display, display_2d_32, COUNT(display_2d_32), 1) == 0);
    rt_tiling thin_1d = layout(THIN_1D, 4, 16, 16);
    CHECK(thin_1d.kind == RT_TILING_1D && thin_1d.slice_bytes == 1024);
    CHECK(misses(&thin_1d, thin_1d_32, COUNT(thin_1d_32), 1) == 0);
    rt_tiling d8 = layout(DISPLAY_1D, 1, 8, 8), d64 = layout(DISPLAY_1D, 8, 8, 8);
    CHECK(misses(&d8, display_1d_8, COUNT(display_1d_8), 1) == 0);
    CHECK(misses(&d64, display_1d_64, COUNT(display_1d_64), 1) == 0);
    rt_tiling big = layout(THIN_2D, 16, 64, 64);
    CHECK(big.macro_width == 64 && big.macro_height == 64 && big.banks == 8);
    /* (63, 0): x bits 3-5 all set: pipe x3^x4 = 0, x4 = 1, x5 = 1 -> 6; in the micro tile x0 x1 x2 -> index
       1 + 4 + 16 = 21, 336 bytes: channel offset 80 low, 1 above */
    const expectation corner[] = {{63, 0, 0, 80 | 6 << 8 | 1 << 14}};
    CHECK(misses(&big, thin_2d_128, COUNT(thin_2d_128), 1) == 0);
    CHECK(misses(&big, corner, 1, 1) == 0);
    rt_tiling linear = layout(LINEAR_GENERAL, 4, 100, 3), aligned = layout(LINEAR_ALIGNED, 2, 64, 2);
    CHECK(rt_tiling_offset(&linear, 5, 2, 0) == (2 * 100 + 5) * 4);
    CHECK(rt_tiling_offset(&linear, 0, 0, 1) == 3 * 100 * 4);
    CHECK(rt_tiling_offset(&aligned, 1, 1, 0) == (64 + 1) * 2);
}

/* Mutants: the thin expectations must catch the display order, a different bank count and a
   swapped pipe/bank position. */
static void mutants(void)
{
    rt_tiling t = layout(THIN_2D, 4, 128, 64);
    rt_tiling display_order = t;
    display_order.micro = RT_MICRO_DISPLAY;
    CHECK(misses(&display_order, thin_2d_32, COUNT(thin_2d_32), 0) > 0);
    rt_tiling eight_banks = t;
    eight_banks.banks = 8;
    CHECK(misses(&eight_banks, thin_2d_32, COUNT(thin_2d_32), 0) > 0);
    rt_tiling taller_banks = t;
    taller_banks.bank_height = 2;
    CHECK(misses(&taller_banks, thin_2d_32, COUNT(thin_2d_32), 0) > 0);
    rt_tiling as_1d = t;
    as_1d.kind = RT_TILING_1D;
    CHECK(misses(&as_1d, thin_2d_32, COUNT(thin_2d_32), 0) > 0);
}

/* shadPS4's texture_cache/tile.h tabulates each mode's macro tile (width, height) by element size
   (1, 2, 4, 8, 16 bytes) for one sample: modes 10 and 14 agree with ours, and mode 0 (Depth2DThin64,
   its 64-byte tile split picking macro mode 0 at every size) is 256 x 128 at every size. */
static void macro_tiles(void)
{
    static const uint32_t sizes[5][2] = {{256, 128}, {128, 128}, {128, 64}, {128, 64}, {64, 64}};
    for (unsigned mode = 0; mode < 2; mode++)
        for (unsigned b = 0; b < 5; b++) {
            const rt_tiling t = layout(mode ? THIN_2D : DISPLAY_2D, 1u << b, 256, 1);
            CHECK(t.macro_width == sizes[b][0] && t.macro_height == sizes[b][1]);
            CHECK(t.height == sizes[b][1]);
        }
    for (unsigned b = 0; b < 5; b++) {
        const rt_tiling t = layout(0, 1u << b, 256, 1);
        CHECK(t.macro_width == 256 && t.macro_height == 128);
        CHECK(rt_tiling_uniform_micro(&t) == (b == 0)); /* a 1-byte micro tile is the split's 64 bytes: nothing to split */
    }
}

/* Every texel of a two-slice image lands on its own element inside the image: the layout is a
   permutation of the image's bytes, for every mode and element size done. */
static void permutations(void)
{
    static const unsigned modes[] = {LINEAR_ALIGNED, DISPLAY_1D, DISPLAY_2D, THIN_1D, THIN_2D, LINEAR_GENERAL, 0 /* Depth2DThin64 */};
    for (size_t m = 0; m < COUNT(modes); m++)
        for (unsigned b = 0; b < 5; b++) {
            const unsigned bytes = 1u << b;
            const rt_tiling t = layout(modes[m], bytes, 512, 256);
            const uint64_t size = rt_tiling_size(&t, 2);
            unsigned char *seen = calloc(size / bytes, 1);
            if (!seen) continue;
            int ok = 1;
            for (uint32_t s = 0; s < 2 && ok; s++)
                for (uint32_t y = 0; y < t.height && ok; y++)
                    for (uint32_t x = 0; x < t.pitch && ok; x++) {
                        const uint64_t at = rt_tiling_offset(&t, x, y, s);
                        if (at % bytes || at >= size || seen[at / bytes]) ok = 0;
                        else seen[at / bytes] = 1;
                    }
            if (!ok) fprintf(stderr, "tiling %u, %u bytes: not a permutation\n", modes[m], bytes);
            CHECK(ok);
            free(seen);
        }
}

/* Depth2DThin64 (mode 0), 4-byte elements, worked by hand from addrlib's macro-tiled formula with
   tileSplitSlice: a 256 x 128 image is one macro tile (mode 0: bank height 4, 16 banks, aspect 4), its
   pieces 64 bytes, so the macro tile holds 256 bytes per channel. (1, 0) is element 1 of the first
   piece: 4; (0, 2) is element 8 (thin order x0 y0 x1 y1): 32; (0, 4) is element 32, byte 128: piece 2,
   two slices of 256 channel bytes up (512: channel row 2), its bank rotated by (16 / 2 + 1) x 2 = 2:
   2 << 11 | 2 << 15 = 69632. Detiling a split layout puts each texel back too. */
static void depth_tiles(void)
{
    const rt_tiling t = layout(0, 4, 256, 128);
    CHECK(rt_tiling_offset(&t, 0, 0, 0) == 0 && rt_tiling_offset(&t, 1, 0, 0) == 4 && rt_tiling_offset(&t, 0, 2, 0) == 32);
    CHECK(rt_tiling_offset(&t, 0, 4, 0) == 69632);
    uint32_t *tiled = calloc(rt_tiling_size(&t, 1) / 4, 4), *linear = calloc(256 * 128, 4);
    if (!tiled || !linear) {
        free(tiled), free(linear);
        failures++;
        return;
    }
    for (uint32_t y = 0; y < 128; y++)
        for (uint32_t x = 0; x < 256; x++) tiled[rt_tiling_offset(&t, x, y, 0) / 4] = y << 16 | x;
    rt_tiling_detile(&t, (const uint8_t *)tiled, (uint8_t *)linear, 256, 128, 1);
    int ok = 1;
    for (uint32_t y = 0; y < 128; y++)
        for (uint32_t x = 0; x < 256; x++) ok &= linear[y * 256 + x] == (y << 16 | x);
    CHECK(ok);
    free(tiled), free(linear);
}

/* Detiling puts each texel back in row order. */
static void detiling(void)
{
    const rt_tiling t = layout(THIN_2D, 4, 256, 100);
    CHECK(t.height == 128);
    const uint32_t width = 200, height = 100;
    uint32_t *tiled = calloc(rt_tiling_size(&t, 1) / 4, 4), *linear = calloc((size_t)width * height, 4);
    if (!tiled || !linear) {
        free(tiled), free(linear);
        failures++;
        return;
    }
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++) tiled[rt_tiling_offset(&t, x, y, 0) / 4] = y << 16 | x;
    rt_tiling_detile(&t, (const uint8_t *)tiled, (uint8_t *)linear, width, height, 1);
    int ok = 1;
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++) ok &= linear[y * width + x] == (y << 16 | x);
    CHECK(ok);
    /* tiling it back gives the same tiled bytes, padding untouched */
    uint32_t *again = calloc(rt_tiling_size(&t, 1) / 4, 4);
    if (again) {
        rt_tiling_tile(&t, (const uint8_t *)linear, (uint8_t *)again, width, height, 1);
        CHECK(!memcmp(again, tiled, rt_tiling_size(&t, 1)));
        free(again);
    }
    free(tiled), free(linear);
}

/* Detiling a micro tile at a time (rt_tiling_detile) puts every texel where rt_tiling_offset says,
   for every mode and element size, on an image whose width and height are not whole micro tiles. */
static void fast_copies(void)
{
    static const unsigned modes[] = {8, 9, 10, 13, 14, 31};
    for (size_t m = 0; m < COUNT(modes); m++)
        for (unsigned b = 0; b < 5; b++) {
            const unsigned bytes = 1u << b;
            const rt_tiling t = layout(modes[m], bytes, 256, 130);
            const uint32_t width = 250, height = 130, slices = 2;
            const uint64_t size = rt_tiling_size(&t, slices);
            uint8_t *tiled = malloc(size), *linear = malloc((size_t)width * height * slices * bytes);
            if (!tiled || !linear) {
                free(tiled), free(linear);
                failures++;
                continue;
            }
            for (uint64_t i = 0; i < size; i++) tiled[i] = (uint8_t)(i * 2654435761u >> 13);
            rt_tiling_detile(&t, tiled, linear, width, height, slices);
            int ok = 1;
            for (uint32_t s = 0; s < slices; s++)
                for (uint32_t y = 0; y < height; y++)
                    for (uint32_t x = 0; x < width; x++)
                        ok &= !memcmp(linear + (((size_t)s * height + y) * width + x) * bytes, tiled + rt_tiling_offset(&t, x, y, s), bytes);
            if (!ok) fprintf(stderr, "tiling %u, %u bytes: fast detile differs\n", modes[m], bytes);
            CHECK(ok);
            free(tiled), free(linear);
        }
}

/* Every texel lies at its micro tile's first texel plus its place in rt_tiling_micro_spread's table,
   for every mode and element size, two slices (the form the renderer's GPU copies take). */
static void micro_spreads(void)
{
    static const unsigned modes[] = {8, 9, 10, 13, 14, 31};
    for (size_t m = 0; m < COUNT(modes); m++)
        for (unsigned b = 0; b < 5; b++) {
            const rt_tiling t = layout(modes[m], 1u << b, 256, 130);
            uint64_t spread[RT_TILING_MICRO_TEXELS];
            rt_tiling_micro_spread(&t, spread);
            int ok = 1;
            for (uint32_t s = 0; s < 2; s++)
                for (uint32_t y = 0; y < 130; y++)
                    for (uint32_t x = 0; x < 256; x++)
                        ok &= rt_tiling_offset(&t, x, y, s) == rt_tiling_offset(&t, x & ~7u, y & ~7u, s) + spread[(y & 7) * 8 + (x & 7)];
            if (!ok) fprintf(stderr, "tiling %u, %u bytes: micro spread differs\n", modes[m], 1u << b);
            CHECK(ok);
        }
}

static void refusals(void)
{
    rt_tiling t;
    char error[160] = "";
    CHECK(rt_tiling_layout(4, 4, 128, 64, &t, error, sizeof error) == -1 && strstr(error, "tiling index 4"));
    CHECK(rt_tiling_layout(THIN_2D, 4, 1000, 64, &t, error, sizeof error) == -1 && strstr(error, "pitch 1000"));
    CHECK(rt_tiling_layout(THIN_1D, 4, 12, 8, &t, error, sizeof error) == -1 && strstr(error, "pitch 12"));
    CHECK(rt_tiling_layout(THIN_2D, 3, 128, 64, &t, error, sizeof error) == -1 && strstr(error, "3 bytes"));
}

/* Mip chains (rt_tiling_mip_chain), worked by hand from the rules in tiling.c. */
static void mip_chains(void)
{
    rt_tiling_level levels[RT_TILING_MAX_LEVELS];
    char error[160] = "";
    /* the character creation screen's 16x16 BC1 texture: 1D thin, POW2_PAD, five levels; every level is
       one micro tile of 8x8 blocks (512 bytes), the smaller ones padded up to it */
    CHECK(rt_tiling_mip_chain(THIN_1D, 8, 4, 1, 16, 16, 16, 16, 5, 1, levels, error, sizeof error) == 0);
    for (unsigned m = 0; m < 5; m++) {
        CHECK(levels[m].offset == 512u * m && levels[m].layout.pitch == 8 && levels[m].layout.height == 8);
        CHECK(levels[m].layout.tiling_index == THIN_1D && levels[m].columns == (m < 2 ? 4u >> m : 1u));
    }
    /* 2D thin, 4-byte texels, 256x256, nine levels: macro tiled (128x64 texels per macro tile) while a
       level spans one, then micro tiled in the thin order, each level at least 256 bytes */
    static const uint64_t offsets[9] = {0, 262144, 327680, 344064, 348160, 349184, 349440, 349696, 349952};
    static const uint32_t pitches[9] = {256, 128, 64, 32, 16, 8, 8, 8, 8};
    CHECK(rt_tiling_mip_chain(THIN_2D, 4, 1, 0, 256, 256, 256, 256, 9, 1, levels, error, sizeof error) == 0);
    for (unsigned m = 0; m < 9; m++) {
        CHECK(levels[m].offset == offsets[m] && levels[m].layout.pitch == pitches[m]);
        CHECK(levels[m].layout.kind == (m < 2 ? RT_TILING_2D : RT_TILING_1D));
    }
    /* two slices: each level holds both before the next level starts */
    CHECK(rt_tiling_mip_chain(THIN_1D, 8, 4, 1, 16, 16, 16, 16, 2, 2, levels, error, sizeof error) == 0 && levels[1].offset == 1024);
    /* linear, 1-byte texels, 16x4 then 8x2: pitches of 64 grown to a 256-element slice */
    CHECK(rt_tiling_mip_chain(8, 1, 1, 0, 64, 4, 16, 4, 2, 1, levels, error, sizeof error) == 0);
    CHECK(levels[0].layout.pitch == 64 && levels[1].offset == 256 && levels[1].layout.pitch == 128);
    CHECK(rt_tiling_mip_chain(THIN_1D, 8, 3, 1, 16, 16, 16, 16, 2, 1, levels, error, sizeof error) == -1);
}

int main(void)
{
    mip_chains();
    hand_worked();
    mutants();
    macro_tiles();
    permutations();
    detiling();
    depth_tiles();
    fast_copies();
    micro_spreads();
    refusals();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("tiling: all checks passed\n");
    return 0;
}
