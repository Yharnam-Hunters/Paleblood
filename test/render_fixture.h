/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What the renderer's GPU tests share (test_render_executor.c, test_renderer.c): guest memory below
   2^40 holding a vertex and a pixel shader (gcn_programs.h) and a triangle's corners and colours, a
   command buffer's recorded calls for one draw, and the check that a 16 x 16 target holds the
   triangle: red (x + 0.5) / 16 and green (y + 0.5) / 16 at pixel (x, y), blue 0.25, alpha 1. */
#ifndef TEST_RENDER_FIXTURE_H
#define TEST_RENDER_FIXTURE_H

#include "runtime/render_calls.h"

#include "gcn_programs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { SKIPPED = 77, SIZE = 16, RECORD_STRIDE = 16 };
enum { COLOR_8_8 = 0x3, COLOR_8_8_8_8 = 0xa, SWAP_STD = 0, SWAP_ALT = 1, PERSP_CENTER = 0x2, FRONT_FACE = 0x1000 };
#define GUEST_BASE 0x3000000000ull  /* below 2^40, as the runtime's GPU-visible memory is */
#define GUEST_SIZE (64ull << 20)
/* where things sit in the guest memory */
enum { VERTEX_CODE = 0x1000, PIXEL_CODE = 0x2000, CORNERS = 0x10000, COLORS = 0x10100, TARGET = 0x100000, OTHER_TARGET = 0x200000 };

static uint64_t at(uint64_t offset) { return GUEST_BASE + offset; }

static void color_target(uint32_t words[RT_COLOR_TARGET_WORDS], uint64_t address, uint32_t format, uint32_t swap)
{
    memset(words, 0, RT_COLOR_TARGET_WORDS * sizeof *words);
    words[0] = (uint32_t)(address >> 8);
    words[1] = SIZE / 8 - 1;
    words[2] = SIZE * SIZE / 64 - 1;
    words[4] = format << 2 | swap << 11; /* number type UNORM */
}

/* A buffer descriptor: base address (48 bits), the stride in the second word's bits 29:16, and the size. */
static void descriptor(uint32_t *words, uint64_t address, uint32_t stride)
{
    words[0] = (uint32_t)address;
    words[1] = ((uint32_t)(address >> 32) & 0xffff) | stride << 16;
    words[2] = 0x1000;
    words[3] = 0;
}

/* Records one draw's calls on a command buffer; returns the bytes "written". */
static uint32_t record_draw(uint64_t buffer, uint64_t target, uint32_t format, uint32_t swap, uint32_t pixel_inputs)
{
    char error[160];
    uint32_t offset = 0x10;
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, target, format, swap);
    CHECK(rt_render_record_color_target(buffer, offset += 0x10, 0, words, error, sizeof error) == 0);
    const float scale[3] = {SIZE / 2, SIZE / 2, 1}, translate[3] = {SIZE / 2, SIZE / 2, 0};
    CHECK(rt_render_record_viewport(buffer, offset += 0x10, 0, 0, 1, scale, translate, error, sizeof error) == 0);
    const uint64_t vertex_code = at(VERTEX_CODE), pixel_code = at(PIXEL_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(vertex_code >> 8), (uint32_t)(vertex_code >> 40), 0x2, 8 << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(buffer, offset += 0x10, vertex, error, sizeof error) == 0);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(pixel_code >> 8), (uint32_t)(pixel_code >> 40), 0x2, 0, 0, 0x4,
                                                   pixel_inputs, pixel_inputs, 1, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(buffer, offset += 0x10, pixel, error, sizeof error) == 0);
    uint32_t user[8];
    descriptor(user, at(CORNERS), RECORD_STRIDE);
    descriptor(user + 4, at(COLORS), RECORD_STRIDE);
    CHECK(rt_render_record_user_data(buffer, offset += 0x10, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(buffer, offset += 0x10, 3, error, sizeof error) == 0);
    return offset + 0x10;
}

static int near(uint32_t channel, float expected) { return fabsf((float)channel - expected * 255.0f) <= 2.0f; }

/* The guest memory, mapped at GUEST_BASE, with the shaders and the triangle's data in place; its
   file descriptor, or -1. */
static int map_guest_memory(void)
{
    const int fd = memfd_create("guest-memory", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)GUEST_SIZE)) return -1;
    unsigned char *guest = mmap((void *)(uintptr_t)GUEST_BASE, GUEST_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    if (guest != (void *)(uintptr_t)GUEST_BASE) return -1;
    memcpy(guest + VERTEX_CODE, vertex_position_color, sizeof vertex_position_color);
    memcpy(guest + PIXEL_CODE, pixel_color, sizeof pixel_color);
    static const float corners[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}};
    static const float colors[3][4] = {{0, 0, 0.25f, 1}, {2, 0, 0.25f, 1}, {0, 2, 0.25f, 1}};
    memcpy(guest + CORNERS, corners, sizeof corners);
    memcpy(guest + COLORS, colors, sizeof colors);
    return fd;
}

/* Every pixel of a 16 x 16 BGRA image holds the triangle's colour. */
static void check_triangle_pixels(const uint32_t *bgra)
{
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            const int ok = near(p >> 16 & 0xff, ((float)px + 0.5f) / SIZE) && near(p >> 8 & 0xff, ((float)y + 0.5f) / SIZE) &&
                           near(p & 0xff, 0.25f) && (p >> 24) == 0xff;
            if (!ok) fprintf(stderr, "pixel (%d, %d) = %08x\n", px, y, p);
            CHECK(ok);
        }
}

#endif
