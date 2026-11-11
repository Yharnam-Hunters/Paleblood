/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The draw executor (runtime/renderer/render_executor.c) on a GPU: guest memory is a shared memory
   file mapped below 2^40 (where the runtime keeps GPU-visible memory), holding a vertex and a pixel
   shader (gcn_programs.h) and the triangle's corners and colours. A command buffer's calls are
   recorded as the GPU library's replacements will record them (render target, viewport, shader
   register blocks, user data, an auto-index draw), the executor runs them, and every pixel of the
   render target is checked.

   The triangle covers the 16 x 16 target with red (x + 0.5) / 16 and green (y + 0.5) / 16 at pixel
   (x, y), blue 0.25 and alpha 1, as in test_gcn_draw. Without a suitable Vulkan device the test
   reports itself skipped (exit 77). */
#define _GNU_SOURCE /* memfd_create, MAP_FIXED_NOREPLACE (render_fixture.h) */
#include "runtime/render_executor.h"

#include "render_fixture.h"
#include "runtime/tiling.h"

#include <stdlib.h>

/* Every pixel of the target holds the triangle's colour. */
static void check_triangle(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    const int read = rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error);
    if (read) fprintf(stderr, "read: %s\n", error);
    CHECK(read == 0);
    if (!read) check_triangle_pixels(bgra);
}

/* Records the triangle with these registers set before its draw, and runs it. */
static int run_with_registers(rt_executor *x, uint64_t buffer, uint64_t target, const uint32_t (*registers)[2], int count,
                              uint32_t instances, char *error, size_t error_size)
{
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    for (int i = 0; i < count; i++, offset += 0x10)
        CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    if (instances) {
        CHECK(rt_render_record_instances(buffer, offset, instances, error, error_size) == 0);
        offset += 0x10;
    }
    return rt_executor_run(x, buffer, record_triangle_draw(buffer, offset), error, error_size);
}

/* The reference image's texel (x, y) as 8_8_8_8 unorm bytes, red first: what check_triangle_pixels
   expects at pixel (x, y). */
static uint32_t reference_texel(uint32_t px, uint32_t y)
{
    const uint32_t red = (uint32_t)lroundf(((float)px + 0.5f) / SIZE * 255), green = (uint32_t)lroundf(((float)y + 0.5f) / SIZE * 255);
    return red | green << 8 | 64u << 16 | 0xffu << 24;
}

/* Draws pixel_image_texel (each pixel the texel under it) into target with the image descriptor in the
   pixel shader's user data; the run's result. */
static int draw_texels(rt_executor *x, uint64_t target, const uint32_t image[8], char *error, size_t error_size)
{
    enum { TEXEL_BUFFER = 0xc0f00000, TEXEL_CODE = 0x2a00 };
    memcpy((void *)(uintptr_t)at(TEXEL_CODE), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(TEXEL_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(TEXEL_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(TEXEL_BUFFER, offset, pixel, error, error_size) == 0);
    CHECK(rt_render_record_user_data(TEXEL_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, image, 8, error, error_size) == 0);
    return rt_executor_run(x, TEXEL_BUFFER, record_triangle_draw(TEXEL_BUFFER, offset + 0x20), error, error_size);
}

/* The pixels of target that differ from the reference image (all of them on a failed read). */
static int differing_pixels(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int differ = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            differ += !(near(p >> 16 & 0xff, ((float)px + 0.5f) / SIZE) && near(p >> 8 & 0xff, ((float)y + 0.5f) / SIZE) &&
                        near(p & 0xff, 0.25f) && (p >> 24) == 0xff);
        }
    return differ;
}

/* A positive float as an unsigned small float of mantissa_bits (5-bit exponent, bias 15), or (with
   mantissa_bits 10 and the sign 0) a half: exact for the dyadic values the tests use. */
static uint32_t small_float_bits(float v, int mantissa_bits)
{
    if (v == 0) return 0;
    int exponent;
    const float fraction = frexpf(v, &exponent); /* v = fraction * 2^exponent, fraction in [0.5, 1) */
    const uint32_t mantissa = (uint32_t)lroundf((fraction * 2 - 1) * (float)(1u << mantissa_bits));
    return (uint32_t)(exponent - 1 + 15) << mantissa_bits | mantissa;
}

/* Float image formats: the reference image as 16_16_16_16 float halves (DATA_FORMAT 12) and as
   10_11_11 float small floats (DATA_FORMAT 6; alpha 1 from the missing component), linear, read texel
   by texel (pixel_image_texel) into an 8_8_8_8 target, draw the reference image. */
static void float_images(rt_executor *x)
{
    enum { HALVES = 0x1e00000, SMALL = 0x1e40000, FLOAT_IMAGE_TARGET = 0x1e80000, LINEAR_ALIGNED = 8, NUM_FLOAT = 7 };
    char error[200] = "";
    for (int kind = 0; kind < 2; kind++) {
        const unsigned bytes = kind ? 4 : 8;
        rt_tiling layout;
        CHECK(rt_tiling_layout(LINEAR_ALIGNED, bytes, SIZE, SIZE, &layout, error, sizeof error) == 0);
        uint8_t *memory = (uint8_t *)(uintptr_t)at(kind ? SMALL : HALVES);
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) {
                const float red = ((float)px + 0.5f) / SIZE, green = ((float)y + 0.5f) / SIZE;
                uint8_t *texel = memory + rt_tiling_offset(&layout, px, y, 0);
                if (kind) {
                    const uint32_t packed = small_float_bits(red, 6) | small_float_bits(green, 6) << 11 | small_float_bits(0.25f, 5) << 22;
                    memcpy(texel, &packed, 4);
                } else {
                    const uint16_t halves[4] = {(uint16_t)small_float_bits(red, 10), (uint16_t)small_float_bits(green, 10),
                                                (uint16_t)small_float_bits(0.25f, 10), (uint16_t)small_float_bits(1.0f, 10)};
                    memcpy(texel, halves, 8);
                }
            }
        uint32_t image[8];
        image_descriptor(image, at(kind ? SMALL : HALVES), kind ? 6 : 12, NUM_FLOAT, SIZE, SIZE);
        image[3] |= LINEAR_ALIGNED << 20;
        if (kind) image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 6 << 6 | 1 << 9); /* x, y, z, 1 */
        const int drawn = draw_texels(x, at(FLOAT_IMAGE_TARGET), image, error, sizeof error);
        if (drawn) fprintf(stderr, "float image %d: %s\n", kind, error);
        CHECK(drawn == 0);
        const int differ = differing_pixels(x, at(FLOAT_IMAGE_TARGET));
        if (differ) fprintf(stderr, "float image %d: %d pixels differ\n", kind, differ);
        CHECK(differ == 0);
    }
}

/* The pixels of target that differ from the reference image's red and green with blue 0 and alpha 1
   (all of them on a failed read). */
static int two_channel_misses(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int differ = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            differ += !(near(p >> 16 & 0xff, ((float)px + 0.5f) / SIZE) && near(p >> 8 & 0xff, ((float)y + 0.5f) / SIZE) &&
                        (p & 0xff) == 0 && (p >> 24) == 0xff);
        }
    return differ;
}

/* Pixel shaders reading images the CPU wrote into guest memory (image_load_mip from a sampled image):
   a 128 x 64 8_8_8_8 image tiled as Thin2DThin (index 14), its texels placed with runtime/tiling.h,
   draws the reference image; the same bytes described as Display2DThin (index 10) do not. A linear one
   (index 8), DST_SEL z, y, x, w over blue-first bytes, and the guest changing its texels between
   draws all read what the guest memory holds; so does an 8_8 image. A target larger than the image
   reads outside it and stops by name. */
static void sampled_images(rt_executor *x)
{
    enum { TILED = 0x800000, LINEAR = 0x810000, SMALL = 0x820000, TEXEL_TARGET = 0x880000, WIDTH = 128, HEIGHT = 64,
           THIN_2D = 14, DISPLAY_2D = 10, LINEAR_ALIGNED = 8 };
    char error[256] = "";
    rt_tiling tiled;
    CHECK(rt_tiling_layout(THIN_2D, 4, WIDTH, HEIGHT, &tiled, error, sizeof error) == 0);
    uint8_t *guest = (uint8_t *)(uintptr_t)at(0);
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t px = 0; px < WIDTH; px++) {
            const uint32_t texel = reference_texel(px % SIZE, y % SIZE);
            memcpy(guest + TILED + rt_tiling_offset(&tiled, px, y, 0), &texel, 4);
            memcpy(guest + LINEAR + (y * WIDTH + px) * 4, &texel, 4);
        }
    uint32_t image[8];
    image_descriptor(image, at(TILED), 10, 0, WIDTH, HEIGHT);
    image[3] |= THIN_2D << 20;
    int drawn = draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error);
    if (drawn) fprintf(stderr, "sampled image: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(TEXEL_TARGET));

    image[3] = (image[3] & ~(0x1fu << 20)) | DISPLAY_2D << 20;    /* mutant: the wrong micro-tile order */
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) > 0);

    image_descriptor(image, at(LINEAR), 10, 0, WIDTH, HEIGHT);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);

    /* blue-first bytes, read through DST_SEL z, y, x, w */
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
        uint32_t *t = (uint32_t *)(guest + LINEAR) + i;
        *t = (*t & 0xff00ff00u) | (*t & 0xffu) << 16 | (*t >> 16 & 0xffu);
    }
    image[3] = (image[3] & ~0xfffu) | (6 | 5 << 3 | 4 << 6 | 7 << 9);
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 6 << 6 | 7 << 9); /* mutant: in place */
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) > 0);

    /* 8_8_8_8 sRGB (NUM_FORMAT 9): each texel draws as its bytes decoded from sRGB to linear (the
       transfer function of IEC 61966-2-1), alpha as is; read as unorm the same bytes do not */
    {
        enum { SRGB = 0x8c0000 };
        uint32_t *const texels = (uint32_t *)(guest + SRGB);
        for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) texels[i] = reference_texel(i % WIDTH % SIZE, i / WIDTH % SIZE);
        uint32_t srgb[8];
        for (int unorm = 0; unorm < 2; unorm++) {
            image_descriptor(srgb, at(SRGB), 10, unorm ? 0 : 9, WIDTH, HEIGHT);
            srgb[3] |= LINEAR_ALIGNED << 20;
            const int srgb_drawn = draw_texels(x, at(TEXEL_TARGET), srgb, error, sizeof error);
            if (srgb_drawn) fprintf(stderr, "sRGB image: %s\n", error);
            uint32_t bgra[SIZE * SIZE];
            CHECK(srgb_drawn == 0 && rt_executor_read_target(x, at(TEXEL_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            int decoded = 1;
            for (int q = 0; q < SIZE * SIZE; q++) {
                const uint32_t texel = reference_texel(q % SIZE, q / SIZE), p = bgra[q];
                const uint32_t drawn[3] = {p >> 16 & 0xff, p >> 8 & 0xff, p & 0xff}; /* red, green, blue */
                for (int c = 0; c < 3; c++) {
                    const double v = (texel >> (8 * c) & 0xff) / 255.0;
                    const double linear = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
                    decoded &= fabs((double)drawn[c] - linear * 255) <= 2.0;
                }
            }
            CHECK(unorm ? !decoded : decoded);
        }
    }

    /* A mip-mapped image (LAST_LEVEL 2, linear): its levels follow level 0 as rt_tiling_mip_chain lays
       them out, and a draw sampling at level 0 sees level 0's texels; with level 0 changed it does not */
    {
        enum { MIPPED = 0x900000 };
        rt_tiling_level chain[3];
        CHECK(rt_tiling_mip_chain(LINEAR_ALIGNED, 4, 1, 0, WIDTH, HEIGHT, WIDTH, HEIGHT, 3, 1, chain, error, sizeof error) == 0);
        for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) ((uint32_t *)(guest + MIPPED))[i] = reference_texel(i % WIDTH % SIZE, i / WIDTH % SIZE);
        memset(guest + MIPPED + chain[1].offset, 0x55, chain[2].offset - chain[1].offset + WIDTH * HEIGHT); /* other levels: grey */
        uint32_t mipped[8];
        image_descriptor(mipped, at(MIPPED), 10, 0, WIDTH, HEIGHT);
        mipped[3] |= LINEAR_ALIGNED << 20 | 2u << 16; /* LAST_LEVEL 2 */
        const int mip_drawn = draw_texels(x, at(TEXEL_TARGET), mipped, error, sizeof error);
        if (mip_drawn) fprintf(stderr, "mip-mapped image: %s\n", error);
        CHECK(mip_drawn == 0 && differing_pixels(x, at(TEXEL_TARGET)) == 0);
        memset(guest + MIPPED, 0x55, WIDTH * HEIGHT * 4); /* mutant: level 0 grey too */
        CHECK(draw_texels(x, at(TEXEL_TARGET), mipped, error, sizeof error) == 0 && differing_pixels(x, at(TEXEL_TARGET)) > 0);
    }

    /* the guest rewrites the texels: the next draw reads the new ones */
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) ((uint32_t *)(guest + LINEAR))[i] = reference_texel(i % WIDTH % SIZE, i / WIDTH % SIZE);
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);

    /* A descriptor the shader loads from memory (pixel_two_images): a zero image from the user data,
       then the reference image's descriptor read from the guest memory at the user data's pointer + 16
       when the draw runs; the sum is the reference image. Pointing the table at the zero image too
       gives black. */
    {
        enum { TABLE = 0x830000, ZERO = 0x840000, TWO_BUFFER = 0xc0f10000, TWO_CODE = 0x2e00 };
        memset(guest + ZERO, 0, WIDTH * HEIGHT * 4);
        uint32_t user[12] = {0}, *table = (uint32_t *)(uintptr_t)at(TABLE);
        user[2] = (uint32_t)at(TABLE), user[3] = (uint32_t)(at(TABLE) >> 32);
        image_descriptor(user + 4, at(ZERO), 10, 0, WIDTH, HEIGHT);
        user[7] |= LINEAR_ALIGNED << 20;
        image_descriptor(table + 4, at(LINEAR), 10, 0, WIDTH, HEIGHT);
        table[7] |= LINEAR_ALIGNED << 20;
        memcpy((void *)(uintptr_t)at(TWO_CODE), pixel_two_images, sizeof pixel_two_images);
        for (int pass = 0; pass < 2; pass++) {
            rt_render_forget(), rt_executor_reset(x);
            uint32_t offset = record_setup(TWO_BUFFER, at(TEXEL_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
            const uint64_t code = at(TWO_CODE);
            const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 12 << 1, 0, 0x4, 0x302, 0x302,
                                                           0, 0, 0, 0xf};
            CHECK(rt_render_record_pixel_shader(TWO_BUFFER, offset, pixel, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(TWO_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(TWO_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 4, error, sizeof error) == 0);
            drawn = rt_executor_run(x, TWO_BUFFER, record_triangle_draw(TWO_BUFFER, offset + 0x30), error, sizeof error);
            if (drawn) fprintf(stderr, "descriptor from memory: %s\n", error);
            CHECK(drawn == 0);
            if (pass == 0) CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
            else CHECK(differing_pixels(x, at(TEXEL_TARGET)) == SIZE * SIZE);
            image_descriptor(table + 4, at(ZERO), 10, 0, WIDTH, HEIGHT);
            table[7] |= LINEAR_ALIGNED << 20;
        }
    }

    /* 8_8 unorm (DATA_FORMAT 3), linear, DST_SEL x, y, 0, 1: red and green from the two bytes, blue 0,
       alpha 1; read as 8_8_8_8 (mutant) the bytes land elsewhere */
    {
        enum { TWO_CHANNEL = 0x8a0000 };
        for (uint32_t y = 0; y < HEIGHT; y++)
            for (uint32_t px = 0; px < WIDTH; px++) {
                const uint32_t texel = reference_texel(px % SIZE, y % SIZE);
                const uint8_t pair[2] = {(uint8_t)texel, (uint8_t)(texel >> 8)};
                memcpy(guest + TWO_CHANNEL + (y * WIDTH + px) * 2, pair, 2);
            }
        image_descriptor(image, at(TWO_CHANNEL), 3, 0, WIDTH, HEIGHT);
        image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 0 << 6 | 1 << 9) | LINEAR_ALIGNED << 20;
        drawn = draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error);
        if (drawn) fprintf(stderr, "8_8 image: %s\n", error);
        CHECK(drawn == 0 && two_channel_misses(x, at(TEXEL_TARGET)) == 0);
        image_descriptor(image, at(TWO_CHANNEL), 10, 0, WIDTH / 2, HEIGHT);
        image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 0 << 6 | 1 << 9) | LINEAR_ALIGNED << 20;
        CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0 && two_channel_misses(x, at(TEXEL_TARGET)) > 0);
    }
    image_descriptor(image, at(SMALL), 10, 0, 8, 8);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == -1 && strstr(error, "outside the image"));
    image[3] = (image[3] & ~(0x1fu << 20)) | 2u << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == -1 && strstr(error, "tiling index 2"));
}

/* A sampled colour lookup (pixel_lookup): the reference image's texel under each pixel, then red,
   green and blue through layers 0, 1 and 2 of a 4-texel 1D array of floats (linear, tiling index 8,
   the CPU's texels): a ramp (i + 0.5) / 4, its inverse 1 - (i + 0.5) / 4, and 0.25. Bilinear filtering
   with clamping to the last texel gives the ramp's own coordinate between texel centres (1/8 to 7/8)
   and the end texels' values outside; so does MAX_ANISO_RATIO 4 with bilinear filters. Mutants: point
   filtering and wrapping differ; with point minification and bilinear magnification the lookup is
   magnified (a texel spans four pixels), so it is the bilinear one, as are anisotropic bilinear
   filters; a border clamp, a MAX_ANISO_RATIO past 4 or a depth compare stops by name. */
static int lookup_misses(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int misses = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            /* the texel bytes reference_texel stores, as unorm values */
            const float red = (float)lroundf(((float)px + 0.5f) / SIZE * 255) / 255, green = (float)lroundf(((float)y + 0.5f) / SIZE * 255) / 255;
            const float r = fminf(fmaxf(red, 0.125f), 0.875f), g = 1 - fminf(fmaxf(green, 0.125f), 0.875f);
            misses += !(near(p >> 16 & 0xff, r) && near(p >> 8 & 0xff, g) && near(p & 0xff, 0.25f) && (p >> 24) == 0xff);
        }
    return misses;
}

/* The executor keeps more images than the 64 it once did: 100 runs, each drawing a different 16x16
   linear image (the reference image, written by the CPU) without a reset, all draw it. */
static void many_images(rt_executor *x)
{
    enum { MANY = 100, MANY_BASE = 0x1c00000, MANY_TARGET = 0x1bc0000, MANY_BUFFER = 0xc0ff0000, MANY_CODE = 0x3a00, LINEAR_ALIGNED = 8 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(MANY_CODE), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    int all = 1;
    for (unsigned k = 0; k < MANY; k++) {
        uint32_t *texels = (uint32_t *)(uintptr_t)at(MANY_BASE + k * 0x1000);
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) texels[y * 64 + px] = reference_texel(px, y); /* linear aligned: 64-texel rows */
        uint32_t image[8];
        image_descriptor(image, at(MANY_BASE + k * 0x1000), 10, 0, SIZE, SIZE);
        image[3] |= LINEAR_ALIGNED << 20, image[4] = 63u << 13; /* PITCH 64 */
        rt_render_forget();
        uint32_t offset = record_setup(MANY_BUFFER, at(MANY_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(MANY_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(MANY_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(MANY_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, image, 8, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, MANY_BUFFER, record_triangle_draw(MANY_BUFFER, offset + 0x20), error, sizeof error);
        if (ran) fprintf(stderr, "image %u: %s\n", k, error);
        all &= ran == 0;
    }
    CHECK(all);
    CHECK(differing_pixels(x, at(MANY_TARGET)) == 0);
}

/* Sampling picks the level the derivatives give (pixel_minified): a 64x64 image with three levels, red,
   green and blue, sampled at (x / 16, y / 16) over 16x16 pixels spans 4 texels a pixel, level 2:
   blue with a point or linear mip filter, red (level 0) with none. image_sample_l at level 1
   (pixel_explicit_level) draws green with a mip filter, red with none; image_sample_lz
   (pixel_level_zero) draws red with any. */
static void sampled_levels(rt_executor *x)
{
    enum { MIPPED = 0x8d0000, LEVELS_TARGET = 0x8e0000, LEVELS_CODE = 0x3400, EXPLICIT_CODE = 0x3600, ZERO_CODE = 0x3c00, LEVELS_BUFFER = 0xc0f30000,
           EDGE = 64, LEVELS = 3, LINEAR_ALIGNED = 8, MIP_POINT = 1, MIP_LINEAR = 2 };
    enum { BY_DERIVATIVES, AT_LEVEL_1, AT_LEVEL_0, KINDS };
    static const uint32_t colours[LEVELS] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u}; /* red, green, blue (bytes R, G, B, A) */
    char error[256] = "";
    uint8_t *guest = (uint8_t *)(uintptr_t)at(0);
    rt_tiling_level chain[LEVELS];
    CHECK(rt_tiling_mip_chain(LINEAR_ALIGNED, 4, 1, 0, EDGE, EDGE, EDGE, EDGE, LEVELS, 1, chain, error, sizeof error) == 0);
    for (unsigned m = 0; m < LEVELS; m++) {
        const uint32_t texels = chain[m].layout.pitch * chain[m].layout.height;
        for (uint32_t i = 0; i < texels; i++) ((uint32_t *)(guest + MIPPED + chain[m].offset))[i] = colours[m];
    }
    memcpy((void *)(uintptr_t)at(LEVELS_CODE), pixel_minified, sizeof pixel_minified);
    memcpy((void *)(uintptr_t)at(EXPLICIT_CODE), pixel_explicit_level, sizeof pixel_explicit_level); /* its own address: shaders are kept by address */
    memcpy((void *)(uintptr_t)at(ZERO_CODE), pixel_level_zero, sizeof pixel_level_zero);
    for (unsigned run = 0; run < KINDS * (MIP_LINEAR + 1); run++) {
        const unsigned mip = run % (MIP_LINEAR + 1), kind = run / (MIP_LINEAR + 1);
        uint32_t user[16] = {0};
        image_descriptor(user + 4, at(MIPPED), 10, 0, EDGE, EDGE);
        user[7] |= LINEAR_ALIGNED << 20 | (LEVELS - 1) << 16;
        user[12] = 0x2 | 0x2 << 3 | 0x2 << 6, user[13] = 15u * 256 << 12, user[14] = mip << 26; /* clamped, MAX_LOD 15, point filters */
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(LEVELS_BUFFER, at(LEVELS_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(kind == AT_LEVEL_1 ? EXPLICIT_CODE : kind == AT_LEVEL_0 ? ZERO_CODE : LEVELS_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302,
                                                       0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(LEVELS_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LEVELS_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LEVELS_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, LEVELS_BUFFER, record_triangle_draw(LEVELS_BUFFER, offset + 0x30), error, sizeof error);
        if (drawn) fprintf(stderr, "sampled levels: %s\n", error);
        CHECK(drawn == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(LEVELS_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
        const uint32_t expected = mip == 0 || kind == AT_LEVEL_0 ? 0xffff0000u : kind == AT_LEVEL_1 ? 0xff00ff00u : 0xff0000ffu; /* BGRA words: red, green, blue */
        int covered = 0, right = 1;
        for (int i = 0; i < SIZE * SIZE; i++)
            if (bgra[i]) covered++, right &= bgra[i] == expected; /* the triangle's pixels */
        if (!right) fprintf(stderr, "sampled levels, mip filter %u, kind %u: a pixel %08x\n", mip, kind, bgra[SIZE * SIZE / 2]);
        CHECK(covered > 0 && right);
    }
}

/* A cube (T# TYPE 11, one cube: six 4x4 faces, linear, each face one colour) sampled at (1.5, 1.5)
   on face 4, then face 1, and face 9 (face 1 of an array's second cube, which has none: the 2D array
   of six faces clamps the layer to 5) draws each face's colour (pixel_cube_face). */
static void sampled_cube(rt_executor *x)
{
    enum { CUBE = 0x8f0000, CUBE_TARGET = 0x900000, CUBE_CODE = 0x3800, CUBE_BUFFER = 0xc0f40000, EDGE = 4, FACES = 6, LINEAR_ALIGNED = 8,
           TYPE_CUBE = 11 };
    char error[256] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(LINEAR_ALIGNED, 4, EDGE, EDGE, &layout, error, sizeof error) == 0);
    for (uint32_t face = 0; face < FACES; face++)
        for (uint32_t y = 0; y < EDGE; y++)
            for (uint32_t px = 0; px < EDGE; px++) {
                const uint32_t colour = 0xff000000u | (40u * (face + 1)) << 8 | 0x20u; /* bytes R 0x20, G 40 (face + 1), B 0, A 0xff */
                memcpy((uint8_t *)(uintptr_t)at(CUBE) + rt_tiling_offset(&layout, px, y, face), &colour, 4);
            }
    memcpy((void *)(uintptr_t)at(CUBE_CODE), pixel_cube_face, sizeof pixel_cube_face);
    static const float faces[] = {4, 1, 9};
    static const uint32_t shown[] = {4, 1, 5};
    for (unsigned k = 0; k < 3; k++) {
        uint32_t user[13] = {0};
        image_descriptor(user + 4, at(CUBE), 10, 0, EDGE, EDGE);
        user[7] = (user[7] & 0x0fffffffu) | LINEAR_ALIGNED << 20 | (uint32_t)TYPE_CUBE << 28;
        user[9] = 35u << 13; /* LAST_ARRAY 35, as the game's cube descriptors have it: not used */
        user[0] = 0x2 | 0x2 << 3 | 0x2 << 6; /* the sampler: clamped, point filters */
        memcpy(&user[12], &faces[k], 4);
        rt_render_forget(), rt_executor_reset(x);
        const uint32_t offset = record_setup(CUBE_BUFFER, at(CUBE_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(CUBE_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 13 << 1, 0, 0x4, PERSP_CENTER,
                                                       PERSP_CENTER, 0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(CUBE_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(CUBE_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(CUBE_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(CUBE_BUFFER, offset + 0x30, RT_STAGE_PIXEL, 12, user + 12, 1, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, CUBE_BUFFER, record_triangle_draw(CUBE_BUFFER, offset + 0x40), error, sizeof error);
        if (drawn) fprintf(stderr, "cube: %s\n", error);
        CHECK(drawn == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(CUBE_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
        const uint32_t expected = 0xff000000u | 0x20u << 16 | (40u * (shown[k] + 1)) << 8; /* BGRA: red 0x20, green by face */
        int right = 1;
        for (int i = 0; i < SIZE * SIZE; i++) right &= bgra[i] == expected;
        if (!right) fprintf(stderr, "cube face %g: %08x, not %08x\n", (double)faces[k], bgra[0], expected);
        CHECK(right);
    }
}

static void sampled_lookup(rt_executor *x)
{
    enum { IMAGE = 0x850000, LUT = 0x860000, TABLE = 0x870000, LOOKUP_TARGET = 0x8c0000, LOOKUP_CODE = 0x3200, LOOKUP_BUFFER = 0xc0f20000,
           WIDTH = 128, HEIGHT = 64, LINEAR_ALIGNED = 8, LUT_TEXELS = 4 };
    char error[256] = "";
    uint8_t *guest = (uint8_t *)(uintptr_t)at(0);
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t px = 0; px < WIDTH; px++) ((uint32_t *)(guest + IMAGE))[y * WIDTH + px] = reference_texel(px % SIZE, y % SIZE);
    float *lut = (float *)(uintptr_t)at(LUT);
    for (unsigned i = 0; i < LUT_TEXELS; i++) {
        lut[i] = ((float)i + 0.5f) / LUT_TEXELS;
        lut[LUT_TEXELS + i] = 1 - ((float)i + 0.5f) / LUT_TEXELS;
        lut[2 * LUT_TEXELS + i] = 0.25f;
    }
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE);
    image_descriptor(table, at(LUT), 4, 7, LUT_TEXELS, 1);                   /* 32 float */
    table[3] = 4u | 0u << 3 | 0u << 6 | 1u << 9 | LINEAR_ALIGNED << 20 | 12u << 28; /* DST_SEL x, 0, 0, 1; 1D array */
    table[4] = (LUT_TEXELS - 1) << 13 | 2;                                    /* PITCH, 3 layers */
    table[5] = 2u << 13;                                                      /* layers 0 to 2 */
    memcpy((void *)(uintptr_t)at(LOOKUP_CODE), pixel_lookup, sizeof pixel_lookup);
    static const uint32_t bilinear_clamped[4] = {0x2 | 0x2 << 3 | 0x2 << 6, 0, 1u << 20 | 1u << 22, 0};
    static const uint32_t point_clamped[4] = {0x2 | 0x2 << 3 | 0x2 << 6, 0, 0, 0};
    static const uint32_t bilinear_wrapped[4] = {0, 0, 1u << 20 | 1u << 22, 0};
    static const uint32_t unequal[4] = {0x2, 0, 1u << 20, 0};
    static const uint32_t border[4] = {0x6, 0, 1u << 20 | 1u << 22, 0};
    /* MAX_ANISO_RATIO 4 with bilinear filters: the same as bilinear (the ratio shapes only the
       anisotropic filters); anisotropic bilinear filters (3) with ratio 4 (16 samples) magnify as the
       bilinear one, a ratio past 4 stops, as does a depth compare */
    static const uint32_t ratio_bilinear[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 4u << 9, 0, 1u << 20 | 1u << 22, 0};
    static const uint32_t anisotropic[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 4u << 9, 0, 3u << 20 | 3u << 22, 0};
    static const uint32_t past_ratio[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 5u << 9, 0, 3u << 20 | 3u << 22, 0};
    static const uint32_t depth_compare[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 3u << 12, 0, 1u << 20 | 1u << 22, 0};
    const uint32_t *const samplers[] = {bilinear_clamped, point_clamped, bilinear_wrapped, unequal, border, ratio_bilinear, anisotropic,
                                        depth_compare, past_ratio};
    for (int k = 0; k < 9; k++) {
        uint32_t user[16] = {0};
        user[2] = (uint32_t)at(TABLE), user[3] = (uint32_t)(at(TABLE) >> 32);
        image_descriptor(user + 4, at(IMAGE), 10, 0, WIDTH, HEIGHT);
        user[7] |= LINEAR_ALIGNED << 20;
        memcpy(user + 12, samplers[k], 4 * sizeof *user);
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(LOOKUP_BUFFER, at(LOOKUP_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(LOOKUP_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302,
                                                       0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(LOOKUP_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, LOOKUP_BUFFER, record_triangle_draw(LOOKUP_BUFFER, offset + 0x30), error, sizeof error);
        if ((k < 3 || k == 5 || k == 6) && drawn) fprintf(stderr, "lookup %d: %s\n", k, error);
        if (k < 3 || k == 5 || k == 6) CHECK(drawn == 0);
        if (k == 0 || k == 5 || k == 6) {
            const int misses = lookup_misses(x, at(LOOKUP_TARGET));
            if (misses) fprintf(stderr, "lookup: %d pixels differ\n", misses);
            CHECK(misses == 0);
        }
        if (k == 1 || k == 2) CHECK(lookup_misses(x, at(LOOKUP_TARGET)) > 0);
        if (k == 3) CHECK(drawn == 0 && lookup_misses(x, at(LOOKUP_TARGET)) == 0); /* magnified: the bilinear filter */
        if (k == 4) CHECK(drawn == -1 && strstr(error, "clamp mode 6"));
        if (k == 8) CHECK(drawn == -1 && strstr(error, "MAX_ANISO_RATIO 5"));
        if (k == 7) CHECK(drawn == -1 && strstr(error, "depth compares"));
    }
}

/* Every pixel of a 16 x 16 target is the triangle's colour (red, green, 0.25, 1) put through f: the
   number of pixels that differ. */
static int blended_misses(rt_executor *x, uint64_t target, void (*f)(float rgba[4]))
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int misses = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            float c[4] = {((float)px + 0.5f) / SIZE, ((float)y + 0.5f) / SIZE, 0.25f, 1};
            f(c);
            const uint32_t p = bgra[y * SIZE + px];
            misses += !(near(p >> 16 & 0xff, c[0]) && near(p >> 8 & 0xff, c[1]) && near(p & 0xff, c[2]) && near(p >> 24, c[3]));
        }
    return misses;
}

static void doubled(float c[4]) { for (int i = 0; i < 4; i++) c[i] = fminf(2 * c[i], 1); }
static void halved(float c[4]) { for (int i = 0; i < 4; i++) c[i] *= 0.5f; }
static void colour_halved_alpha_kept(float c[4]) { for (int i = 0; i < 3; i++) c[i] *= 0.5f; }

/* Blending (CB_BLEND0_CONTROL; Mesa's sid.h numbers): the triangle drawn over itself with ONE + ONE
   doubles it (saturated); over a new (zero) target with CONSTANT_COLOR and ONE, and the constant
   (0.5, 0.5, 0.5, 0.5) in CB_BLEND_RED-ALPHA, halves it; with SEPARATE_ALPHA_BLEND and alpha's own
   ONE, ZERO, alpha stays 1 while colour halves. */
static void blending(rt_executor *x)
{
    enum { BLEND_BUFFER = 0xc0f30000, DOUBLE = 0x900000, HALF = 0x940000, SEPARATE = 0x980000, ONE = 1, CONSTANT_COLOR = 13, ZERO = 0 };
    char error[200] = "";
    const uint32_t enable = 1u << 30, separate = 1u << 29;
    const uint32_t half = 0x3f000000;  /* 0.5f */
    /* target 1's blending (CB_BLEND1_CONTROL) changes nothing while its slot is off */
    const uint32_t plain[][2] = {{0xa1e0, 0}, {0xa1e1, enable | ONE | ONE << 8}};
    CHECK(run_with_registers(x, BLEND_BUFFER, at(DOUBLE), plain, 2, 0, error, sizeof error) == 0);
    const uint32_t add[][2] = {{0xa1e0, enable | ONE | ONE << 8}};
    const int doubled_run = run_with_registers(x, BLEND_BUFFER, at(DOUBLE), add, 1, 0, error, sizeof error);
    if (doubled_run) fprintf(stderr, "blend: %s\n", error);
    CHECK(doubled_run == 0);
    CHECK(blended_misses(x, at(DOUBLE), doubled) == 0);
    const uint32_t constant[][2] = {{0xa105, half}, {0xa106, half}, {0xa107, half}, {0xa108, half}, {0xa1e0, enable | CONSTANT_COLOR | ONE << 8}};
    CHECK(run_with_registers(x, BLEND_BUFFER, at(HALF), constant, 5, 0, error, sizeof error) == 0);
    CHECK(blended_misses(x, at(HALF), halved) == 0);
    const uint32_t alpha_apart[][2] = {{0xa105, half}, {0xa106, half}, {0xa107, half}, {0xa108, half},
                                       {0xa1e0, enable | separate | CONSTANT_COLOR | ONE << 8 | ONE << 16 | ZERO << 24}};
    CHECK(run_with_registers(x, BLEND_BUFFER, at(SEPARATE), alpha_apart, 5, 0, error, sizeof error) == 0);
    CHECK(blended_misses(x, at(SEPARATE), colour_halved_alpha_kept) == 0);
}

/* Draws in one buffer run as one batch, each after the last: the triangle drawn twice with ONE + ONE
   into a new (zero) target, in one buffer, doubles it: the second draw blends over the first's output
   with no wait between them. */
static void draws_in_one_batch(rt_executor *x)
{
    enum { BATCH_BUFFER = 0xc0f40000, TWICE = 0x9c0000, ONE = 1 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(BATCH_BUFFER, at(TWICE), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_register(BATCH_BUFFER, offset, 0xa1e0, 1u << 30 | ONE | ONE << 8, error, sizeof error) == 0);
    offset = record_triangle_draw(BATCH_BUFFER, offset + 0x10);
    const int ran = rt_executor_run(x, BATCH_BUFFER, record_triangle_draw(BATCH_BUFFER, offset), error, sizeof error);
    if (ran) fprintf(stderr, "draws in one batch: %s\n", error);
    CHECK(ran == 0);
    CHECK(blended_misses(x, at(TWICE), doubled) == 0);
}

/* Two buffers recorded without submitting (rt_executor_record_calls) join one open batch: both name the
   same ticket, the next one, and once rt_executor_submit hands the batch over and that ticket has run,
   the target holds both blended draws (the second buffer's draw keeps the first's state). */
static void buffers_in_one_submission(rt_executor *x)
{
    enum { FIRST_BUFFER = 0xc1020000, SECOND_BUFFER = 0xc1030000, JOINED = 0x2f80000, ONE = 1 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    CHECK(rt_executor_finish(x, error, sizeof error) == 0);
    uint32_t offset = record_setup(FIRST_BUFFER, at(JOINED), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_register(FIRST_BUFFER, offset, 0xa1e0, 1u << 30 | ONE | ONE << 8, error, sizeof error) == 0);
    const uint32_t first_size = record_triangle_draw(FIRST_BUFFER, offset + 0x10);
    const uint32_t second_size = record_triangle_draw(SECOND_BUFFER, 0x10);
    size_t count = 0;
    uint64_t first_ticket = 0, second_ticket = 0;
    const rt_render_call *calls = rt_render_submitted(FIRST_BUFFER, first_size, &count);
    CHECK(rt_executor_record_calls(x, calls, count, &first_ticket, error, sizeof error) == 0);
    calls = rt_render_submitted(SECOND_BUFFER, second_size, &count);
    CHECK(rt_executor_record_calls(x, calls, count, &second_ticket, error, sizeof error) == 0);
    CHECK(first_ticket && first_ticket == second_ticket);
    CHECK(rt_executor_submit(x, error, sizeof error) == 0);
    CHECK(rt_executor_wait_ticket(x, second_ticket, error, sizeof error) == 0);
    CHECK(blended_misses(x, at(JOINED), doubled) == 0);
}

/* Runs the triangle's state with these registers and a draw of `count` vertices (record_setup's). */
static int run_counted(rt_executor *x, uint64_t buffer, uint64_t target, const uint32_t (*registers)[2], int n, uint32_t count, char *error,
                       size_t error_size)
{
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    for (int i = 0; i < n; i++, offset += 0x10) CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    CHECK(rt_render_record_draw_auto(buffer, offset, count, error, error_size) == 0);
    return rt_executor_run(x, buffer, offset + 0x10, error, error_size);
}

/* Whether a target that was drawn into holds no pixel at all (every fragment failed its test). */
static int nothing_drawn(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return 0;
    for (int i = 0; i < SIZE * SIZE; i++)
        if (bgra[i]) return 0;
    return 1;
}

/* Draws the triangle (its second and third corners swapped when reversed) with pixel_face_float, or
   pixel_face_bits and SPI_BARYC_CNTL's FRONT_FACE_ALL_BITS when all_bits; the run's result. */
static int draw_facing(rt_executor *x, uint64_t target, int all_bits, int reversed, char *error, size_t error_size)
{
    enum { FACE_BUFFER = 0xc0fc0000, FACE_CODE = 0x3000, FRONT_FACE = 0x1000 };
    const uint32_t *program = all_bits ? pixel_face_bits : pixel_face_float;
    memcpy((void *)(uintptr_t)at(FACE_CODE), program, all_bits ? sizeof pixel_face_bits : sizeof pixel_face_float);
    float (*corners)[4] = (float (*)[4])(uintptr_t)at(CORNERS);
    float swap[4];
    if (reversed) memcpy(swap, corners[1], sizeof swap), memcpy(corners[1], corners[2], sizeof swap), memcpy(corners[2], swap, sizeof swap);
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t offset = record_setup(FACE_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(FACE_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 0, 0, 0,
                                                   PERSP_CENTER | FRONT_FACE, PERSP_CENTER | FRONT_FACE, 1, all_bits ? 1u << 24 : 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(FACE_BUFFER, offset, pixel, error, error_size) == 0);
    const int ran = rt_executor_run(x, FACE_BUFFER, record_triangle_draw(FACE_BUFFER, offset + 0x10), error, error_size);
    if (reversed) memcpy(swap, corners[1], sizeof swap), memcpy(corners[1], corners[2], sizeof swap), memcpy(corners[2], swap, sizeof swap);
    return ran;
}

/* The pixel shader's front-face input (SPI_PS_INPUT_ENA bit 12): with PA_SU_SC_MODE_CNTL at its reset
   state counter-clockwise triangles face front (Vulkan's area rule, in framebuffer coordinates). The
   test triangle, (0, 0), (32, 0), (0, 32) on the 16 x 16 target with y down, faces back: its face VGPR is
   -1.0 as a float (red 0) and 0 with FRONT_FACE_ALL_BITS; reversed it faces front: 1.0, and 1. */
static void front_face(rt_executor *x)
{
    enum { FACE_TARGET = 0x1a40000 };
    char error[200] = "";
    for (int all_bits = 0; all_bits < 2; all_bits++)
        for (int reversed = 0; reversed < 2; reversed++) {
            const int ran = draw_facing(x, at(FACE_TARGET), all_bits, reversed, error, sizeof error);
            if (ran) fprintf(stderr, "front face: %s\n", error);
            CHECK(ran == 0);
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(FACE_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            int all = 1;
            for (int i = 0; i < SIZE * SIZE; i++) all &= bgra[i] == (reversed ? 0xffff4040u : 0xff004040u);
            if (!all) fprintf(stderr, "front face (all bits %d, reversed %d): %08x\n", all_bits, reversed, bgra[0]);
            CHECK(all);
        }
}

/* Draws pixel_color_two_targets with colour targets in slots 0 and 2 (none in 1), CB_TARGET_MASK mask
   and the pixel shader's CB_SHADER_MASK shader_mask; the run's result. A slot-2 target of another size
   when odd_size. */
static int draw_two_targets(rt_executor *x, uint64_t first, uint64_t third, uint32_t mask, uint32_t shader_mask, int odd_size, char *error,
                            size_t error_size)
{
    enum { TWO_BUFFER = 0xc0f80000, TWO_CODE = 0x2e00 };
    memcpy((void *)(uintptr_t)at(TWO_CODE), pixel_color_two_targets, sizeof pixel_color_two_targets);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(TWO_BUFFER, first, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(TWO_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 0, 0, 0x404,
                                                   PERSP_CENTER, PERSP_CENTER, 1, 0, 0, shader_mask};
    CHECK(rt_render_record_pixel_shader(TWO_BUFFER, offset, pixel, error, error_size) == 0);
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, third, COLOR_8_8_8_8, SWAP_STD);
    if (odd_size) words[1] = 2 * SIZE / 8 - 1, words[2] = 4 * SIZE * SIZE / 64 - 1;
    CHECK(rt_render_record_color_target(TWO_BUFFER, offset + 0x10, 2, words, error, error_size) == 0);
    CHECK(rt_render_record_register(TWO_BUFFER, offset + 0x20, 0xa08e, mask, error, error_size) == 0);
    return rt_executor_run(x, TWO_BUFFER, record_triangle_draw(TWO_BUFFER, offset + 0x30), error, error_size);
}

/* Several colour targets in one draw (MRT): the pixel shader's mrt0 and mrt2 exports land in the
   targets of slots 0 and 2, with no target in slot 1 between them; CB_TARGET_MASK's nibble per slot
   masks channels: 0x7 on slot 2 leaves its alpha as it was (0), and slot 0 masked off entirely draws
   only slot 2. A slot the pixel shader's CB_SHADER_MASK leaves out is not written either, whatever
   CB_TARGET_MASK says: with shader mask 0x00f slot 2's target is never drawn into (Vulkan would leave
   it undefined). Colour targets of different sizes in one draw stop by name. */
static void multiple_targets(rt_executor *x)
{
    enum { MRT_FIRST = 0x1900000, MRT_THIRD = 0x1940000, MASKED_FIRST = 0x1980000, MASKED_THIRD = 0x19c0000, ODD_THIRD = 0x1a00000,
           SHADER_FIRST = 0x2100000, SHADER_THIRD = 0x2140000 };
    char error[200] = "";
    int drawn = draw_two_targets(x, at(MRT_FIRST), at(MRT_THIRD), 0xf0f, 0xf0f, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "two targets: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(MRT_FIRST));
    check_triangle(x, at(MRT_THIRD));
    drawn = draw_two_targets(x, at(MASKED_FIRST), at(MASKED_THIRD), 0x700, 0xf0f, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "masked targets: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(MASKED_FIRST), bgra, SIZE, SIZE, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(rt_executor_read_target(x, at(MASKED_THIRD), bgra, SIZE, SIZE, error, sizeof error) == 0);
    for (int i = 0; i < SIZE * SIZE; i++) bgra[i] |= 0xffu << 24; /* alpha was masked: 0, as the memory held */
    check_triangle_pixels(bgra);
    CHECK(rt_executor_read_target(x, at(MASKED_THIRD), bgra, SIZE, SIZE, error, sizeof error) == 0 && (bgra[0] >> 24) == 0);
    drawn = draw_two_targets(x, at(SHADER_FIRST), at(SHADER_THIRD), 0xf0f, 0x00f, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "shader-masked targets: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(SHADER_FIRST));
    CHECK(rt_executor_read_target(x, at(SHADER_THIRD), bgra, SIZE, SIZE, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(draw_two_targets(x, at(MRT_FIRST), at(ODD_THIRD), 0xf0f, 0xf0f, 1, error, sizeof error) == -1 && strstr(error, "different sizes"));
}

/* A render target cleared by a compute shader (as the game clears its G-buffer): a dispatch stores
   (0.25, 0.5, 0.75, 1) over a 16 x 16 8_8_8_8 image at the target's memory, then (in the next buffer)
   a draw into the target
   that keeps what is there (blend ZERO + ONE) leaves the cleared colour, which reaches the memory too;
   a clear by a 32_32_32_32 float image there stops the draw by name. */
static void compute_cleared_target(rt_executor *x)
{
    enum { CLEAR_BUFFER = 0xc0fd0000, DRAW_BUFFER = 0xc0fe0000, CLEAR_CODE = 0x3200, CLEARED = 0x1a80000, WIDE_CLEARED = 0x1ac0000, LINEAR_ALIGNED = 8 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(CLEAR_CODE), image_clear_tile, sizeof image_clear_tile);
    for (int wide = 0; wide < 2; wide++) {
        const uint64_t target = at(wide ? WIDE_CLEARED : CLEARED);
        memset((void *)(uintptr_t)target, 0, SIZE * SIZE * 16);
        rt_render_forget(), rt_executor_reset(x);
        const uint64_t code = at(CLEAR_CODE);
        const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1, SIZE, SIZE, 1};
        CHECK(rt_render_record_compute_shader_at(CLEAR_BUFFER + 0x10, shader) == 0);
        uint32_t user[8];
        image_descriptor(user, target, wide ? 14 : 10, wide ? 7 : 0, SIZE, SIZE); /* 32_32_32_32 float, or 8_8_8_8 unorm */
        user[3] |= LINEAR_ALIGNED << 20;
        CHECK(rt_render_record_user_data(CLEAR_BUFFER, 0x20, RT_STAGE_COMPUTE, 8, user, 8, error, sizeof error) == 0);
        const uint32_t one[3] = {1, 1, 1};
        CHECK(rt_render_record_dispatch(CLEAR_BUFFER, 0x30, one, error, sizeof error) == 0);
        const int cleared_run = rt_executor_run(x, CLEAR_BUFFER, 0x40, error, sizeof error);
        if (cleared_run) fprintf(stderr, "compute clear: %s\n", error);
        CHECK(cleared_run == 0);
        /* the draw in the next buffer: the executor keeps its images and targets between buffers */
        rt_render_forget();
        const uint32_t offset = record_setup(DRAW_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_register(DRAW_BUFFER, offset, 0xa1e0, 1u << 30 | 0u | 1u << 8, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, DRAW_BUFFER, record_triangle_draw(DRAW_BUFFER, offset + 0x10), error, sizeof error);
        if (wide) {
            CHECK(ran == -1 && strstr(error, "another format"));
            continue;
        }
        if (ran) fprintf(stderr, "compute-cleared target: %s\n", error);
        CHECK(ran == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error) == 0);
        /* blue first for the display: red 0.25, green 0.5, blue 0.75, alpha 1 (the store's rounding of 0.5 to 127 or 128 either way) */
        int cleared = 1;
        for (int i = 0; i < SIZE * SIZE; i++)
            cleared &= near(bgra[i] >> 16 & 0xff, 0.25f) && near(bgra[i] >> 8 & 0xff, 0.5f) && near(bgra[i] & 0xff, 0.75f) && bgra[i] >> 24 == 0xff;
        if (!cleared) fprintf(stderr, "compute-cleared target: %08x\n", bgra[0]);
        CHECK(cleared);
        const uint32_t first = *(const uint32_t *)(uintptr_t)target; /* written back to the memory: red first */
        CHECK(near(first & 0xff, 0.25f) && near(first >> 8 & 0xff, 0.5f) && near(first >> 16 & 0xff, 0.75f) && first >> 24 == 0xff);
    }
}

/* Fast clears: a target with CB_COLOR_INFO's FAST_CLEAR whose 128-byte CMASK (TILE_MAX 0) is all 0 is
   cleared to CB_COLOR0_CLEAR_WORD0 (0xff336699: red 0x99 first) before a draw that keeps what is there
   (blend ZERO + ONE), and its CMASK becomes all ones; the next draw (the triangle) draws over it as
   usual; a CMASK filled with another value (0x10 bytes, as the game's clear pass leaves it) is cleared
   the same way; a mixed CMASK stops by name. */
static void fast_clears(rt_executor *x)
{
    enum { CLEAR_BUFFER = 0xc0fe8000, FAST = 0x1d00000, CMASK = 0x1d80000, CLEAR_WORD0 = 0xa323 };
    enum { CLEARED_TO_ZERO, DRAWN_OVER, CLEARED_TO_0X10, HALF_CLEARED, PASSES };
    char error[200] = "";
    uint8_t *cmask = (uint8_t *)(uintptr_t)at(CMASK);
    for (int pass = 0; pass < PASSES; pass++) {
        if (pass == CLEARED_TO_ZERO) memset(cmask, 0, 128);
        if (pass == CLEARED_TO_0X10) memset(cmask, 0x10, 128);
        if (pass == HALF_CLEARED) memset(cmask, 0, 64); /* the rest still all ones */
        const int keeps = pass == CLEARED_TO_ZERO || pass == CLEARED_TO_0X10;
        rt_render_forget();
        if (pass == CLEARED_TO_ZERO) rt_executor_reset(x);
        uint32_t offset = record_setup(CLEAR_BUFFER, at(FAST), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, at(FAST), COLOR_8_8_8_8, SWAP_STD);
        words[4] |= 1u << 13, words[7] = (uint32_t)(at(CMASK) >> 8); /* FAST_CLEAR, CMASK */
        CHECK(rt_render_record_color_target(CLEAR_BUFFER, offset, 0, words, error, sizeof error) == 0);
        CHECK(rt_render_record_register(CLEAR_BUFFER, offset + 0x10, CLEAR_WORD0, 0xff336699u, error, sizeof error) == 0);
        CHECK(rt_render_record_register(CLEAR_BUFFER, offset + 0x20, 0xa1e0, keeps ? (1u << 30 | 0u | 1u << 8) : 0, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, CLEAR_BUFFER, record_triangle_draw(CLEAR_BUFFER, offset + 0x30), error, sizeof error);
        if (pass == HALF_CLEARED) {
            CHECK(ran == -1 && strstr(error, "partly fast-cleared"));
            break;
        }
        if (ran) fprintf(stderr, "fast clear pass %d: %s\n", pass, error);
        CHECK(ran == 0);
        if (keeps) {
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(FAST), bgra, SIZE, SIZE, error, sizeof error) == 0);
            int cleared = 1, expanded = 1;
            for (int i = 0; i < SIZE * SIZE; i++) cleared &= bgra[i] == 0xff996633u;
            for (int i = 0; i < 128; i++) expanded &= cmask[i] == 0xff;
            if (!cleared) fprintf(stderr, "fast clear pass %d: %08x\n", pass, bgra[0]);
            CHECK(cleared && expanded);
        } else {
            check_triangle(x, at(FAST));
        }
    }
}

/* A dispatch storing to a target's CMASK through a buffer descriptor based there (a metadata clear, as
   shadPS4 takes one) runs before the CPU reads that CMASK for the next draw's fast clear in the same
   batch: scale_add's 1.0 floats (64 of them, from zeros) leave the CMASK mixed rather than one fill,
   which stops the draw by name. Read before the dispatch had run, the CMASK of all ones would let the
   draw go ahead. */
static void metadata_store_waits(rt_executor *x)
{
    enum { BUFFER = 0xc0fec000, COMPUTE_CODE = 0x3000, TARGET_AT = 0x1da0000, CMASK = 0x1de0000, ZEROS = 0x1de1000, LANES = 64 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(COMPUTE_CODE), scale_add, sizeof scale_add);
    memset((void *)(uintptr_t)at(CMASK), 0xff, LANES * 4);
    memset((void *)(uintptr_t)at(ZEROS), 0, LANES * 4);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(BUFFER, at(TARGET_AT), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(COMPUTE_CODE);
    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, 8 << 1, LANES, 1, 1};
    CHECK(rt_render_record_compute_shader_at(BUFFER + offset, shader) == 0);
    uint32_t user[8];
    descriptor(user, at(ZEROS), 4);
    descriptor(user + 4, at(CMASK), 4);
    CHECK(rt_render_record_user_data(BUFFER, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
    const uint32_t one[3] = {1, 1, 1};
    CHECK(rt_render_record_dispatch(BUFFER, offset + 0x20, one, error, sizeof error) == 0);
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, at(TARGET_AT), COLOR_8_8_8_8, SWAP_STD);
    words[4] |= 1u << 13, words[7] = (uint32_t)(at(CMASK) >> 8); /* FAST_CLEAR, CMASK */
    CHECK(rt_render_record_color_target(BUFFER, offset + 0x30, 0, words, error, sizeof error) == 0);
    const int ran = rt_executor_run(x, BUFFER, record_triangle_draw(BUFFER, offset + 0x40), error, sizeof error);
    if (ran == 0 || !strstr(error, "partly fast-cleared")) fprintf(stderr, "metadata store: %d %s\n", ran, error);
    CHECK(ran == -1 && strstr(error, "partly fast-cleared"));
}

/* Depth and stencil without a depth buffer, and triangle strips:
   - with the depth target switched off (DB_Z_INFO and DB_STENCIL_INFO 0, as the library's setter
     writes for no target), depth and stencil tests draw as if off; Z_WRITE_ENABLE alone writes nothing
     and draws too;
   - a test with no depth target set at all stops by name;
   - colour writes off (CB_COLOR_CONTROL mode CB_DISABLE) draw nothing;
   - four corners as a triangle strip (VGT_PRIMITIVE_TYPE 6) cover the target with the reference image;
     drawn as a list (mutant) they leave half of it empty. */
static void depth_and_strips(rt_executor *x)
{
    uint32_t bgra[4];
    enum { DEPTH_BUFFER = 0xc0f40000, NO_BUFFER = 0xa00000, WRITE_ONLY = 0xa40000, OFF = 0xa80000, STRIP = 0xac0000, LIST = 0xb00000 };
    char error[200] = "";
    const uint32_t no_target[][2] = {{0xa010, 0}, {0xa011, 0}, {0xa200, 0x2 | 0x1 | 0x4 | 0x7u << 4 | 0x7u << 8}};
    int drawn = run_counted(x, DEPTH_BUFFER, at(NO_BUFFER), no_target, 3, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "no depth buffer: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(NO_BUFFER));
    const uint32_t write_only[][2] = {{0xa200, 0x4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(WRITE_ONLY), write_only, 1, 3, error, sizeof error) == 0);
    check_triangle(x, at(WRITE_ONLY));
    /* A 16 x 16 depth target (Z_32_FLOAT with 8-bit stencil; DB_DEPTH_SIZE in eighths, less one):
       - the game's stencil pass (target mask 0, stencil always, replace on pass, DB_STENCILREFMASK 0xff0000)
         writes only the depth target: drawn, no colour target made;
       - the triangle with the depth test always passing and depth writes, into the colour target too,
         draws the reference image;
       - tests that read the buffer (depth LESS and LESS_EQUAL, stencil EQUAL) compare with what earlier
         draws wrote; add-and-wrap steps by 1; one reading a depth target no draw wrote, an add stepping
         by another STENCILOPVAL, a depth target smaller than the colour target (a larger one draws),
         another stencil operation, depth bounds,
         DB_RENDER_CONTROL's clears, or a missing DB_STENCILREFMASK stop by name. */
    enum { DEPTH = 0xb40000, DEPTH_COLOUR = 0xb80000, STENCIL_ONLY = 0xbc0000 };
    const uint32_t depth_size = (SIZE / 8 - 1) | (SIZE / 8 - 1) << 11, z = (uint32_t)(at(DEPTH) >> 8), st = (uint32_t)(at(DEPTH + 0x2000) >> 8);
#define DEPTH_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, z}, {0xa013, st}, {0xa014, z}, {0xa015, st}, {0xa016, depth_size}, \
                     {0xa017, 0}, {0xa002, 0}
    const uint32_t stencil_pass[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0xff0000}, {0xa10b, 0x30030}, {0xa200, 0x700771}};
    drawn = run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), stencil_pass, 13, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "stencil pass: %s\n", error);
    CHECK(drawn == 0);
    CHECK(rt_executor_read_target(x, at(STENCIL_ONLY), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    const uint32_t depth_always[][2] = {DEPTH_TARGET, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
    drawn = run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), depth_always, 10, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "depth always: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(DEPTH_COLOUR));
    /* Tests that read the buffer, after draws wrote it: the depth-always draw left the triangle's depth,
       so LESS on the same triangle passes nowhere and LESS_EQUAL everywhere it covers. A stencil pass
       writing 0x7f under the triangle (test value 0x7f, masks 0xff, replace on pass; colour masked
       off), then stencil EQUAL 0x7f draws the triangle; EQUAL 0x01 draws nothing. */
    enum { LESS = 0x1400000, LESS_EQUAL = 0x1440000, EQUAL_HIT = 0x1480000, EQUAL_MISS = 0x14c0000, UNWRITTEN = 0x1500000 };
    const uint32_t depth_less[][2] = {DEPTH_TARGET, {0xa200, 0x2 | 0x1u << 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(LESS), depth_less, 10, 3, error, sizeof error) == 0);
    CHECK(nothing_drawn(x, at(LESS)));
    const uint32_t depth_less_equal[][2] = {DEPTH_TARGET, {0xa200, 0x2 | 0x3u << 4}};
    drawn = run_counted(x, DEPTH_BUFFER, at(LESS_EQUAL), depth_less_equal, 10, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "depth less or equal: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(LESS_EQUAL));
    const uint32_t mark[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0xffff7f}, {0xa10b, 0x30030}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), mark, 13, 3, error, sizeof error) == 0);
    const uint32_t stencil_equal[][2] = {DEPTH_TARGET, {0xa10c, 0xffff7f}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
    drawn = run_counted(x, DEPTH_BUFFER, at(EQUAL_HIT), stencil_equal, 12, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "stencil equal: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(EQUAL_HIT));
    const uint32_t stencil_other[][2] = {DEPTH_TARGET, {0xa10c, 0xffff01}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(EQUAL_MISS), stencil_other, 12, 3, error, sizeof error) == 0);
    CHECK(nothing_drawn(x, at(EQUAL_MISS)));
    /* add-and-wrap stepping by STENCILOPVAL 1 (DB_STENCILREFMASK bits 24-31) on pass turns 0x7f into 0x80:
       EQUAL 0x80 then draws the triangle; a step of 2 stops by name */
    enum { INCREMENTED = 0x1540000 };
    const uint32_t step[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x01ffff00}, {0xa10b, 0x80}, {0xa200, 0x1 | 0x7u << 8}};
    drawn = run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), step, 13, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "stencil add and wrap: %s\n", error);
    CHECK(drawn == 0);
    const uint32_t equal_next[][2] = {DEPTH_TARGET, {0xa10c, 0xffff80}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(INCREMENTED), equal_next, 12, 3, error, sizeof error) == 0);
    check_triangle(x, at(INCREMENTED));
    const uint32_t step_two[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x02ffff00}, {0xa10b, 0x80}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), step_two, 13, 3, error, sizeof error) == -1 && strstr(error, "STENCILOPVAL 2"));
    /* a depth target larger than the colour target (24 x 24 under 16 x 16, as the game pads its depth
       target) draws at the colour target's size; a smaller one (8 x 8) stops by name */
    enum { PADDED_DEPTH = 0x1580000, PADDED_COLOUR = 0x15c0000, SMALL_DEPTH = 0x1600000 };
    {
        const uint32_t pz = (uint32_t)(at(PADDED_DEPTH) >> 8), pst = (uint32_t)(at(PADDED_DEPTH + 0x4000) >> 8);
        const uint32_t padded[][2] = {{0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, pz}, {0xa013, pst}, {0xa014, pz}, {0xa015, pst},
                                      {0xa016, 2u | 2u << 11}, {0xa017, 0}, {0xa002, 0}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(PADDED_COLOUR), padded, 10, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "padded depth target: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(PADDED_COLOUR));
        const uint32_t sz = (uint32_t)(at(SMALL_DEPTH) >> 8), sst = (uint32_t)(at(SMALL_DEPTH + 0x1000) >> 8);
        const uint32_t small[][2] = {{0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, sz}, {0xa013, sst}, {0xa014, sz}, {0xa015, sst},
                                     {0xa016, 0}, {0xa017, 0}, {0xa002, 0}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), small, 10, 3, error, sizeof error) == -1 && strstr(error, "smaller"));
    }
    /* A depth fast clear: with TILE_SURFACE_ENABLE and an HTILE whose every word has ZMASK 0 (cleared),
       the depth reads DB_DEPTH_CLEAR (1.0): LESS_EQUAL on a target no draw wrote draws the triangle, and
       the HTILE's words get ZMASK 0xf (the clear is used once). Without the clear the test would read a
       target no draw wrote (a stop). A partly cleared HTILE stops by name. */
    {
        enum { CLEARED_Z = 0x1800000, CLEARED_HTILE = 0x1810000, CLEARED_COLOUR = 0x1820000, HTILE_WORDS = (SIZE / 8) * (SIZE / 8) };
        const uint32_t cz = (uint32_t)(at(CLEARED_Z) >> 8), cst = (uint32_t)(at(CLEARED_Z + 0x2000) >> 8), htile = (uint32_t)(at(CLEARED_HTILE) >> 8);
        uint32_t *words = (uint32_t *)(uintptr_t)at(CLEARED_HTILE);
        for (unsigned i = 0; i < HTILE_WORDS; i++) words[i] = 0xfffffff0u;
#define CLEARED_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, cz}, {0xa013, cst}, {0xa014, cz}, {0xa015, cst}, \
                       {0xa016, depth_size}, {0xa017, 0}, {0xa002, 0}, {0xa005, htile}, {0xa00b, 0x3f800000u}
        const uint32_t less_equal_cleared[][2] = {CLEARED_TARGET, {0xa200, 0x2 | 0x3u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(CLEARED_COLOUR), less_equal_cleared, 12, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth fast clear: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(CLEARED_COLOUR));
        int expanded = 1;
        for (unsigned i = 0; i < HTILE_WORDS; i++) expanded &= words[i] == 0xffffffffu;
        CHECK(expanded);
        words[1] = 0xfffffff0u; /* one tile cleared again, the others not */
        CHECK(run_counted(x, DEPTH_BUFFER, at(CLEARED_COLOUR), less_equal_cleared, 12, 3, error, sizeof error) == -1 &&
              strstr(error, "partly fast-cleared HTILE"));
#undef CLEARED_TARGET
    }
    /* Depth write-back: a 256 x 256 Depth2DThin64 depth target (tile index 0), fast-cleared to 1.0 through
       HTILE, leaves 1.0 in every texel of its memory once a draw testing it (LEQUAL, no depth writes) has
       run; a draw writing the triangle's depth (0) then leaves 0.0 under the triangle (the colour target's
       16 x 16 corner). Without write-backs the memory keeps what it held (0x5a bytes here). */
    {
        enum { WB_Z = 0x1900000, WB_HTILE = 0x1980000, WB_COLOUR = 0x19a0000, WB_SIZE = 256, WB_WORDS = (WB_SIZE / 8) * (WB_SIZE / 8) };
        const uint32_t wz = (uint32_t)(at(WB_Z) >> 8), wst = (uint32_t)(at(WB_Z + 0x40000) >> 8), wh = (uint32_t)(at(WB_HTILE) >> 8);
        const uint32_t wb_size = (WB_SIZE / 8 - 1) | (WB_SIZE / 8 - 1) << 11;
        uint32_t *htile_words = (uint32_t *)(uintptr_t)at(WB_HTILE);
        for (unsigned i = 0; i < WB_WORDS; i++) htile_words[i] = 0xfffffff0u;
        float *depth = (float *)(uintptr_t)at(WB_Z);
        memset(depth, 0x5a, WB_SIZE * WB_SIZE * 4);
#define WB_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, wz}, {0xa013, wst}, {0xa014, wz}, {0xa015, wst}, \
                  {0xa016, wb_size}, {0xa017, 0}, {0xa002, 0}, {0xa005, wh}, {0xa00b, 0x3f800000u}
        const uint32_t clear_only[][2] = {WB_TARGET, {0xa200, 0x2 | 0x3u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(WB_COLOUR), clear_only, 12, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth write-back (clear): %s\n", error);
        CHECK(drawn == 0);
        int ones = 1;
        for (unsigned i = 0; i < WB_SIZE * WB_SIZE; i++) ones &= depth[i] == 1.0f;
        CHECK(ones);
        const uint32_t write_depth[][2] = {WB_TARGET, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(WB_COLOUR), write_depth, 12, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth write-back (draw): %s\n", error);
        CHECK(drawn == 0);
        int zeros = 1;
        for (unsigned i = 0; i < 16 * 16; i++) { /* the colour target's 16 x 16 corner: the draw's area */
            const unsigned px = i % 16, y = i / 16;
            rt_tiling layout;
            char reason[160];
            CHECK(rt_tiling_layout(0, 4, WB_SIZE, WB_SIZE, &layout, reason, sizeof reason) == 0);
            zeros &= *(const float *)((const uint8_t *)depth + rt_tiling_offset(&layout, px, y, 0)) == 0.0f;
        }
        CHECK(zeros);
#undef WB_TARGET
    }
    /* Slices of an array depth target (DB_DEPTH_VIEW SLICE_START = SLICE_MAX): a draw into slice 1
       writes slice 1 only: LESS on slice 1 then draws nothing, a test reading slice 0 meets a slice no
       draw wrote; a view of two slices stops by name. */
    {
        enum { SLICED = 0x1740000, SLICE_COLOUR = 0x1780000, SLICE_ONE = 1u | 1u << 13, SLICES_ZERO_ONE = 0u | 1u << 13 };
        const uint32_t sz2 = (uint32_t)(at(SLICED) >> 8), sst2 = (uint32_t)(at(SLICED + 0x2000) >> 8);
#define SLICED_TARGET(view) {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, sz2}, {0xa013, sst2}, {0xa014, sz2}, {0xa015, sst2}, \
                            {0xa016, depth_size}, {0xa017, 0}, {0xa002, view}
        const uint32_t write_one[][2] = {SLICED_TARGET(SLICE_ONE), {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), write_one, 10, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth slice 1: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(SLICE_COLOUR));
        const uint32_t less_one[][2] = {SLICED_TARGET(SLICE_ONE), {0xa200, 0x2 | 0x1u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), less_one, 10, 3, error, sizeof error) == 0);
        const uint32_t less_zero[][2] = {SLICED_TARGET(0), {0xa200, 0x2 | 0x1u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), less_zero, 10, 3, error, sizeof error) == -1 &&
              strstr(error, "no draw has written"));
        const uint32_t two_slices[][2] = {SLICED_TARGET(SLICES_ZERO_ONE), {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), two_slices, 10, 3, error, sizeof error) == -1 &&
              strstr(error, "more than one slice"));
#undef SLICED_TARGET
    }
    /* A depth-only pass (no pixel shader, target mask 0, depth always with writes) into a fresh depth
       target leaves the triangle's depth: LESS on the same triangle then draws nothing, LESS_EQUAL the
       triangle. */
    {
        enum { DEPTH_ONLY_Z = 0x1640000, ONLY_LESS = 0x1680000, ONLY_LESS_EQUAL = 0x16c0000, NO_TARGET = 0x1700000 };
        const uint32_t oz = (uint32_t)(at(DEPTH_ONLY_Z) >> 8), ost = (uint32_t)(at(DEPTH_ONLY_Z + 0x2000) >> 8);
#define DEPTH_ONLY_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, oz}, {0xa013, ost}, {0xa014, oz}, {0xa015, ost}, \
                          {0xa016, depth_size}, {0xa017, 0}, {0xa002, 0}
        const uint32_t prepass[][2] = {DEPTH_ONLY_TARGET, {0xa08e, 0}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        rt_render_forget(), rt_executor_reset(x);
        uint32_t at_offset = record_setup(DEPTH_BUFFER, at(NO_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS];
        CHECK(rt_render_record_pixel_shader_at(DEPTH_BUFFER + at_offset, no_pixel_shader) == 0);
        at_offset += 0x10;
        for (int i = 0; i < 11; i++, at_offset += 0x10)
            CHECK(rt_render_record_register(DEPTH_BUFFER, at_offset, prepass[i][0], prepass[i][1], error, sizeof error) == 0);
        CHECK(rt_render_record_draw_auto(DEPTH_BUFFER, at_offset, 3, error, sizeof error) == 0);
        /* the colour draws after it, in the same run: the pixel shader placed again by record_setup's state */
        const int prepassed = rt_executor_run(x, DEPTH_BUFFER, at_offset + 0x10, error, sizeof error);
        if (prepassed) fprintf(stderr, "depth-only pass: %s\n", error);
        CHECK(prepassed == 0);
        CHECK(rt_executor_read_target(x, at(NO_TARGET), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
        const uint32_t only_less[][2] = {DEPTH_ONLY_TARGET, {0xa200, 0x2 | 0x1u << 4}};
        const uint32_t only_less_equal[][2] = {DEPTH_ONLY_TARGET, {0xa200, 0x2 | 0x3u << 4}};
        /* run_counted resets the record but not the executor's depth targets: they keep what the pass wrote */
        CHECK(run_counted(x, DEPTH_BUFFER, at(ONLY_LESS), only_less, 10, 3, error, sizeof error) == 0);
        CHECK(nothing_drawn(x, at(ONLY_LESS)));
        drawn = run_counted(x, DEPTH_BUFFER, at(ONLY_LESS_EQUAL), only_less_equal, 10, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "after the depth-only pass: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(ONLY_LESS_EQUAL));
#undef DEPTH_ONLY_TARGET
    }
    /* a test reading a depth target no draw has written stops by name: its memory is never loaded */
    const uint32_t uz = (uint32_t)(at(UNWRITTEN) >> 8), ust = (uint32_t)(at(UNWRITTEN + 0x2000) >> 8);
    const uint32_t unwritten[][2] = {{0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, uz}, {0xa013, ust}, {0xa014, uz}, {0xa015, ust},
                                     {0xa016, depth_size}, {0xa017, 0}, {0xa002, 0}, {0xa200, 0x2 | 0x1u << 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), unwritten, 10, 3, error, sizeof error) == -1 && strstr(error, "no draw has written"));
    /* replace with STENCILOPVAL (operation 4) on pass, the test always: the stencil under the triangle
       becomes 0x7f though STENCILTESTVAL is 0x01, so EQUAL 0x7f draws the triangle and EQUAL 0x01
       nothing; with an EQUAL test on 0x01 beside it, it stops by name */
    {
        enum { OP_REPLACED = 0x17c0000, OP_MISSED = 0x1800000 };
        const uint32_t replace_op[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x7fffff01}, {0xa10b, 0x40}, {0xa200, 0x1 | 0x7u << 8}};
        drawn = run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), replace_op, 13, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "stencil replace with STENCILOPVAL: %s\n", error);
        CHECK(drawn == 0);
        const uint32_t equal_op_value[][2] = {DEPTH_TARGET, {0xa10c, 0xffff7f}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(OP_REPLACED), equal_op_value, 12, 3, error, sizeof error) == 0);
        check_triangle(x, at(OP_REPLACED));
        const uint32_t equal_test_value[][2] = {DEPTH_TARGET, {0xa10c, 0xffff01}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(OP_MISSED), equal_test_value, 12, 3, error, sizeof error) == 0);
        CHECK(nothing_drawn(x, at(OP_MISSED)));
        const uint32_t conflicting[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x7fffff01}, {0xa10b, 0x40}, {0xa200, 0x1 | 0x2u << 8}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), conflicting, 13, 3, error, sizeof error) == -1 &&
              strstr(error, "replace with STENCILOPVAL 127"));
    }
    const uint32_t incremented[][2] = {DEPTH_TARGET, {0xa10c, 0xff0000}, {0xa10b, 0x50}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), incremented, 12, 3, error, sizeof error) == -1 && strstr(error, "stencil operation 5"));
    const uint32_t bounds[][2] = {DEPTH_TARGET, {0xa200, 0x8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), bounds, 10, 3, error, sizeof error) == -1 && strstr(error, "depth bounds"));
    const uint32_t clearing[][2] = {DEPTH_TARGET, {0xa000, 0x1}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), clearing, 11, 3, error, sizeof error) == -1 && strstr(error, "DB_RENDER_CONTROL"));
    const uint32_t no_reference[][2] = {DEPTH_TARGET, {0xa10b, 0x30030}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), no_reference, 11, 3, error, sizeof error) == -1 && strstr(error, "DB_STENCILREFMASK"));
#undef DEPTH_TARGET
    const uint32_t unknown_target[][2] = {{0xa200, 0x1}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(NO_BUFFER), unknown_target, 1, 3, error, sizeof error) == -1 &&
          strstr(error, "no depth target set"));
    const uint32_t colour_off[][2] = {{0xa202, 0x0u << 4 | 0xccu << 16}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(OFF), colour_off, 1, 3, error, sizeof error) == 0);
    CHECK(rt_executor_read_target(x, at(OFF), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));

    float *corners = (float *)(uintptr_t)at(CORNERS), *colors = (float *)(uintptr_t)at(COLORS);
    float saved_corners[12], saved_colors[12];
    memcpy(saved_corners, corners, sizeof saved_corners);
    memcpy(saved_colors, colors, sizeof saved_colors);
    static const float strip_corners[4][4] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}, {1, 1, 0, 1}};
    static const float strip_colors[4][4] = {{0, 0, 0.25f, 1}, {1, 0, 0.25f, 1}, {0, 1, 0.25f, 1}, {1, 1, 0.25f, 1}};
    memcpy(corners, strip_corners, sizeof strip_corners);
    memcpy(colors, strip_colors, sizeof strip_colors);
    const uint32_t strip[][2] = {{0xc242, 6}};
    drawn = run_counted(x, DEPTH_BUFFER, at(STRIP), strip, 1, 4, error, sizeof error);
    if (drawn) fprintf(stderr, "strip: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(STRIP));
    const uint32_t list[][2] = {{0xc242, 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(LIST), list, 1, 3, error, sizeof error) == 0);
    uint32_t pixels[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(LIST), pixels, SIZE, SIZE, error, sizeof error) == 0);
    CHECK(pixels[SIZE * SIZE - 1] == 0); /* the bottom-right corner is the strip's second triangle */
    memcpy(corners, saved_corners, sizeof saved_corners);
    memcpy(colors, saved_colors, sizeof saved_colors);
}

/* A vertex shader calling a fetch shader (vertex_calling_fetch, fetch_position_color): its user data
   holds the fetch shader's address and a pointer to a table of the two vertex buffers' descriptors
   (32_32_32_32 float, DST_SEL x, y, z, w); the triangle draws the reference image. */
static void fetch_shader(rt_executor *x)
{
    enum { FETCH_BUFFER = 0xc0f50000, VS_CODE = 0x3600, FS_CODE = 0x3700, TABLE = 0x11000, FETCH_TARGET = 0xc00000 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(VS_CODE), vertex_calling_fetch, sizeof vertex_calling_fetch);
    memcpy((void *)(uintptr_t)at(FS_CODE), fetch_position_color, sizeof fetch_position_color);
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE);
    const uint32_t float_xyzw = 4 | 5 << 3 | 6 << 6 | 7 << 9 | 7u << 12 | 14u << 15; /* DST_SEL, NUM_FORMAT float, DATA_FORMAT 32_32_32_32 */
    descriptor(table, at(CORNERS), RECORD_STRIDE), table[3] = float_xyzw;
    descriptor(table + 4, at(COLORS), RECORD_STRIDE), table[7] = float_xyzw;
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(FETCH_BUFFER, at(FETCH_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(VS_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 4 << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(FETCH_BUFFER, offset, vertex, error, sizeof error) == 0);
    const uint32_t user[4] = {(uint32_t)at(FS_CODE), (uint32_t)(at(FS_CODE) >> 32), (uint32_t)at(TABLE), (uint32_t)(at(TABLE) >> 32)};
    CHECK(rt_render_record_user_data(FETCH_BUFFER, offset + 0x10, RT_STAGE_VERTEX, 0, user, 4, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, FETCH_BUFFER, record_triangle_draw(FETCH_BUFFER, offset + 0x20), error, sizeof error);
    if (drawn) fprintf(stderr, "fetch shader: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(FETCH_TARGET));
}

/* Every pixel of the 16 x 16 target at `target` is `bgra`. */
static int filled_with(rt_executor *x, uint64_t target, uint32_t bgra)
{
    uint32_t pixels[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, pixels, SIZE, SIZE, error, sizeof error)) return 0;
    for (int i = 0; i < SIZE * SIZE; i++)
        if (pixels[i] != bgra) return 0;
    return 1;
}

/* SPI_PS_INPUT_CNTL_0 (0xa191) says what the pixel shader's attribute 0 reads (gcn_spirv.h): with OFFSET 1
   it is the vertex shader's parameter 1 (vertex_colour_second's colour; without the register it reads
   parameter 0, the position, and the triangle's pixels are wrong), DEFAULT_VAL gives a constant, and
   FLAT_SHADE the first vertex's colour everywhere. */
static void pixel_input_controls(rt_executor *x)
{
    enum { CONTROL_BUFFER = 0xc1010000, VS_CODE = 0x3e00, CONTROL_TARGET = 0x2f00000, SPI_PS_INPUT_CNTL_0 = 0xa191,
           USE_DEFAULT = 0x20, DEFAULT_ONES = 3u << 8, DEFAULT_BLACK = 1u << 8, FLAT_SHADE = 0x400 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(VS_CODE), vertex_colour_second, sizeof vertex_colour_second);
    const uint64_t code = at(VS_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 1 << 1, 0x4, 0};
    for (int mapped = 0; mapped <= 1; mapped++) {
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(CONTROL_BUFFER, at(CONTROL_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_vertex_shader(CONTROL_BUFFER, offset, vertex, error, sizeof error) == 0);
        if (mapped) CHECK(rt_render_record_register(CONTROL_BUFFER, offset + 0x10, SPI_PS_INPUT_CNTL_0, 1, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, CONTROL_BUFFER, record_triangle_draw(CONTROL_BUFFER, offset + 0x20), error, sizeof error);
        if (drawn) fprintf(stderr, "pixel input controls: %s\n", error);
        CHECK(drawn == 0);
        if (mapped) check_triangle(x, at(CONTROL_TARGET));
        else {
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(CONTROL_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            CHECK((bgra[0] & 0xff) == 0); /* the position's z, 0, where the colour's blue is 0.25 */
        }
    }
    const uint32_t white[][2] = {{SPI_PS_INPUT_CNTL_0, USE_DEFAULT | DEFAULT_ONES}};
    CHECK(run_with_registers(x, CONTROL_BUFFER, at(CONTROL_TARGET), white, 1, 0, error, sizeof error) == 0);
    CHECK(filled_with(x, at(CONTROL_TARGET), 0xffffffffu));
    const uint32_t black[][2] = {{SPI_PS_INPUT_CNTL_0, USE_DEFAULT | DEFAULT_BLACK}};
    CHECK(run_with_registers(x, CONTROL_BUFFER, at(CONTROL_TARGET), black, 1, 0, error, sizeof error) == 0);
    CHECK(filled_with(x, at(CONTROL_TARGET), 0xff000000u));
    const uint32_t flat[][2] = {{SPI_PS_INPUT_CNTL_0, FLAT_SHADE}}; /* the first vertex's (0, 0, 0.25, 1) */
    CHECK(run_with_registers(x, CONTROL_BUFFER, at(CONTROL_TARGET), flat, 1, 0, error, sizeof error) == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(CONTROL_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int same = 1;
    for (int i = 0; i < SIZE * SIZE; i++) same &= (bgra[i] >> 16 & 0xff) == 0 && (bgra[i] >> 8 & 0xff) == 0 && near(bgra[i] & 0xff, 0.25f);
    CHECK(same);
}

/* A vertex shader that calls its fetch shader and then loads a texel from an image whose descriptor is in
   its user data (vertex_fetch_then_image: s[8:15], which fetch_position_only leaves alone): the image is
   bound to the vertex stage, and every pixel of the triangle takes that texel's colour (a 1x1 linear
   32_32_32_32 float image holding red). */
static void vertex_image_after_fetch(rt_executor *x)
{
    enum { VERTEX_IMAGE_BUFFER = 0xc0f90000, VS_CODE = 0x3800, FS_CODE = 0x3700, TABLE = 0x11000, TEXEL = 0x12000, VI_TARGET = 0x1e00000,
           LINEAR_ALIGNED = 8, DATA_32_32_32_32 = 14, NUM_FLOAT = 7, USER_SGPRS = 16 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(VS_CODE), vertex_fetch_then_image, sizeof vertex_fetch_then_image);
    memcpy((void *)(uintptr_t)at(FS_CODE), fetch_position_only, sizeof fetch_position_only);
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE);
    const uint32_t float_xyzw = 4 | 5 << 3 | 6 << 6 | 7 << 9 | 7u << 12 | 14u << 15;
    descriptor(table, at(CORNERS), RECORD_STRIDE), table[3] = float_xyzw;
    const float red[4] = {1, 0, 0, 1};
    memcpy((void *)(uintptr_t)at(TEXEL), red, sizeof red);
    memset((void *)(uintptr_t)at(VI_TARGET), 0, SIZE * SIZE * 4);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(VERTEX_IMAGE_BUFFER, at(VI_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(VS_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, USER_SGPRS << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(VERTEX_IMAGE_BUFFER, offset, vertex, error, sizeof error) == 0);
    uint32_t user[USER_SGPRS] = {(uint32_t)at(FS_CODE), (uint32_t)(at(FS_CODE) >> 32), (uint32_t)at(TABLE), (uint32_t)(at(TABLE) >> 32)};
    image_descriptor(user + 8, at(TEXEL), DATA_32_32_32_32, NUM_FLOAT, 1, 1);
    user[8 + 3] |= LINEAR_ALIGNED << 20;
    CHECK(rt_render_record_user_data(VERTEX_IMAGE_BUFFER, offset + 0x10, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(VERTEX_IMAGE_BUFFER, offset + 0x20, RT_STAGE_VERTEX, 8, user + 8, 8, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, VERTEX_IMAGE_BUFFER, record_triangle_draw(VERTEX_IMAGE_BUFFER, offset + 0x30), error, sizeof error);
    if (drawn) fprintf(stderr, "vertex image after fetch: %s\n", error);
    CHECK(drawn == 0);
    uint32_t pixels[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(VI_TARGET), pixels, SIZE, SIZE, error, sizeof error) == 0);
    unsigned covered = 0, red_pixels = 0;
    for (unsigned i = 0; i < SIZE * SIZE; i++) {
        if (!pixels[i]) continue;
        covered++;
        red_pixels += near(pixels[i] >> 16 & 0xff, 1) && near(pixels[i] >> 8 & 0xff, 0) && near(pixels[i] & 0xff, 0);
    }
    CHECK(covered > SIZE * SIZE / 8 && red_pixels == covered);
}

/* Within one buffer, a draw reading a render target's memory as an image sees an earlier draw into it:
   the triangle drawn into a linear target, then (same buffer) each pixel of another target the texel
   under it from that memory, gives the reference image. The target is written back for that read,
   not after every draw. */
static void target_read_in_buffer(rt_executor *x)
{
    enum { READ_BUFFER = 0xc0f70000, DRAWN = 0xe40000, COPIED = 0xe80000, LINEAR_ALIGNED = 8 };
    char error[200] = "";
    memset((void *)(uintptr_t)at(DRAWN), 0, SIZE * SIZE * 4);
    memcpy((void *)(uintptr_t)at(0x2a00), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_triangle_draw(READ_BUFFER, record_setup(READ_BUFFER, at(DRAWN), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER));
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, at(COPIED), COLOR_8_8_8_8, SWAP_STD);
    CHECK(rt_render_record_color_target(READ_BUFFER, offset, 0, words, error, sizeof error) == 0);
    const uint64_t code = at(0x2a00);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(READ_BUFFER, offset + 0x10, pixel, error, sizeof error) == 0);
    uint32_t image[8];
    image_descriptor(image, at(DRAWN), 10, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(rt_render_record_user_data(READ_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 0, image, 8, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, READ_BUFFER, record_triangle_draw(READ_BUFFER, offset + 0x30), error, sizeof error);
    if (drawn) fprintf(stderr, "target read in its buffer: %s\n", error);
    CHECK(drawn == 0);
    CHECK(differing_pixels(x, at(COPIED)) == 0);
}

/* User data a shader uses that no call has set stops the draw by name, rather than running on zeros: a
   pixel shader whose image descriptor is in s[0:7], with only slots 0-3 set, and after a reset none. */
static void unset_user_data(rt_executor *x)
{
    enum { UNSET_BUFFER = 0xc0f60000, UNSET_TARGET = 0xc20000 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(0x2a00), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(UNSET_BUFFER, at(UNSET_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(0x2a00);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(UNSET_BUFFER, offset, pixel, error, sizeof error) == 0);
    const uint32_t half[4] = {0};
    CHECK(rt_render_record_user_data(UNSET_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, half, 4, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, UNSET_BUFFER, record_triangle_draw(UNSET_BUFFER, offset + 0x20), error, sizeof error) == -1 &&
          strstr(error, "a descriptor in s[0:7], user data no call has set"));
    rt_render_forget(), rt_executor_reset(x);
    offset = record_setup(UNSET_BUFFER, at(UNSET_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_pixel_shader(UNSET_BUFFER, offset, pixel, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, UNSET_BUFFER, record_triangle_draw(UNSET_BUFFER, offset + 0x10), error, sizeof error) == -1 &&
          strstr(error, "a descriptor in s[0:7], user data no call has set (slots set: 0)"));
}

/* The GPU's state carries over from one submitted buffer to the next: the triangle's state recorded in
   one buffer, its draw alone in the next, draws the reference image; a slot beyond 0 switched on in one
   buffer and off again in the next no longer stops the draw. rt_executor_reset forgets it all. */
static void state_across_buffers(rt_executor *x)
{
    enum { FIRST = 0xc0f60000, SECOND = 0xc0f70000, ACROSS = 0xc40000 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t first_size = record_setup(FIRST, at(ACROSS), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    uint32_t target_words[RT_COLOR_TARGET_WORDS];
    color_target(target_words, at(ACROSS) + 0x40000, COLOR_8_8_8_8, SWAP_STD);
    CHECK(rt_render_record_color_target(FIRST, first_size, 1, target_words, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, FIRST, first_size + 0x10, error, sizeof error) == 0);
    CHECK(rt_render_record_color_target(SECOND, 0x10, 1, NULL, error, sizeof error) == 0);
    const uint32_t second_size = record_triangle_draw(SECOND, 0x20);
    const int drawn = rt_executor_run(x, SECOND, second_size, error, sizeof error);
    if (drawn) fprintf(stderr, "across buffers: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(ACROSS));
    rt_executor_reset(x);
    rt_render_forget();
    color_target(target_words, at(ACROSS), COLOR_8_8_8_8, SWAP_STD);
    CHECK(rt_render_record_color_target(SECOND, 0x10, 0, target_words, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(SECOND, 0x20, 3, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, SECOND, 0x30, error, sizeof error) == -1 && strstr(error, "no viewport"));
}

/* Runs the triangle's state, then an indexed draw (DRAW_INDEX_OFFSET_2) of three indices from `first`,
   with the index base and type given (type ~0u: none set) and, when size is not 0, INDEX_BUFFER_SIZE. */
static int run_indexed(rt_executor *x, uint64_t target, uint64_t indices, uint32_t type, uint32_t size, uint32_t first, char *error,
                       size_t error_size)
{
    enum { INDEXED_BUFFER = 0xc0f80000 };
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(INDEXED_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_index_base(INDEXED_BUFFER, offset, indices, error, error_size) == 0);
    if (type != ~0u) CHECK(rt_render_record_index_type(INDEXED_BUFFER, offset + 0x10, type, error, error_size) == 0);
    if (size) CHECK(rt_render_record_index_size(INDEXED_BUFFER, offset + 0x20, size, error, error_size) == 0);
    CHECK(rt_render_record_draw_index_offset(INDEXED_BUFFER, offset + 0x30, first, 3, error, error_size) == 0);
    return rt_executor_run(x, INDEXED_BUFFER, offset + 0x40, error, error_size);
}

/* Indexed draws: the triangle's corners through 16-bit indices (9, 9, 9, 0, 1, 2) from index 3, and
   through 32-bit ones (2, 0, 1 wound the other way round still covers it), draw the reference image;
   from index 0 (mutant) they draw nothing. No index type, a swapped index type, and indices past
   INDEX_BUFFER_SIZE stop by name. */
static void indexed_draws(rt_executor *x)
{
    enum { INDICES_16 = 0x12000, INDICES_32 = 0x12100, INDEXED_16 = 0xc80000, INDEXED_32 = 0xcc0000, FROM_ZERO = 0xd00000 };
    char error[200] = "";
    static const uint16_t sixteen[6] = {9, 9, 9, 0, 1, 2};
    static const uint32_t thirty_two[4] = {7, 2, 0, 1};
    memcpy((void *)(uintptr_t)at(INDICES_16), sixteen, sizeof sixteen);
    memcpy((void *)(uintptr_t)at(INDICES_32), thirty_two, sizeof thirty_two);
    int drawn = run_indexed(x, at(INDEXED_16), at(INDICES_16), 0, 0, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "indexed 16: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(INDEXED_16));
    drawn = run_indexed(x, at(INDEXED_32), at(INDICES_32), 1, 4, 1, error, sizeof error);
    if (drawn) fprintf(stderr, "indexed 32: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(INDEXED_32));
    CHECK(run_indexed(x, at(FROM_ZERO), at(INDICES_16), 0, 0, 0, error, sizeof error) == 0);
    uint32_t pixels[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(FROM_ZERO), pixels, SIZE, SIZE, error, sizeof error) == 0);
    int empty = 1;
    for (int i = 0; i < SIZE * SIZE; i++) empty &= pixels[i] == 0;
    CHECK(empty); /* indices 9, 9, 9: a point, no triangle */
    CHECK(run_indexed(x, at(INDEXED_16), at(INDICES_16), ~0u, 0, 3, error, sizeof error) == -1 && strstr(error, "no index base and type"));
    CHECK(run_indexed(x, at(INDEXED_16), at(INDICES_16), 0x4, 0, 3, error, sizeof error) == -1 && strstr(error, "index type 0x4"));
    CHECK(run_indexed(x, at(INDEXED_16), at(INDICES_16), 0, 5, 3, error, sizeof error) == -1 && strstr(error, "past the 5-index buffer"));
}

/* Record a packet draw of the last three indices, including fields that the older
   first/count API cannot express. Each run starts with independent GPU state. */
static int run_index_packet(rt_executor *x, uint64_t target, uint64_t indices, uint32_t saved_size,
                            uint32_t header, uint32_t max_size, uint32_t count, uint32_t initiator,
                            char *error, size_t error_size)
{
    enum { BUFFER = 0xc0fb0000 };
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_index_base(BUFFER, offset, indices, error, error_size) == 0);
    CHECK(rt_render_record_index_type(BUFFER, offset + 0x10, 0, error, error_size) == 0);
    if (saved_size) CHECK(rt_render_record_index_size(BUFFER, offset + 0x20, saved_size, error, error_size) == 0);
    CHECK(rt_render_record_draw_index_offset_packet(BUFFER, offset + 0x30, header, max_size, 3, count,
                                                     initiator, error, error_size) == 0);
    return rt_executor_run(x, BUFFER, offset + 0x40, error, error_size);
}

/* A packet's MAX_SIZE applies independently of INDEX_BUFFER_SIZE. Unsupported
   packet fields stop before drawing; a valid packet draws the complete triangle. */
static void indexed_packets(rt_executor *x, int fd)
{
    enum { INDICES = 0x12200, TARGET_PACKET = 0xd40000 };
    const uint32_t offset_header = 0xc0033500u;
    const uint16_t indices[] = {9, 9, 9, 0, 1, 2};
    memcpy((void *)(uintptr_t)at(INDICES), indices, sizeof indices);
    char error[200] = "";
    CHECK(run_index_packet(x, at(TARGET_PACKET), at(INDICES), 1, offset_header, 3, 3, 0,
                           error, sizeof error) == 0);
    check_triangle(x, at(TARGET_PACKET));
    CHECK(run_index_packet(x, at(TARGET_PACKET), at(INDICES), 0, offset_header, 3, 3, 0,
                           error, sizeof error) == 0);
    check_triangle(x, at(TARGET_PACKET));

    const struct { uint32_t header, max_size, count, initiator; const char *reason; } refused[] = {
        {offset_header | 1, 3, 3, 0, "predicated indexed draws"},
        {0, 3, 3, 0, "unsupported indexed offset packet header"},
        {offset_header, 3, 3, 1, "unsupported indexed offset draw initiator"},
        {offset_header, 2, 3, 0, "beyond MAX_SIZE"},
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        error[0] = '\0';
        CHECK(run_index_packet(x, at(TARGET_PACKET), at(INDICES), 0, refused[i].header,
                               refused[i].max_size, refused[i].count, refused[i].initiator,
                               error, sizeof error) == -1 && strstr(error, refused[i].reason));
    }
    rt_executor_reset(x);
    const rt_render_call bad_tag = {.kind = RT_CALL_DRAW_INDEX_OFFSET,
        .u.draw_index = {.count = 3, .has_packet_fields = 2, .header = offset_header, .max_size = 3}};
    CHECK(rt_executor_run_calls(x, &bad_tag, 1, error, sizeof error) == -1 &&
          strstr(error, "invalid indexed packet field tag"));
    rt_render_forget();
    CHECK(rt_render_record_draw_indexed(0xc0fc0000, 0, 0xc0042700u, 3, at(INDICES), 3, 0,
                                         error, sizeof error) == 0);
    CHECK(rt_executor_run(x, 0xc0fc0000, 0x20, error, sizeof error) == -1 &&
          strstr(error, "DRAW_INDEX_2 is not implemented"));

    /* A low guest window makes the unchecked sum wrap into valid GPU memory.
       With no targets enabled it would be accepted if the overflow guard vanished. */
    const uint64_t low_base = 0x100000000ull;
    const rt_executor_memory low_memory = {fd, GUEST_SIZE, low_base};
    rt_executor *low = NULL;
    const int opened = rt_executor_open(&low_memory, &low, error, sizeof error);
    if (opened) fprintf(stderr, "low index window: %s\n", error);
    CHECK(opened == 0);
    if (low) {
        const rt_render_call overflow[] = {
            {.kind = RT_CALL_INDEX_BASE, .u.index_base = {.address = UINT64_MAX - 5}},
            {.kind = RT_CALL_INDEX_TYPE, .u.index_type = {.type = 0}},
            {.kind = RT_CALL_DRAW_INDEX_OFFSET, .u.draw_index = {
                .first = (uint32_t)(low_base / 2 + 3), .count = 3, .has_packet_fields = 1,
                .header = offset_header, .max_size = 3}},
        };
        CHECK(rt_executor_run_calls(low, overflow, sizeof overflow / sizeof *overflow, error, sizeof error) == -1 &&
              strstr(error, "index base plus offset overflows"));
        rt_executor_close(low);
    }
}

/* A render target's memory stays its contents: after a buffer draws the triangle into a linear target,
   the guest memory under it holds the reference image (red first, as SWAP_STD stores it); a pixel shader
   reading that memory as an image draws the reference image into another target; and when the CPU
   rewrites the memory, the next buffer's draw keeping what is there (blend ZERO + ONE) shows the CPU's
   texels, not the old image. */
static void target_memory(rt_executor *x)
{
    enum { SOURCE = 0xd40000, COPY = 0xd80000 };
    char error[200] = "";
    const uint32_t plain[][2] = {{0xa1e0, 0}};
    CHECK(run_with_registers(x, 0xc0f90000, at(SOURCE), plain, 1, 0, error, sizeof error) == 0);
    const uint32_t *memory = (const uint32_t *)(uintptr_t)at(SOURCE);
    int same = 1;
    for (uint32_t y = 0; y < SIZE; y++)
        for (uint32_t px = 0; px < SIZE; px++) {
            const uint32_t p = memory[y * SIZE + px], want = reference_texel(px, y);
            for (int k = 0; k < 4; k++) same &= abs((int)(p >> (8 * k) & 0xff) - (int)(want >> (8 * k) & 0xff)) <= 2;
        }
    CHECK(same);
    uint32_t image[8];
    image_descriptor(image, at(SOURCE), 10, 0, SIZE, SIZE);
    image[3] |= 8u << 20; /* linear aligned, as the target */
    const int read = draw_texels(x, at(COPY), image, error, sizeof error);
    if (read) fprintf(stderr, "target as image: %s\n", error);
    CHECK(read == 0);
    CHECK(differing_pixels(x, at(COPY)) == 0);
    for (int i = 0; i < SIZE * SIZE; i++) ((uint32_t *)(uintptr_t)at(SOURCE))[i] = 0xff402010u; /* the CPU's texels */
    const uint32_t keep[][2] = {{0xa1e0, 1u << 30 | 0u | 1u << 8}}; /* blend ZERO + ONE: what is there stays */
    CHECK(run_with_registers(x, 0xc0f90000, at(SOURCE), keep, 1, 0, error, sizeof error) == 0);
    uint32_t bgra[4];
    CHECK(rt_executor_read_target(x, at(SOURCE), bgra, 2, 2, error, sizeof error) == 0);
    CHECK(bgra[0] == 0xff102040u); /* the display's blue-first order of red 0x10, green 0x20, blue 0x40 */
}

/* A tiled render target (Thin1DThin, tiling index 13) moves to and from its memory through the GPU's
   tile copies: after a buffer draws the triangle, each texel is where rt_tiling_offset says; a texel the
   CPU then rewrites there shows at its pixel after the next buffer's draw keeps what is there. */
static void tiled_target_memory(rt_executor *x)
{
    enum { TILED_BUFFER = 0xc0fa0000, TILED = 0xec0000, THIN_1D = 13 };
    char error[200] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(THIN_1D, 4, SIZE, SIZE, &layout, error, sizeof error) == 0);
    uint8_t *memory = (uint8_t *)(uintptr_t)at(TILED);
    memset(memory, 0, rt_tiling_size(&layout, 1));
    for (int pass = 0; pass < 2; pass++) {
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(TILED_BUFFER, at(TILED), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, at(TILED), COLOR_8_8_8_8, SWAP_STD);
        words[5] = THIN_1D;
        CHECK(rt_render_record_color_target(TILED_BUFFER, offset, 0, words, error, sizeof error) == 0);
        const uint32_t blend = pass ? 1u << 30 | 0u | 1u << 8 : 0; /* the second pass: ZERO + ONE keeps what is there */
        CHECK(rt_render_record_register(TILED_BUFFER, offset + 0x10, 0xa1e0, blend, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, TILED_BUFFER, record_triangle_draw(TILED_BUFFER, offset + 0x20), error, sizeof error);
        if (ran) fprintf(stderr, "tiled target: %s\n", error);
        CHECK(ran == 0);
        if (pass) break;
        int placed = 1;
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) {
                uint32_t p;
                memcpy(&p, memory + rt_tiling_offset(&layout, px, y, 0), 4);
                const uint32_t want = reference_texel(px, y);
                for (int k = 0; k < 4; k++) placed &= abs((int)(p >> (8 * k) & 0xff) - (int)(want >> (8 * k) & 0xff)) <= 2;
            }
        CHECK(placed);
        const uint32_t cpu = 0xff402010u; /* the CPU's texel at (5, 3) */
        memcpy(memory + rt_tiling_offset(&layout, 5, 3, 0), &cpu, 4);
    }
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(TILED), bgra, SIZE, SIZE, error, sizeof error) == 0);
    CHECK(bgra[3 * SIZE + 5] == 0xff102040u);
    CHECK(differing_pixels(x, at(TILED)) == 1);
}

/* A half (IEEE binary16) as a float. */
static float half_value(uint16_t h)
{
    const int exponent = h >> 10 & 0x1f, mantissa = h & 0x3ff;
    const float magnitude = exponent ? ldexpf(1.0f + (float)mantissa / 1024, exponent - 15) : ldexpf((float)mantissa / 1024, -14);
    return h >> 15 ? -magnitude : magnitude;
}

/* An unsigned float of `mantissa_bits` mantissa bits and a 5-bit exponent (bias 15), as 10_11_11 holds them. */
static float small_float_value(uint32_t bits, int mantissa_bits)
{
    const int exponent = (int)(bits >> mantissa_bits & 0x1f);
    const float mantissa = (float)(bits & ((1u << mantissa_bits) - 1)) / (float)(1u << mantissa_bits);
    return exponent ? ldexpf(1.0f + mantissa, exponent - 15) : ldexpf(mantissa, -14);
}

/* Float render targets: the triangle drawn into a 16_16_16_16 float target (8-byte texels) and into a
   10_11_11 float one (Thin1DThin, tiling index 13) is written back to their memory as halves (x first)
   and as unsigned 11-, 11- and 10-bit floats (x in the low bits): red (x + 0.5) / 16, green
   (y + 0.5) / 16, blue 0.25 (alpha 1 in the halves). Reading them as BGRA stops by name. */
static void float_targets(rt_executor *x)
{
    enum { FLOAT_BUFFER = 0xc0fb0000, HALVES = 0x1b00000, SMALL_FLOATS = 0x1b80000, THIN_1D = 13, NUMBER_FLOAT = 7 };
    static const struct { uint32_t address, format, bytes; } cases[] = {{HALVES, 0xc, 8}, {SMALL_FLOATS, 0x6, 4}};
    char error[200] = "";
    for (unsigned c = 0; c < 2; c++) {
        rt_tiling layout;
        CHECK(rt_tiling_layout(THIN_1D, cases[c].bytes, SIZE, SIZE, &layout, error, sizeof error) == 0);
        uint8_t *memory = (uint8_t *)(uintptr_t)at(cases[c].address);
        memset(memory, 0, rt_tiling_size(&layout, 1));
        rt_render_forget(), rt_executor_reset(x);
        const uint32_t offset = record_setup(FLOAT_BUFFER, at(cases[c].address), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, at(cases[c].address), cases[c].format, SWAP_STD);
        words[4] |= NUMBER_FLOAT << 8, words[5] = THIN_1D;
        CHECK(rt_render_record_color_target(FLOAT_BUFFER, offset, 0, words, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, FLOAT_BUFFER, record_triangle_draw(FLOAT_BUFFER, offset + 0x10), error, sizeof error);
        if (ran) fprintf(stderr, "float target %#x: %s\n", cases[c].format, error);
        CHECK(ran == 0);
        int placed = 1;
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) {
                const uint8_t *texel = memory + rt_tiling_offset(&layout, px, y, 0);
                float got[4] = {0, 0, 0, 1};
                if (cases[c].bytes == 8) {
                    uint16_t h[4];
                    memcpy(h, texel, sizeof h);
                    for (int k = 0; k < 4; k++) got[k] = half_value(h[k]);
                } else {
                    uint32_t p;
                    memcpy(&p, texel, 4);
                    got[0] = small_float_value(p & 0x7ff, 6), got[1] = small_float_value(p >> 11 & 0x7ff, 6), got[2] = small_float_value(p >> 22, 5);
                }
                const float want[4] = {((float)px + 0.5f) / SIZE, ((float)y + 0.5f) / SIZE, 0.25f, 1};
                for (int k = 0; k < 4; k++) placed &= fabsf(got[k] - want[k]) <= want[k] / 32 + 1e-3f;
            }
        if (!placed) fprintf(stderr, "float target %#x: texels not as drawn\n", cases[c].format);
        CHECK(placed);
    }
    uint32_t bgra[4];
    CHECK(rt_executor_read_target(x, at(SMALL_FLOATS), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "not 8_8_8_8"));
}

/* A block-compressed image (BC1, DATA_FORMAT 35) the CPU wrote, linear (tiling index 8) in 4 x 4 blocks
   of 8 bytes: each block a solid colour (colour 0, all indices 0), its red from the block's column and
   green from its row in RGB 5:6:5, blue full. image_load_mip of each pixel shows its block's colour;
   read as BC4 (mutant) it does not. */
static void compressed_image(rt_executor *x)
{
    enum { BLOCKS = 0xe00000, BLOCK_TARGET = 0xdc0000, BLOCKS_PER_ROW = SIZE / 4, LINEAR_ALIGNED = 8, BC1 = 35, BC4 = 38 };
    char error[200] = "";
    uint32_t *blocks = (uint32_t *)(uintptr_t)at(BLOCKS);
    for (uint32_t by = 0; by < BLOCKS_PER_ROW; by++)
        for (uint32_t bx = 0; bx < BLOCKS_PER_ROW; bx++) {
            const uint32_t colour0 = (bx * 8) << 11 | (by * 16) << 5 | 31;
            blocks[(by * BLOCKS_PER_ROW + bx) * 2] = colour0;     /* colour 1 is 0: colour 0 > colour 1, four-colour mode */
            blocks[(by * BLOCKS_PER_ROW + bx) * 2 + 1] = 0;       /* every texel index 0 */
        }
    uint32_t image[8];
    image_descriptor(image, at(BLOCKS), BC1, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    const int drawn = draw_texels(x, at(BLOCK_TARGET), image, error, sizeof error);
    if (drawn) fprintf(stderr, "BC1: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(BLOCK_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int right = 1;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            right &= near(p >> 16 & 0xff, (float)(px / 4 * 8) / 31) && near(p >> 8 & 0xff, (float)(y / 4 * 16) / 63) && near(p & 0xff, 1);
        }
    CHECK(right);
    image_descriptor(image, at(BLOCKS), BC4, 0, SIZE, SIZE);
    image[3] = (image[3] & ~0xfffu) | (4 | 4 << 3 | 4 << 6 | 4 << 9) | LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(BLOCK_TARGET), image, error, sizeof error) == 0);
    CHECK(rt_executor_read_target(x, at(BLOCK_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int differs = 0;
    for (int i = 0; i < SIZE * SIZE; i++) differs |= (bgra[i] & 0xff) != 0xff;
    CHECK(differs);
}

int main(void)
{
    const int fd = map_guest_memory();
    if (fd < 0) {
        fprintf(stderr, "cannot map the guest memory at %#llx\n", GUEST_BASE);
        return 1;
    }
    rt_executor *x = NULL;
    char error[256] = "";
    const rt_executor_memory memory = {fd, GUEST_SIZE, GUEST_BASE};
    if (rt_executor_open(&memory, &x, error, sizeof error)) {
        fprintf(stderr, "skipped: %s\n", error);
        return SKIPPED;
    }
    enum { BUFFER = 0xc0de0000, SECOND = 0xc0df0000, THIRD = 0xc0e00000 };

    /* A triangle into an R8G8B8A8 target (SWAP_STD), through guest addresses and the pool's GPU buffer. */
    rt_render_forget(), rt_executor_reset(x);
    uint32_t size = record_draw(BUFFER, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const int ran = rt_executor_run(x, BUFFER, size, error, sizeof error);
    if (ran) fprintf(stderr, "run: %s\n", error);
    CHECK(ran == 0);
    check_triangle(x, at(TARGET));

    /* The same into a B8G8R8A8 target (SWAP_ALT): the display's pixels are the same. */
    size = record_draw(SECOND, at(OTHER_TARGET), COLOR_8_8_8_8, SWAP_ALT, PERSP_CENTER);
    CHECK(rt_executor_run(x, SECOND, size, error, sizeof error) == 0);
    check_triangle(x, at(OTHER_TARGET));

    /* What is not drawn yet stops with the reason. */
    size = record_draw(THIRD, at(TARGET) + 0x400000, COLOR_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == -1 && strstr(error, "not drawn yet"));
    size = record_draw(THIRD, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER | FRONT_FACE);
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == 0); /* the front face, pixel_color not reading it */
    front_face(x);
    size = record_draw(THIRD, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER | 0x10); /* the linear barycentrics */
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == -1 && strstr(error, "pixel-shader inputs"));
    /* The library's registers: the normal state draws as before, a target masked off draws nothing,
       and what is not drawn yet (blending, depth, other primitives, instances) stops by name. */
    enum { FOURTH = 0xc0e10000 };
    const uint32_t normal[][2] = {{0xa1e0, 0}, {0xa200, 0}, {0xa202, 0x1u << 4 | 0xccu << 16}, {0xa08e, 0xf}, {0xc242, 4}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), normal, 5, 1, error, sizeof error) == 0);
    check_triangle(x, at(TARGET));
    const uint32_t masked[][2] = {{0xa08e, 0xf0}};                   /* only target 1 writes */
    CHECK(run_with_registers(x, FOURTH, at(TARGET) + 0x400000, masked, 1, 0, error, sizeof error) == 0);
    uint32_t bgra[4];
    CHECK(rt_executor_read_target(x, at(TARGET) + 0x400000, bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    const uint32_t dual_source[][2] = {{0xa1e0, 0x40000000u | 15}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), dual_source, 1, 0, error, sizeof error) == -1 && strstr(error, "blend factor 15"));
    blending(x);
    draws_in_one_batch(x);
    buffers_in_one_submission(x);
    const uint32_t depth[][2] = {{0xa200, 0x2}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), depth, 1, 0, error, sizeof error) == -1 && strstr(error, "depth or stencil"));
    const uint32_t line_strips[][2] = {{0xc242, 5}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), line_strips, 1, 0, error, sizeof error) == -1 && strstr(error, "primitive type 5"));
    depth_and_strips(x);
    multiple_targets(x);
    compute_cleared_target(x);
    /* a fast-clear eliminate pass (mode 2) over a target without fast clears writes nothing */
    const uint32_t eliminate[][2] = {{0xa202, 0x2u << 4 | 0xccu << 16}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET) + 0x480000, eliminate, 1, 0, error, sizeof error) == 0);
    CHECK(rt_executor_read_target(x, at(TARGET) + 0x480000, bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    fast_clears(x);
    metadata_store_waits(x);
    const uint32_t resolve[][2] = {{0xa202, 0x3u << 4 | 0xccu << 16}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), resolve, 1, 0, error, sizeof error) == -1 && strstr(error, "colour mode 3"));
    const uint32_t unknown[][2] = {{0xa1e8, 0}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), unknown, 1, 0, error, sizeof error) == -1 && strstr(error, "0xa1e8"));
    CHECK(run_with_registers(x, FOURTH, at(TARGET), normal, 0, 2, error, sizeof error) == -1 && strstr(error, "2 instances"));
    CHECK(rt_executor_read_target(x, at(0x300000), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(rt_executor_read_target(x, at(TARGET), bgra, SIZE + 1, 1, error, sizeof error) == -1);

    /* A vertex shader that writes VCC's halves as scalars (as the game's do, before vector writes set
       VCC): drawn as before; one that then reads VCC as a lane mask stops by name at run time, naming
       that shader. */
    {
        enum { VCC_CODE = 0x2c00, VCC_TARGET = 0x780000 };
        static uint32_t dead_write[64], read_after[64];
        const size_t n = sizeof vertex_position_color / sizeof *vertex_position_color;
        dead_write[0] = 0xbeeb0380;                           /* s_mov_b32 vcc_hi, 0 */
        memcpy(dead_write + 1, vertex_position_color, sizeof vertex_position_color);
        read_after[0] = 0xbeeb0380;
        read_after[1] = 0x00121280;                           /* v_cndmask_b32_e32 v9, 0, v9, vcc */
        memcpy(read_after + 2, vertex_position_color, sizeof vertex_position_color);
        for (int variant = 0; variant < 2; variant++) {
            const uint64_t code = at(VCC_CODE + 0x200 * (unsigned)variant);
            memcpy((void *)(uintptr_t)code, variant ? read_after : dead_write, (n + 1 + (unsigned)variant) * sizeof(uint32_t));
            rt_render_forget(), rt_executor_reset(x);
            uint32_t offset = record_setup(FOURTH, at(VCC_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
            const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0};
            CHECK(rt_render_record_vertex_shader(FOURTH, offset, vertex, error, sizeof error) == 0);
            const int drawn = rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error);
            if (variant == 0) {
                if (drawn) fprintf(stderr, "vcc dead write: %s\n", error);
                CHECK(drawn == 0);
                check_triangle(x, at(VCC_TARGET));
            } else {
                CHECK(drawn == -1 && strstr(error, "read VCC as a lane mask"));
                /* the stop names the shader that wrote it, though it is checked after the batch */
                char named[64];
                snprintf(named, sizeof named, "the vertex shader at %#llx", (unsigned long long)code);
                if (!strstr(error, named)) fprintf(stderr, "vcc stop: %s\n", error);
                CHECK(strstr(error, named) != NULL);
            }
        }
    }

    /* A pixel shader reading the pixel's position (SPI_PS_INPUT 0x302: barycentrics, then x and y in
       v2 and v3): its colour (x / 16, y / 16) at pixel centres is the triangle's image. */
    {
        enum { POSITION_CODE = 0x2800, POSITION_TARGET = 0x700000 };
        memcpy((void *)(uintptr_t)at(POSITION_CODE), pixel_position_color, sizeof pixel_position_color);
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(FOURTH, at(POSITION_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(POSITION_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 0, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(FOURTH, offset, pixel, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error);
        if (drawn) fprintf(stderr, "position: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(POSITION_TARGET));
    }

    /* A rect list (primitive type 17): three corners of the whole target, the right angle first,
       then last; the geometry stage adds the fourth corner, and its colour (2, 2) as the other two
       minus the corner's, so the target holds the same image as the large triangle. Then a count
       that is not whole rects stops by name. */
    {
        enum { RECT_TARGET = 0x600000 };
        float *corners = (float *)(uintptr_t)at(CORNERS), *colors = (float *)(uintptr_t)at(COLORS);
        float saved_corners[12], saved_colors[12];
        memcpy(saved_corners, corners, sizeof saved_corners);
        memcpy(saved_colors, colors, sizeof saved_colors);
        static const float rect_corners[2][3][4] = {{{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}},
                                                     {{1, -1, 0, 1}, {-1, 1, 0, 1}, {-1, -1, 0, 1}}};
        static const float rect_colors[2][3][4] = {{{0, 0, 0.25f, 1}, {1, 0, 0.25f, 1}, {0, 1, 0.25f, 1}},
                                                    {{1, 0, 0.25f, 1}, {0, 1, 0.25f, 1}, {0, 0, 0.25f, 1}}};
        const uint32_t rect_list[][2] = {{0xc242, 17}};
        for (int order = 0; order < 2; order++) {
            memcpy(corners, rect_corners[order], sizeof rect_corners[order]);
            memcpy(colors, rect_colors[order], sizeof rect_colors[order]);
            const int drawn = run_with_registers(x, FOURTH, at(RECT_TARGET), rect_list, 1, 0, error, sizeof error);
            if (drawn) fprintf(stderr, "rect list: %s\n", error);
            CHECK(drawn == 0);
            check_triangle(x, at(RECT_TARGET));
        }
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(FOURTH, at(RECT_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_register(FOURTH, offset, 0xc242, 17, error, sizeof error) == 0);
        CHECK(rt_render_record_draw_auto(FOURTH, offset + 0x10, 4, error, sizeof error) == 0);
        CHECK(rt_executor_run(x, FOURTH, offset + 0x20, error, sizeof error) == -1 && strstr(error, "whole rects"));
        memcpy(corners, saved_corners, sizeof saved_corners);
        memcpy(colors, saved_colors, sizeof saved_colors);
    }
    /* No pixel shader (placed as the runtime's sceGnmSetPsShader places it) with the colour target
       written: stops by name (a depth-only pass writes no colour target; depth_and_strips draws one). */
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(FOURTH, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS];
    CHECK(rt_render_record_pixel_shader_at(FOURTH + offset, no_pixel_shader) == 0);
    CHECK(rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error) == -1 &&
          strstr(error, "no pixel shader that writes a colour target"));

    /* A compute dispatch (scale_add: out[i] = in[i] * 2.5 + 1, one workgroup of 64) through the record:
       the shader placed as sceGnmSetCsShader places it, its user data the two buffer descriptors;
       the results are in the guest memory when the run returns. Then a dispatch with no compute
       shader, and one whose shader takes its workgroup's y index, stop by name. */
    {
        enum { COMPUTE = 0xc0e20000, COMPUTE_CODE = 0x3000, INPUT = 0x20000, OUTPUT = 0x21000, LANES = 64 };
        memcpy((void *)(uintptr_t)at(COMPUTE_CODE), scale_add, sizeof scale_add);
        float *input = (float *)(uintptr_t)at(INPUT), *output = (float *)(uintptr_t)at(OUTPUT);
        for (unsigned i = 0; i < LANES; i++) input[i] = (float)i, output[i] = -1;
        const uint64_t code = at(COMPUTE_CODE);
        uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, 8 << 1, LANES, 1, 1};
        rt_render_forget(), rt_executor_reset(x);
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, shader) == 0);
        uint32_t user[8];
        descriptor(user, at(INPUT), 4);
        descriptor(user + 4, at(OUTPUT), 4);
        CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        const uint32_t one[3] = {1, 1, 1};
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, COMPUTE, 0x40, error, sizeof error);
        if (ran) fprintf(stderr, "dispatch: %s\n", error);
        CHECK(ran == 0);
        int right = 1;
        for (unsigned i = 0; i < LANES; i++) right &= output[i] == (float)i * 2.5f + 1.0f;
        CHECK(right);

        /* two dispatches in one buffer (one batch): the second reads what the first stored */
        enum { SECOND_OUTPUT = 0x22800 };
        float *second = (float *)(uintptr_t)at(SECOND_OUTPUT);
        for (unsigned i = 0; i < LANES; i++) output[i] = -1, second[i] = -1;
        rt_render_forget();
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, shader) == 0);
        CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
        uint32_t chained[8];
        descriptor(chained, at(OUTPUT), 4);
        descriptor(chained + 4, at(SECOND_OUTPUT), 4);
        CHECK(rt_render_record_user_data(COMPUTE, 0x40, RT_STAGE_COMPUTE, 0, chained, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_dispatch(COMPUTE, 0x50, one, error, sizeof error) == 0);
        const int chain = rt_executor_run(x, COMPUTE, 0x60, error, sizeof error);
        if (chain) fprintf(stderr, "chained dispatches: %s\n", error);
        CHECK(chain == 0);
        for (unsigned i = 0; i < LANES; i++) right &= second[i] == ((float)i * 2.5f + 1.0f) * 2.5f + 1.0f;
        CHECK(right);

        rt_render_forget(), rt_executor_reset(x);
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
        CHECK(rt_executor_run(x, COMPUTE, 0x40, error, sizeof error) == -1 && strstr(error, "without a compute shader"));
        shader[3] |= 1u << 8; /* TGID_Y_EN */
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, shader) == 0);
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
        CHECK(rt_executor_run(x, COMPUTE, 0x40, error, sizeof error) == -1 && strstr(error, "y or z index"));
    }

    /* Storage images through the record: a fill pass writes a 64 x 1 RGBA32F image (its first use, so
       the executor makes it), a copy pass copies it to a second, and a third reads the second back into
       guest memory; each texel arrives as written. Reading an image the GPU never wrote stops by name. */
    {
        enum { IMAGES = 0xc0e30000, FILL_CODE = 0x4000, COPY_CODE = 0x4400, READ_CODE = 0x4800, IMAGE_A = 0x500000, IMAGE_B = 0x510000,
               BACK = 0x22000, LANES = 64 };
        memcpy((void *)(uintptr_t)at(FILL_CODE), image_fill, sizeof image_fill);
        memcpy((void *)(uintptr_t)at(COPY_CODE), image_copy, sizeof image_copy);
        memcpy((void *)(uintptr_t)at(READ_CODE), image_to_buffer, sizeof image_to_buffer);
        float *back = (float *)(uintptr_t)at(BACK);
        for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
        uint32_t image_a[8], image_b[8], user[16] = {0};
        const uint32_t one[3] = {1, 1, 1};
        const uint64_t codes[3] = {at(FILL_CODE), at(COPY_CODE), at(READ_CODE)};
        /* three times: 2D images, then (as the game's) 3-layer 1D arrays seen through their first layer,
           where the same coordinates (x, 0) are (x, slice 0), then through layer 1 alone (BASE_ARRAY 1),
           where slice 0 counts from the view's first layer */
        for (int shape = 0; shape < 3; shape++) {
        image_descriptor(image_a, at(IMAGE_A + 0x20000 * (unsigned)shape), 14, 7, LANES, 1);
        image_descriptor(image_b, at(IMAGE_B + 0x20000 * (unsigned)shape), 14, 7, LANES, 1);
        if (shape >= 1)
            for (int k = 0; k < 2; k++) {
                uint32_t *d = k ? image_b : image_a;
                const uint32_t layer = shape == 2 ? 1 : 0;
                image_descriptor(d, at((k ? IMAGE_B : IMAGE_A) + 0x20000), 14, 7, LANES, 1);
                d[3] = (d[3] & 0x0fffffffu) | 12u << 28;   /* TYPE 1D array */
                d[4] |= 2;                                   /* DEPTH: 3 layers */
                d[5] = layer | layer << 13;                  /* BASE_ARRAY, LAST_ARRAY */
            }
        for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = 0x10;
        for (int pass = 0; pass < 3; pass++) {
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[pass] >> 8), (uint32_t)(codes[pass] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
            memset(user, 0, sizeof user);
            if (pass == 0) memcpy(user + 8, image_a, sizeof image_a);
            if (pass == 1) memcpy(user, image_a, sizeof image_a), memcpy(user + 8, image_b, sizeof image_b);
            if (pass == 2) descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
            offset += 0x40;
        }
        const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
        if (ran) fprintf(stderr, "images: %s\n", error);
        CHECK(ran == 0);
        int right = 1;
        for (unsigned i = 0; i < LANES; i++)
            right &= back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f;
        CHECK(right);
        }

        /* Stores outside a storage image write nothing: 64 lanes fill a 40-texel image, and 40 lanes
           read it back; texels 0-39 hold their values and nothing past them was touched. */
        {
            enum { NARROW = 40 };
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_a, at(IMAGE_A + 0x70000), 14, 7, NARROW, 1);
            for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
            uint32_t offset = 0x10;
            for (int pass = 0; pass < 2; pass++) {
                const uint64_t code = pass ? codes[2] : codes[0];
                const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1,
                                                                  pass ? NARROW : LANES, 1, 1};
                CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                memset(user, 0, sizeof user);
                if (pass) descriptor(user, at(BACK), 16);
                memcpy(user + 8, image_a, sizeof image_a);
                CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                offset += 0x40;
            }
            const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
            if (ran) fprintf(stderr, "stores past an image: %s\n", error);
            CHECK(ran == 0);
            int right = 1;
            for (unsigned i = 0; i < LANES; i++)
                right &= i < NARROW ? back[i * 4] == (float)i && back[i * 4 + 3] == 0.5f : back[i * 4] == -1 && back[i * 4 + 3] == -1;
            CHECK(right);
        }

        /* A 3D image (TYPE 3D, DEPTH 1: two slices of 64 x 1), as the game's compute shader fills a colour
           volume: texels stored in slice 1 read back from slice 1 as stored, and slice 0 keeps the new
           image's zeros (a 3D image taken as one 2D slice would return the stored texels there too).
           A storage cube (TYPE CUBE, faces 0 to 5) is an array of its faces: the same programs store to
           face 1 and read faces 1 and 0. Reading a 3D image the GPU never wrote stops by name: its layout
           in memory is not done yet. */
        for (int cube = 0; cube < 2; cube++) {
            enum { FILL3_CODE = 0x4c00, READ3_CODE = 0x5000, READ30_CODE = 0x5400, IMAGE_3D = IMAGE_A + 0x90000, CUBE_IMAGE = IMAGE_A + 0x98000 };
            memcpy((void *)(uintptr_t)at(FILL3_CODE), image_fill_3d, sizeof image_fill_3d);
            memcpy((void *)(uintptr_t)at(READ3_CODE), image_to_buffer_3d, sizeof image_to_buffer_3d);
            memcpy((void *)(uintptr_t)at(READ30_CODE), image_to_buffer_3d_slice0, sizeof image_to_buffer_3d_slice0);
            uint32_t volume[8];
            image_descriptor(volume, at(cube ? CUBE_IMAGE : IMAGE_3D), 14, 7, LANES, 1);
            volume[3] = (volume[3] & 0x0fffffffu) | (uint32_t)(cube ? RT_IMAGE_CUBE : RT_IMAGE_3D) << 28;
            if (cube) volume[5] = 5u << 13; /* faces 0 to 5 (DEPTH 0: one cube) */
            else volume[4] |= 1;            /* DEPTH: two slices */
            for (int slice = 1; slice >= 0; slice--) {
                rt_render_forget(), rt_executor_reset(x);
                for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
                uint32_t offset = 0x10;
                const uint64_t passes[2] = {at(FILL3_CODE), at(slice ? READ3_CODE : READ30_CODE)};
                for (int pass = 0; pass < 2; pass++) {
                    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(passes[pass] >> 8), (uint32_t)(passes[pass] >> 40), 0x3, 16 << 1,
                                                                      LANES, 1, 1};
                    CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                    memset(user, 0, sizeof user);
                    if (pass) descriptor(user, at(BACK), 16);
                    memcpy(user + 8, volume, sizeof volume);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                    offset += 0x40;
                }
                const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
                if (ran) fprintf(stderr, "%s: %s\n", cube ? "storage cube" : "3D image", error);
                CHECK(ran == 0);
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= slice ? back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f
                                   : back[i * 4] == 0 && back[i * 4 + 1] == 0 && back[i * 4 + 2] == 0 && back[i * 4 + 3] == 0;
                CHECK(right);
            }
            if (cube) continue;
            rt_render_forget(), rt_executor_reset(x);
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(at(READ3_CODE) >> 8), (uint32_t)(at(READ3_CODE) >> 40), 0x3, 16 << 1,
                                                              LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            memset(user, 0, sizeof user);
            descriptor(user, at(BACK), 16);
            image_descriptor(volume, at(IMAGE_3D + 0x8000), 14, 7, LANES, 1);
            volume[3] = (volume[3] & 0x0fffffffu) | (uint32_t)RT_IMAGE_3D << 28;
            volume[4] |= 1;
            memcpy(user + 8, volume, sizeof volume);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            CHECK(rt_executor_run(x, IMAGES, 0x50, error, sizeof error) == -1 && strstr(error, "3D image") && strstr(error, "not done yet"));
        }

        /* A storage image's DST_SEL permutes its components (shadPS4 swizzles storage loads by it and
           stores by its inverse): stores of (i, 1, 2, 0.5) through z, y, x, w (0xf2e) read back through
           x, y, z, w as (2, 1, i, 0.5), and through z, y, x, w again as stored. */
        {
            enum { SWIZZLED = IMAGE_A + 0xa0000, BGRA = 6 | 5 << 3 | 4 << 6 | 7 << 9 };
            for (int read_swizzled = 0; read_swizzled < 2; read_swizzled++) {
                rt_render_forget(), rt_executor_reset(x);
                for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
                uint32_t offset = 0x10;
                for (int pass = 0; pass < 2; pass++) {
                    const uint64_t code = pass ? codes[2] : codes[0];
                    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1, LANES, 1, 1};
                    CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                    memset(user, 0, sizeof user);
                    if (pass) descriptor(user, at(BACK), 16);
                    image_descriptor(image_a, at(SWIZZLED), 14, 7, LANES, 1);
                    if (!pass || read_swizzled) image_a[3] = (image_a[3] & ~0xfffu) | BGRA;
                    memcpy(user + 8, image_a, sizeof image_a);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                    offset += 0x40;
                }
                const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
                if (ran) fprintf(stderr, "swizzled storage image: %s\n", error);
                CHECK(ran == 0);
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= read_swizzled ? back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f
                                           : back[i * 4] == 2.0f && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == (float)i && back[i * 4 + 3] == 0.5f;
                CHECK(right);
            }
        }

        /* A one-component storage image with DST_SEL (0, 0, 0, x), as the game's alpha-only images: a store
           of the shader's w alone (DMASK 8) writes x; read back through the identity it is x, and through
           the same DST_SEL it is (0, 0, 0, x). */
        {
            enum { ALPHA_CODE = 0x5800, ALPHA_IMAGE = IMAGE_A + 0xc0000, TO_ALPHA = 0 | 0 << 3 | 0 << 6 | 4 << 9 };
            memcpy((void *)(uintptr_t)at(ALPHA_CODE), image_fill_alpha, sizeof image_fill_alpha);
            for (int read_swizzled = 0; read_swizzled < 2; read_swizzled++) {
                rt_render_forget(), rt_executor_reset(x);
                for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
                uint32_t offset = 0x10;
                for (int pass = 0; pass < 2; pass++) {
                    const uint64_t code = pass ? codes[2] : at(ALPHA_CODE);
                    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1, LANES, 1, 1};
                    CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                    memset(user, 0, sizeof user);
                    if (pass) descriptor(user, at(BACK), 16);
                    image_descriptor(image_a, at(ALPHA_IMAGE), 4, 7, LANES, 1); /* 32 float */
                    if (!pass || read_swizzled) image_a[3] = (image_a[3] & ~0xfffu) | TO_ALPHA;
                    memcpy(user + 8, image_a, sizeof image_a);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                    offset += 0x40;
                }
                const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
                if (ran) fprintf(stderr, "alpha storage image: %s\n", error);
                CHECK(ran == 0);
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= read_swizzled ? back[i * 4] == 0 && back[i * 4 + 1] == 0 && back[i * 4 + 2] == 0 && back[i * 4 + 3] == (float)i
                                           : back[i * 4] == (float)i;
                CHECK(right);
            }
        }

        /* A compute shader's stores to an sRGB image (8_8_8_8, NUM_FORMAT 9) go out as UNORM, unconverted
           (Vulkan has no sRGB storage images); a draw sampling the same memory as sRGB then reads those
           texels, not the guest memory's (zeros here): row 0's texels hold (min(i, 1), 1, 1, 0.5), so pixel
           (1, 0) is white with alpha about 128 and pixel (0, 0) has no red. A compute shader reading an sRGB
           image stops by name. */
        {
            enum { SRGB_IMAGE = IMAGE_A + 0xb0000, SRGB_TARGET = 0x2180000 };
            memset((void *)(uintptr_t)at(SRGB_IMAGE), 0, SIZE * SIZE * 4);
            uint32_t srgb[8];
            image_descriptor(srgb, at(SRGB_IMAGE), 10, 9, SIZE, SIZE);
            srgb[3] |= 8u << 20; /* linear: the draw's load would read the memory as it lies */
            rt_render_forget(), rt_executor_reset(x);
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[0] >> 8), (uint32_t)(codes[0] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            memset(user, 0, sizeof user);
            memcpy(user + 8, srgb, sizeof srgb);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x30, one, error, sizeof error) == 0);
            int ran = rt_executor_run(x, IMAGES, 0x40, error, sizeof error);
            if (ran) fprintf(stderr, "sRGB storage image: %s\n", error);
            CHECK(ran == 0);
            ran = draw_texels(x, at(SRGB_TARGET), srgb, error, sizeof error);
            if (ran) fprintf(stderr, "sRGB image drawn: %s\n", error);
            CHECK(ran == 0);
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(SRGB_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            const uint32_t alpha = bgra[1] >> 24; /* 0.5 as unorm: 127 or 128 */
            CHECK((bgra[1] & 0xffffffu) == 0xffffffu && (alpha == 127 || alpha == 128) && (bgra[0] >> 16 & 0xff) == 0 && bgra[SIZE] == 0);
            rt_render_forget(), rt_executor_reset(x);
            const uint32_t reader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[2] >> 8), (uint32_t)(codes[2] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, reader) == 0);
            descriptor(user, at(BACK), 16);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            CHECK(rt_executor_run(x, IMAGES, 0x50, error, sizeof error) == -1 && strstr(error, "reading an sRGB image"));
        }

        /* An image no shader wrote holds the guest memory's texels: linear (tiling index 8) RGBA32F
           texels the CPU wrote read back as written; a layout not done yet (index 1) stops by name. */
        float *guest_texels = (float *)(uintptr_t)at(IMAGE_B + 0x8000);
        for (unsigned i = 0; i < LANES * 4; i++) guest_texels[i] = (float)i * 0.25f, back[i] = -1;
        for (int tiling = 0; tiling < 2; tiling++) {
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_b, at(IMAGE_B + 0x8000), 14, 7, LANES, 1);
            image_b[3] |= (tiling ? 8u : 1u) << 20;
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[2] >> 8), (uint32_t)(codes[2] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            const int loaded = rt_executor_run(x, IMAGES, 0x50, error, sizeof error);
            if (!tiling) {
                CHECK(loaded == -1 && strstr(error, "tiling index 1"));
                continue;
            }
            if (loaded) fprintf(stderr, "guest texels: %s\n", error);
            CHECK(loaded == 0);
            int same = 1;
            for (unsigned i = 0; i < LANES * 4; i++) same &= back[i] == (float)i * 0.25f;
            CHECK(same);
        }

        /* What a shader stores to an image reaches the guest memory under it (write_back_image), laid out
           as its tile mode has it: linear (tiling index 8) RGBA32F texels filled by image_fill are in the
           memory as (float(i), 1, 2, 0.5) once the run returns, for any other view of it to load. */
        {
            float *const stored = (float *)(uintptr_t)at(IMAGE_B + 0x10000);
            for (unsigned i = 0; i < LANES * 4; i++) stored[i] = -1;
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_b, at(IMAGE_B + 0x10000), 14, 7, LANES, 1);
            image_b[3] |= 8u << 20;
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[0] >> 8), (uint32_t)(codes[0] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            memset(user, 0, sizeof user);
            memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x30, one, error, sizeof error) == 0);
            const int ran = rt_executor_run(x, IMAGES, 0x40, error, sizeof error);
            if (ran) fprintf(stderr, "stored image written back: %s\n", error);
            CHECK(ran == 0);
            int written = 1;
            for (unsigned i = 0; i < LANES; i++)
                written &= stored[i * 4] == (float)i && stored[i * 4 + 1] == 1.0f && stored[i * 4 + 2] == 2.0f && stored[i * 4 + 3] == 0.5f;
            CHECK(written);
        }

        /* An 8_8_8_8 uint image (DATA_FORMAT 10, NUM_FORMAT 4), linear: each texel's four bytes read
           back as four integers, red first. */
        {
            uint8_t *const bytes = (uint8_t *)(uintptr_t)at(IMAGE_B + 0xc000);
            for (unsigned i = 0; i < LANES * 4; i++) bytes[i] = (uint8_t)(i * 7 + 3);
            uint32_t *const words = (uint32_t *)back;
            for (unsigned i = 0; i < LANES * 4; i++) words[i] = 0xeeeeeeeeu;
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_b, at(IMAGE_B + 0xc000), 10, 4, LANES, 1);
            image_b[3] |= 8u << 20;
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[2] >> 8), (uint32_t)(codes[2] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            const int loaded = rt_executor_run(x, IMAGES, 0x50, error, sizeof error);
            if (loaded) fprintf(stderr, "8_8_8_8 uint image: %s\n", error);
            CHECK(loaded == 0);
            int same = 1;
            for (unsigned i = 0; i < LANES * 4; i++) same &= words[i] == (uint8_t)(i * 7 + 3);
            CHECK(same);
        }
    }

    sampled_images(x);
    sampled_lookup(x);
    float_images(x);
    many_images(x);
    sampled_levels(x);
    sampled_cube(x);
    fetch_shader(x);
    pixel_input_controls(x);
    vertex_image_after_fetch(x);
    unset_user_data(x);
    target_read_in_buffer(x);
    state_across_buffers(x);
    indexed_draws(x);
    indexed_packets(x, fd);
    target_memory(x);
    tiled_target_memory(x);
    float_targets(x);
    compressed_image(x);

    rt_executor_close(x);
    rt_render_forget();
    munmap((void *)(uintptr_t)GUEST_BASE, GUEST_SIZE);
    close(fd);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
