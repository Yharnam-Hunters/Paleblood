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
    rt_render_forget();
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
    rt_render_forget();
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

/* Pixel shaders reading images the CPU wrote into guest memory (image_load_mip from a sampled image):
   a 128 x 64 8_8_8_8 image tiled as Thin2DThin (index 14), its texels placed with runtime/tiling.h,
   draws the reference image; the same bytes described as Display2DThin (index 10) do not. A linear one
   (index 8), DST_SEL z, y, x, w over blue-first bytes, and the guest changing its texels between
   draws all read what the guest memory holds. A target larger than the image reads outside it and
   stops by name. */
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
            rt_render_forget();
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
   and the end texels' values outside. Mutants: point filtering and wrapping differ; a sampler with
   unequal filters, a border clamp or anisotropy stops by name. */
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
    static const uint32_t anisotropic[4] = {0x2 | 2u << 9, 0, 1u << 20 | 1u << 22, 0};
    const uint32_t *const samplers[] = {bilinear_clamped, point_clamped, bilinear_wrapped, unequal, border, anisotropic};
    for (int k = 0; k < 6; k++) {
        uint32_t user[16] = {0};
        user[2] = (uint32_t)at(TABLE), user[3] = (uint32_t)(at(TABLE) >> 32);
        image_descriptor(user + 4, at(IMAGE), 10, 0, WIDTH, HEIGHT);
        user[7] |= LINEAR_ALIGNED << 20;
        memcpy(user + 12, samplers[k], 4 * sizeof *user);
        rt_render_forget();
        uint32_t offset = record_setup(LOOKUP_BUFFER, at(LOOKUP_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(LOOKUP_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302,
                                                       0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(LOOKUP_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, LOOKUP_BUFFER, record_triangle_draw(LOOKUP_BUFFER, offset + 0x30), error, sizeof error);
        if (k < 3 && drawn) fprintf(stderr, "lookup %d: %s\n", k, error);
        if (k < 3) CHECK(drawn == 0);
        if (k == 0) {
            const int misses = lookup_misses(x, at(LOOKUP_TARGET));
            if (misses) fprintf(stderr, "lookup: %d pixels differ\n", misses);
            CHECK(misses == 0);
        }
        if (k == 1 || k == 2) CHECK(lookup_misses(x, at(LOOKUP_TARGET)) > 0);
        if (k == 3) CHECK(drawn == -1 && strstr(error, "filters 1 (magnifying) and 0"));
        if (k == 4) CHECK(drawn == -1 && strstr(error, "clamp mode 6"));
        if (k == 5) CHECK(drawn == -1 && strstr(error, "anisotropy"));
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

/* Runs the triangle's state with these registers and a draw of `count` vertices (record_setup's). */
static int run_counted(rt_executor *x, uint64_t buffer, uint64_t target, const uint32_t (*registers)[2], int n, uint32_t count, char *error,
                       size_t error_size)
{
    rt_render_forget();
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    for (int i = 0; i < n; i++, offset += 0x10) CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    CHECK(rt_render_record_draw_auto(buffer, offset, count, error, error_size) == 0);
    return rt_executor_run(x, buffer, offset + 0x10, error, error_size);
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
       - a test that reads the buffer (stencil EQUAL, depth LESS), another stencil operation, depth bounds,
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
    const uint32_t stencil_equal[][2] = {DEPTH_TARGET, {0xa10c, 0xff0000}, {0xa10b, 0x30030}, {0xa200, 0x1 | 0x2u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), stencil_equal, 12, 3, error, sizeof error) == -1 && strstr(error, "reading the stencil"));
    const uint32_t depth_less[][2] = {DEPTH_TARGET, {0xa200, 0x2 | 0x1u << 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), depth_less, 10, 3, error, sizeof error) == -1 && strstr(error, "reading the depth"));
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
    rt_render_forget();
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
    rt_render_forget();
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
    const uint32_t depth[][2] = {{0xa200, 0x2}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), depth, 1, 0, error, sizeof error) == -1 && strstr(error, "depth or stencil"));
    const uint32_t line_strips[][2] = {{0xc242, 5}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), line_strips, 1, 0, error, sizeof error) == -1 && strstr(error, "primitive type 5"));
    depth_and_strips(x);
    const uint32_t partial[][2] = {{0xa08e, 0x7}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), partial, 1, 0, error, sizeof error) == -1 && strstr(error, "write mask"));
    const uint32_t resolve[][2] = {{0xa202, 0x3u << 4 | 0xccu << 16}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), resolve, 1, 0, error, sizeof error) == -1 && strstr(error, "colour mode 3"));
    const uint32_t unknown[][2] = {{0xa1e8, 0}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), unknown, 1, 0, error, sizeof error) == -1 && strstr(error, "0xa1e8"));
    CHECK(run_with_registers(x, FOURTH, at(TARGET), normal, 0, 2, error, sizeof error) == -1 && strstr(error, "2 instances"));
    CHECK(rt_executor_read_target(x, at(0x300000), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(rt_executor_read_target(x, at(TARGET), bgra, SIZE + 1, 1, error, sizeof error) == -1);

    /* A vertex shader that writes VCC's halves as scalars (as the game's do, before vector writes set
       VCC): drawn as before; one that then reads VCC as a lane mask stops by name at run time. */
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
            rt_render_forget();
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
            }
        }
    }

    /* A pixel shader reading the pixel's position (SPI_PS_INPUT 0x302: barycentrics, then x and y in
       v2 and v3): its colour (x / 16, y / 16) at pixel centres is the triangle's image. */
    {
        enum { POSITION_CODE = 0x2800, POSITION_TARGET = 0x700000 };
        memcpy((void *)(uintptr_t)at(POSITION_CODE), pixel_position_color, sizeof pixel_position_color);
        rt_render_forget();
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
        rt_render_forget();
        uint32_t offset = record_setup(FOURTH, at(RECT_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_register(FOURTH, offset, 0xc242, 17, error, sizeof error) == 0);
        CHECK(rt_render_record_draw_auto(FOURTH, offset + 0x10, 4, error, sizeof error) == 0);
        CHECK(rt_executor_run(x, FOURTH, offset + 0x20, error, sizeof error) == -1 && strstr(error, "whole rects"));
        memcpy(corners, saved_corners, sizeof saved_corners);
        memcpy(colors, saved_colors, sizeof saved_colors);
    }
    /* No pixel shader (a depth-only pass, placed as the runtime's sceGnmSetPsShader places it): a
       draw with it stops by name. */
    rt_render_forget();
    uint32_t offset = record_setup(FOURTH, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS];
    CHECK(rt_render_record_pixel_shader_at(FOURTH + offset, no_pixel_shader) == 0);
    CHECK(rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error) == -1 &&
          strstr(error, "no pixel shader"));

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
        rt_render_forget();
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

        rt_render_forget();
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
        rt_render_forget();
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

        /* An image no shader wrote holds the guest memory's texels: linear (tiling index 8) RGBA32F
           texels the CPU wrote read back as written; a layout not done yet (index 0) stops by name. */
        float *guest_texels = (float *)(uintptr_t)at(IMAGE_B + 0x8000);
        for (unsigned i = 0; i < LANES * 4; i++) guest_texels[i] = (float)i * 0.25f, back[i] = -1;
        for (int tiling = 0; tiling < 2; tiling++) {
            rt_render_forget();
            image_descriptor(image_b, at(IMAGE_B + 0x8000), 14, 7, LANES, 1);
            image_b[3] |= (tiling ? 8u : 0u) << 20;
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[2] >> 8), (uint32_t)(codes[2] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            const int loaded = rt_executor_run(x, IMAGES, 0x50, error, sizeof error);
            if (!tiling) {
                CHECK(loaded == -1 && strstr(error, "tiling index 0"));
                continue;
            }
            if (loaded) fprintf(stderr, "guest texels: %s\n", error);
            CHECK(loaded == 0);
            int same = 1;
            for (unsigned i = 0; i < LANES * 4; i++) same &= back[i] == (float)i * 0.25f;
            CHECK(same);
        }
    }

    sampled_images(x);
    sampled_lookup(x);
    fetch_shader(x);

    rt_executor_close(x);
    rt_render_forget();
    munmap((void *)(uintptr_t)GUEST_BASE, GUEST_SIZE);
    close(fd);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
