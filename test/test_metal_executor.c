/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 programs and data, translated through SPIR-V and MSL on a real Metal device.
   No game executable, capture, original-side harness or offline Metal compiler is used. */
#include "runtime/render_executor.h"
#include "runtime/render_calls.h"
#include "gcn_programs.h"
#include "metal_executor_programs.h"

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL_SIZE = 32 << 20, VERTEX_CODE = 0x1000, PIXEL_CODE = 0x2000,
       USER_PIXEL_CODE = 0x4000, UNSUPPORTED_CODE = 0x5000, STORE_VERTEX_CODE = 0x6000,
       CLIP_VERTEX_CODE = 0x7000, CLIP_EIGHT_CODE = 0x8000, TWO_VERTEX_CODE = 0x9000,
       TWO_PIXEL_CODE = 0xa000, FLOAT_PIXEL_CODE = 0xb000, FORMAT_VERTEX_CODE = 0xc000,
       FORMAT_STORE_CODE = 0xd000, FORMAT_PIXEL_CODE = 0xe000, COMPUTE_CODE = 0xf000,
       POSITION_PIXEL_CODE = 0x18000, VCC_VERTEX_CODE = 0x19000, VCC_PIXEL_CODE = 0x1a000, QUAD_PIXEL_CODE = 0x1b000,
       IMAGE_PIXEL_CODE = 0x1c000, IMAGE_MIP_PIXEL_CODE = 0x1d000,
       SAMPLE_PIXEL_CODE = 0x1e000, MEMORY_IMAGE_PIXEL_CODE = 0x1f000,
       FETCH_CALLEE_CODE = 0x26000, FETCH_CALLER_CODE = 0x27000, FETCH_DESCRIPTOR_TABLE = 0x25000,
       STORE_X_VERTEX_CODE = 0x28000, PACKED_COLORS = 0x29000,
       CORNERS = 0x10000, COLORS = 0x10100, SOLID_COLORS = 0x10200, VERTEX_WRITES = 0x11000,
       INDEX_CORNERS = 0x12000, INDEX_COLORS = 0x13000, INDICES16 = 0x14000, INDICES32 = 0x14100,
       FORMAT_WRITES = 0x17000 };
/* LLVM llvm-mc -triple=amdgcn -mcpu=bonaire --show-encoding, as for gcn_programs.h.
   Draw the same triangle, and write an owned marker for each vertex through s[8:11]. */
static const uint32_t vertex_store_witness[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70,           /* s_waitcnt vmcnt(0) */
    0x7e1202ff, 0x13572468, /* v_mov_b32_e32 v9, 0x13572468 */
    0x34140082,           /* v_lshlrev_b32_e32 v10, 2, v0 */
    0xe0701000, 0x8002090a, /* buffer_store_dword v9, v10, s[8:11], 0 offen */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000,           /* s_endpgm */
};
static const uint64_t guest_base = 0x3000000000ull;
static const uint32_t draw_indexed_header = 0xc0042700u, draw_indexed_offset_header = 0xc0033500u;
enum { DST_XYZW = 4 | 5 << 3 | 6 << 6 | 7 << 9, DST_ZYXW = 6 | 5 << 3 | 4 << 6 | 7 << 9 };
static unsigned failures, checks, scenarios;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)
static uint64_t at(unsigned offset) { return guest_base + offset; }

static void descriptor(uint32_t *words, uint64_t address)
{
    words[0] = (uint32_t)address;
    words[1] = (uint32_t)(address >> 32) | 16u << 16;
    words[2] = 0x1000;
    words[3] = 0;
}

static void vertex_registers(uint32_t words[RT_VERTEX_SHADER_WORDS], unsigned code_offset)
{
    memset(words, 0, RT_VERTEX_SHADER_WORDS * sizeof *words);
    const uint64_t code = at(code_offset);
    words[0] = (uint32_t)(code >> 8); words[1] = (uint32_t)(code >> 40);
    words[2] = 2; words[3] = 8 << 1; words[5] = 4;
}

static void pixel_registers(uint32_t words[RT_PIXEL_SHADER_WORDS], unsigned code_offset)
{
    memset(words, 0, RT_PIXEL_SHADER_WORDS * sizeof *words);
    const uint64_t code = at(code_offset);
    words[0] = (uint32_t)(code >> 8); words[1] = (uint32_t)(code >> 40);
    words[2] = 2; words[5] = 4; words[6] = 2; words[7] = 2; words[8] = 1; words[11] = 15;
}

static uint32_t record_state_setup(uint64_t buffer, uint64_t target, unsigned width, unsigned height,
                                   unsigned swap, const float scale[3], const float translate[3])
{
    char error[256] = "";
    uint32_t color[RT_COLOR_TARGET_WORDS] = {0};
    color[0] = (uint32_t)(target >> 8);
    color[1] = width / 8 - 1;
    color[2] = width * height / 64 - 1;
    color[4] = 0xau << 2 | swap << 11; color[5] = 31;
    CHECK(rt_render_record_color_target(buffer, 0x10, 0, color, error, sizeof error) == 0);
    CHECK(rt_render_record_viewport(buffer, 0x20, 0, 0, 1, scale, translate, error, sizeof error) == 0);
    const uint64_t vc = at(VERTEX_CODE), pc = at(PIXEL_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(vc >> 8), (uint32_t)(vc >> 40), 2, 8 << 1, 0, 4, 0};
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(pc >> 8), (uint32_t)(pc >> 40), 2, 0, 0, 4, 2, 2, 1, 0, 0, 0xf};
    CHECK(rt_render_record_vertex_shader(buffer, 0x30, vertex, error, sizeof error) == 0);
    CHECK(rt_render_record_pixel_shader(buffer, 0x40, pixel, error, sizeof error) == 0);
    return 0x50;
}

static uint32_t record_setup(uint64_t buffer, uint64_t target, unsigned width, unsigned height,
                            unsigned swap, const float scale[3], const float translate[3], unsigned colors)
{
    const uint32_t size = record_state_setup(buffer, target, width, height, swap, scale, translate);
    char error[256] = "";
    uint32_t user[8];
    descriptor(user, at(CORNERS)); descriptor(user + 4, at(colors));
    CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0);
    return size + 0x10;
}

static uint32_t record_draw(uint64_t buffer, uint64_t target, unsigned width, unsigned height,
                            unsigned swap, const float scale[3], const float translate[3], unsigned colors)
{
    char error[256] = "";
    uint32_t offset = record_setup(buffer, target, width, height, swap, scale, translate, colors);
    CHECK(rt_render_record_draw_auto(buffer, offset, 3, error, sizeof error) == 0);
    return offset + 0x10;
}

static uint32_t record_index_setup(uint64_t buffer, uint64_t target, unsigned type)
{
    const float scale[3] = {8, 8, 1}, translate[3] = {8, 8, 0};
    uint32_t offset = record_setup(buffer, target, 16, 16, 0, scale, translate, COLORS);
    char error[256] = "";
    uint32_t user[8];
    descriptor(user, at(INDEX_CORNERS)); descriptor(user + 4, at(INDEX_COLORS));
    CHECK(rt_render_record_user_data(buffer, offset, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0);
    offset += 0x10;
    CHECK(rt_render_record_index_type(buffer, offset, type, error, sizeof error) == 0);
    return offset + 0x10;
}

static int run(rt_executor *x, uint64_t buffer, uint32_t size)
{
    char error[1024] = "";
    int result = rt_executor_run(x, buffer, size, error, sizeof error);
    if (result) fprintf(stderr, "run: %s\n", error);
    CHECK(result == 0);
    return result;
}

static int near(unsigned channel, float expected)
{
    return fabsf((float)channel - expected * 255.0f) <= 2.0f;
}

static float srgb(float linear)
{
    return linear <= 0.0031308f ? 12.92f * linear : 1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
}

static void expect_error(rt_executor *x, uint64_t buffer, uint32_t size, const char *reason)
{
    char error[1024] = "";
    int result = rt_executor_run(x, buffer, size, error, sizeof error);
    if (result != -1 || !strstr(error, reason)) fprintf(stderr, "expected '%s', got %d: %s\n", reason, result, error);
    CHECK(result == -1 && strstr(error, reason));
}

static void check_solid(rt_executor *x, uint64_t target)
{
    uint32_t pixels[16 * 16];
    char error[256] = "";
    CHECK(rt_executor_read_target(x, target, pixels, 16, 16, error, sizeof error) == 0);
    unsigned bad = 0;
    for (unsigned i = 0; i < 16 * 16; ++i)
        if (!near(pixels[i] >> 16 & 255, 0.75f) || !near(pixels[i] >> 8 & 255, 0.125f) ||
            !near(pixels[i] & 255, 0.5f) || pixels[i] >> 24 != 255) bad++;
    CHECK(bad == 0);
}

static void check_gradient(rt_executor *x, uint64_t target, unsigned width, unsigned height, int reverse_y)
{
    uint32_t pixels[64 * 64];
    char error[256] = "";
    CHECK(width * height <= 64 * 64);
    int result = rt_executor_read_target(x, target, pixels, width, height, error, sizeof error);
    if (result) fprintf(stderr, "read: %s\n", error);
    CHECK(result == 0);
    if (result) return;
    unsigned bad = 0;
    for (unsigned y = 0; y < height; ++y) for (unsigned col = 0; col < width; ++col) {
        uint32_t p = pixels[y * width + col];
        float green = ((float)y + 0.5f) / height;
        if (reverse_y) green = 1 - green;
        if (!near(p >> 16 & 255, ((float)col + 0.5f) / width) || !near(p >> 8 & 255, green) ||
            !near(p & 255, 0.25f) || p >> 24 != 255) {
            if (!bad) fprintf(stderr, "first unexpected pixel (%u,%u): %08x\n", col, y, p);
            bad++;
        }
    }
    CHECK(bad == 0);
}

static void check_clipped_gradient(rt_executor *x, uint64_t target, unsigned left, unsigned top,
                                    unsigned right, unsigned bottom)
{
    uint32_t pixels[16 * 16];
    char error[256] = "";
    CHECK(rt_executor_read_target(x, target, pixels, 16, 16, error, sizeof error) == 0);
    unsigned bad = 0;
    for (unsigned y = 0; y < 16; ++y) for (unsigned col = 0; col < 16; ++col) {
        uint32_t p = pixels[y * 16 + col];
        if (col < left || col >= right || y < top || y >= bottom) { if (p) bad++; }
        else if (!near(p >> 16 & 255, ((float)col + 0.5f) / 16) ||
            !near(p >> 8 & 255, ((float)y + 0.5f) / 16) || !near(p & 255, 0.25f) || p >> 24 != 255) bad++;
    }
    CHECK(bad == 0);
}

static void check_two_parameters(rt_executor *x, uint64_t target)
{
    uint32_t pixels[16 * 16]; char error[256] = "";
    CHECK(rt_executor_read_target(x, target, pixels, 16, 16, error, sizeof error) == 0);
    unsigned bad = 0;
    for (unsigned y = 0; y < 16; ++y) for (unsigned col = 0; col < 16; ++col) {
        uint32_t p = pixels[y * 16 + col];
        if (!near(p >> 16 & 255, ((float)y + 0.5f) / 16) ||
            !near(p >> 8 & 255, ((float)col + 0.5f) / 16) || !near(p & 255, 0.5f) || p >> 24 != 255) bad++;
    }
    CHECK(bad == 0);
}

/* Fetch cases use the same owned programs as the common translator tests. They start with no
   implicit user writes, so the call target and descriptor-table reads each have their own witness. */
static uint32_t record_fetch_setup(uint64_t buffer, unsigned user_count, unsigned pair,
                                   uint64_t fetch, uint64_t table, uint32_t written)
{
    const float scale[3] = {8, 8, 1}, translate[3] = {8, 8, 0};
    char error[256] = "";
    uint32_t size = record_state_setup(buffer, at(0x930000), 16, 16, 0, scale, translate);
    uint32_t vertex[RT_VERTEX_SHADER_WORDS]; vertex_registers(vertex, FETCH_CALLER_CODE);
    vertex[2] = 0x42; vertex[3] = user_count << 1;
    CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
    uint32_t data[16] = {0};
    data[2] = (uint32_t)table; data[3] = (uint32_t)(table >> 32);
    if (pair < 15) { data[pair] = (uint32_t)fetch; data[pair + 1] = (uint32_t)(fetch >> 32); }
    for (unsigned slot = 0; slot < 16; ++slot) if (written & (1u << slot)) {
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, slot, data + slot, 1, error, sizeof error) == 0);
        size += 0x10;
    }
    return size;
}

static void check_fetch_pixels(rt_executor *x, int solid, int swap)
{
    uint32_t pixels[16 * 16]; char error[256] = "";
    CHECK(rt_executor_read_target(x, at(0x930000), pixels, 16, 16, error, sizeof error) == 0);
    unsigned bad = 0;
    for (unsigned y = 0; y < 16; ++y) for (unsigned col = 0; col < 16; ++col) {
        float red = solid ? 0.75f : ((float)col + 0.5f) / 16;
        float green = solid ? 0.125f : ((float)y + 0.5f) / 16;
        const float blue = solid ? 0.5f : 0.25f;
        if (swap) { const float previous = red; red = green; green = previous; }
        const uint32_t pixel = pixels[y * 16 + col];
        if (!near(pixel >> 16 & 255, red) || !near(pixel >> 8 & 255, green) ||
            !near(pixel & 255, blue) || pixel >> 24 != 255) bad++;
    }
    CHECK(bad == 0);
}

static void fetch_draw(rt_executor *x, uint64_t buffer, uint32_t size, int solid, int swap)
{
    char error[256] = "";
    CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0);
    if (!run(x, buffer, size + 0x10)) check_fetch_pixels(x, solid, swap);
    scenarios++;
}

static void check_fetch_shaders(rt_executor *x, unsigned char *pool)
{
    enum { SECOND_FETCH = 0x2a000, BAD_FETCH = 0x2b000 };
    uint32_t descriptors[12];
    descriptor(descriptors, at(CORNERS)); descriptor(descriptors + 4, at(COLORS)); descriptor(descriptors + 8, at(SOLID_COLORS));
    for (unsigned i = 0; i < 3; ++i) descriptors[4 * i + 3] = DST_XYZW | 7u << 12 | 14u << 15;
    memcpy(pool + FETCH_DESCRIPTOR_TABLE, descriptors, sizeof descriptors);
    memcpy(pool + SECOND_FETCH, fetch_position_color, sizeof fetch_position_color);
    ((uint32_t *)(pool + SECOND_FETCH))[1] = 0xc0840308; /* LLVM: s_load_dwordx4 s[8:11], s[2:3], 0x8 */
    uint64_t buffer = 2000;
    char error[1024] = "";
    uint32_t size;
    rt_executor_reset(x);
    size = record_fetch_setup(buffer, 4, 0, at(FETCH_CALLEE_CODE), at(FETCH_DESCRIPTOR_TABLE), 15);
    fetch_draw(x, buffer++, size, 0, 0);

    /* Only the target pair changes: same caller, table and cached state, different fetch body. */
    uint64_t address = at(SECOND_FETCH);
    uint32_t target[2] = {(uint32_t)address, (uint32_t)(address >> 32)};
    CHECK(rt_render_record_user_data(buffer, 0x10, RT_STAGE_VERTEX, 0, target, 2, error, sizeof error) == 0);
    fetch_draw(x, buffer++, 0x20, 1, 0);
    ((uint32_t *)(pool + SECOND_FETCH))[1] = 0xc0840304;
    fetch_draw(x, buffer++, 0x10, 0, 0); /* Same target address, live callee bytes changed. */
    ((uint32_t *)(pool + SECOND_FETCH))[1] = 0xc0840308;
    fetch_draw(x, buffer++, 0x10, 1, 0);

    /* Same caller address, different exports. Recheck its complete bytes as well. */
    ((uint32_t *)(pool + FETCH_CALLER_CODE))[3] = 0x08070506; /* LLVM: exp param0, v6, v5, v7, v8 */
    fetch_draw(x, buffer++, 0x10, 1, 1);
    memcpy(pool + FETCH_CALLER_CODE, vertex_calling_fetch, sizeof vertex_calling_fetch);
    fetch_draw(x, buffer++, 0x10, 1, 0);

    /* The pixel stage's data cannot redirect the fetch. */
    const uint32_t poison[4] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    CHECK(rt_render_record_user_data(buffer, 0x10, RT_STAGE_PIXEL, 0, poison, 4, error, sizeof error) == 0);
    fetch_draw(x, buffer++, 0x20, 1, 0);

    /* Both target words must be written even on a cache hit; a callee also needs its table pointer. */
    const struct { uint32_t written; const char *reason; } missing[] = {
        {14, "vertex fetch target shader reads unset user data 0x1"},
        {13, "vertex fetch target shader reads unset user data 0x2"},
        {3, "vertex shader reads unset user data 0xc"},
        {7, "vertex shader reads unset user data 0x8"},
        {11, "vertex shader reads unset user data 0x4"},
    };
    for (unsigned i = 0; i < sizeof missing / sizeof missing[0]; ++i) {
        rt_executor_reset(x);
        size = record_fetch_setup(buffer, 4, 0, at(SECOND_FETCH), at(FETCH_DESCRIPTOR_TABLE), missing[i].written);
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0);
        expect_error(x, buffer++, size + 0x10, missing[i].reason);
        check_fetch_pixels(x, 1, 0);
        scenarios++;
    }

    /* A zero low target word is explicitly set; unused high pointer bits are outside GCN's 48-bit address. */
    memcpy(pool, fetch_position_color, sizeof fetch_position_color);
    rt_executor_reset(x);
    size = record_fetch_setup(buffer, 4, 0, guest_base | 0xabcd000000000000ull, at(FETCH_DESCRIPTOR_TABLE), 15);
    fetch_draw(x, buffer++, size, 0, 0);

    /* The last complete SGPR pair is supported; a neighboring incomplete pair is refused before a shift/read. */
    ((uint32_t *)(pool + FETCH_CALLER_CODE))[0] = 0xbe80210e; /* LLVM: s_swappc_b64 s[0:1], s[14:15] */
    rt_executor_reset(x);
    size = record_fetch_setup(buffer, 16, 14, at(SECOND_FETCH), at(FETCH_DESCRIPTOR_TABLE), (3u << 14) | 12);
    fetch_draw(x, buffer++, size, 1, 0);
    const struct { unsigned pair, user; } outside_user[] = {{14, 15}, {15, 16}, {16, 16}, {104, 16}};
    for (unsigned i = 0; i < sizeof outside_user / sizeof outside_user[0]; ++i) {
        ((uint32_t *)(pool + FETCH_CALLER_CODE))[0] = 0xbe802100u | outside_user[i].pair;
        rt_executor_reset(x);
        size = record_fetch_setup(buffer, outside_user[i].user, 0, at(SECOND_FETCH), at(FETCH_DESCRIPTOR_TABLE), 15);
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0);
        expect_error(x, buffer++, size + 0x10, "outside its user data"); scenarios++;
    }
    memcpy(pool + FETCH_CALLER_CODE, vertex_calling_fetch, sizeof vertex_calling_fetch);

    const uint64_t invalid[] = {guest_base - 4, guest_base + POOL_SIZE, at(FETCH_CALLEE_CODE) + 1, 0};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        rt_executor_reset(x);
        size = record_fetch_setup(buffer, 4, 0, invalid[i], at(FETCH_DESCRIPTOR_TABLE), 15);
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0);
        expect_error(x, buffer++, size + 0x10, "fetch shader code is outside the GPU-visible pool or unaligned"); scenarios++;
    }

    /* A return can occupy the last pool dword; an instruction needing a literal cannot. No vertex is fetched in this draw. */
    uint32_t *last = (uint32_t *)(pool + POOL_SIZE - 4); *last = 0xbe802000;
    rt_executor_reset(x);
    size = record_fetch_setup(buffer, 4, 0, at(POOL_SIZE - 4), at(FETCH_DESCRIPTOR_TABLE), 15);
    CHECK(rt_render_record_draw_auto(buffer, size, 0, error, sizeof error) == 0);
    run(x, buffer++, size + 0x10); scenarios++;
    *last = 0x7e1202ff; /* v_mov_b32 v9, literal, with no literal word in the pool */
    rt_executor_reset(x);
    size = record_fetch_setup(buffer, 4, 0, at(POOL_SIZE - 4), at(FETCH_DESCRIPTOR_TABLE), 15);
    CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0);
    expect_error(x, buffer++, size + 0x10, "fetch shader has no complete GFX7 program"); scenarios++;

    const struct { uint32_t words[3]; unsigned count; const char *reason; } rejected[] = {
        {{0xffffffff}, 1, "fetch shader has no complete GFX7 program"},
        {{0xbf810000}, 1, "only straight-line callees"},
        {{0xbe802002}, 1, "not the call's return pair"},
        {{0xbf820000, 0xbe802000}, 2, "only straight-line callees"},
        {{0xbe802100, 0xbe802000}, 2, "only straight-line callees"},
        {{0xbe800380, 0xbe802000}, 2, "only straight-line callees"},
    };
    for (unsigned i = 0; i < sizeof rejected / sizeof rejected[0]; ++i) {
        memcpy(pool + BAD_FETCH, rejected[i].words, rejected[i].count * sizeof(uint32_t));
        rt_executor_reset(x);
        size = record_fetch_setup(buffer, 4, 0, at(BAD_FETCH), at(FETCH_DESCRIPTOR_TABLE), 15);
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0);
        expect_error(x, buffer++, size + 0x10, rejected[i].reason); scenarios++;
    }

    /* Call analysis refuses a changed target or a second call without looking through that target. */
    uint32_t *caller = (uint32_t *)(pool + FETCH_CALLER_CODE);
    for (unsigned kind = 0; kind < 2; ++kind) {
        caller[0] = kind ? 0xbe802100 : 0xbe800380;
        memcpy(caller + 1, vertex_calling_fetch, sizeof vertex_calling_fetch);
        rt_executor_reset(x);
        size = record_fetch_setup(buffer, 4, 0, 0, 0, 0);
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0);
        expect_error(x, buffer++, size + 0x10, kind ? "more than one call" : "after s_mov_b32"); scenarios++;
    }
    memcpy(caller, vertex_calling_fetch, sizeof vertex_calling_fetch);

    /* Both index widths feed the fetch's vertex index, including a nonzero index offset. */
    descriptor(descriptors, at(INDEX_CORNERS)); descriptor(descriptors + 4, at(INDEX_COLORS));
    descriptors[3] = descriptors[7] = DST_XYZW | 7u << 12 | 14u << 15;
    memcpy(pool + FETCH_DESCRIPTOR_TABLE, descriptors, sizeof descriptors);
    for (unsigned width = 0; width < 2; ++width) {
        rt_executor_reset(x);
        size = record_fetch_setup(buffer, 4, 0, at(FETCH_CALLEE_CODE), at(FETCH_DESCRIPTOR_TABLE), 15);
        CHECK(rt_render_record_index_type(buffer, size, width, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_index_base(buffer, size, at(width ? INDICES32 : INDICES16), error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_index_offset_packet(buffer, size, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0);
        if (!run(x, buffer++, size + 0x10)) check_fetch_pixels(x, 0, 0);
        scenarios++;
    }
    rt_executor_reset(x);
}

/* A state-only buffer feeds indexed draws in later buffers, including error and reset boundaries. */
static void check_persistent_state(rt_executor *x, unsigned char *pool)
{
    const uint64_t target = at(0xd00000), setup_buffer = 1000, draw_buffer = 1001;
    const uint64_t unmask_buffer = 1002, reject_buffer = 1003, retained_buffer = 1004, fresh_buffer = 1005;
    uint32_t vertex[RT_VERTEX_SHADER_WORDS], words[4];
    uint32_t *markers = (uint32_t *)(pool + VERTEX_WRITES);
    char error[1024] = "";
    rt_executor_reset(x);
    uint32_t size = record_index_setup(setup_buffer, target, 1);
    vertex_registers(vertex, STORE_VERTEX_CODE); vertex[2] = 0x43; vertex[3] = 12 << 1;
    CHECK(rt_render_record_vertex_shader(setup_buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
    descriptor(words, at(VERTEX_WRITES));
    CHECK(rt_render_record_user_data(setup_buffer, size, RT_STAGE_VERTEX, 8, words, 4, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_index_base(setup_buffer, size, at(INDICES32), error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_register(setup_buffer, size, 0xa08e, 0, error, sizeof error) == 0); size += 0x10;
    run(x, setup_buffer, size);
    uint32_t untouched = 0xdeadbeef;
    CHECK(rt_executor_read_target(x, target, &untouched, 1, 1, error, sizeof error) == -1 && untouched == 0xdeadbeef);
    memset(markers, 0, 7 * sizeof *markers);
    CHECK(rt_render_record_draw_index_offset_packet(draw_buffer, 0x10, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0);
    if (!run(x, draw_buffer, 0x20)) {
        CHECK(markers[0] == 0 && markers[1] == 0 && markers[2] == 0 && markers[6] == 0);
        CHECK(markers[3] == 0x13572468 && markers[4] == 0x13572468 && markers[5] == 0x13572468);
        check_clipped_gradient(x, target, 0, 0, 0, 0);
    }
    scenarios++;

    /* Only the write mask changes; shaders, SGPRs, viewport and 32-bit index state still apply. */
    CHECK(rt_render_record_register(unmask_buffer, 0x10, 0xa08e, 15, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_index_offset_packet(unmask_buffer, 0x20, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0);
    if (!run(x, unmask_buffer, 0x30)) check_gradient(x, target, 16, 16, 0);
    scenarios++;

    /* An unsupported event stops in order; the preceding mask update is not rolled back. */
    CHECK(rt_render_record_register(reject_buffer, 0x10, 0xa08e, 0, error, sizeof error) == 0);
    CHECK(rt_render_record_register(reject_buffer, 0x20, 0xffff, 0, error, sizeof error) == 0);
    expect_error(x, reject_buffer, 0x30, "register 0xffff");
    const float solid[3][4] = {{0.75f, 0.125f, 0.5f, 1}, {0.75f, 0.125f, 0.5f, 1}, {0.75f, 0.125f, 0.5f, 1}};
    memcpy(pool + INDEX_COLORS + 3 * sizeof solid[0], solid, sizeof solid);
    memset(markers, 0, 7 * sizeof *markers);
    CHECK(rt_render_record_draw_index_offset_packet(retained_buffer, 0x10, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0);
    if (!run(x, retained_buffer, 0x20)) {
        CHECK(markers[3] == 0x13572468 && markers[4] == 0x13572468 && markers[5] == 0x13572468 && markers[6] == 0);
        check_gradient(x, target, 16, 16, 0);
    }
    scenarios++;

    /* Reset forgets both index state and the masked state, leaving completed pixels intact. */
    rt_executor_reset(x);
    expect_error(x, draw_buffer, 0x20, "indexed draw requires INDEX_TYPE");
    check_gradient(x, target, 16, 16, 0);
    scenarios++;
    size = record_index_setup(fresh_buffer, target, 1);
    CHECK(rt_render_record_index_base(fresh_buffer, size, at(INDICES32), error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_index_offset_packet(fresh_buffer, size, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0); size += 0x10;
    if (!run(x, fresh_buffer, size)) check_solid(x, target);
    const float colors[3][4] = {{0, 0, 0.25f, 1}, {2, 0, 0.25f, 1}, {0, 2, 0.25f, 1}};
    memcpy(pool + INDEX_COLORS + 3 * sizeof colors[0], colors, sizeof colors);
    scenarios++;
    rt_executor_reset(x);
}

/* Explicit zero is a defined value; leaving a required slot unset must fail, including cache hits. */
static void check_written_user_data(const rt_executor_memory *memory, unsigned char *pool)
{
    rt_executor *executor = NULL;
    char error[1024] = "";
    CHECK(rt_executor_open(memory, &executor, error, sizeof error) == 0);
    if (!executor) return;
    const float scale[3] = {8, 8, 1}, translate[3] = {8, 8, 0};
    const float colors[3][4] = {{0, 0, 0.25f, 1}, {2, 0, 0.25f, 1}, {0, 2, 0.25f, 1}};
    memcpy(pool, colors, sizeof colors); /* A missing low base word still points safely inside owned memory. */
    for (unsigned stage = 0; stage < 2; ++stage) {
        const uint64_t target = at(0x1100000 + stage * 0x10000);
        for (unsigned cached = 0; cached < 2; ++cached) {
            rt_executor_reset(executor);
            const uint64_t setup = 1600 + stage * 8 + cached * 4, missing = setup + 1, defined = setup + 2;
            uint32_t size = record_setup(setup, target, 16, 16, 0, scale, translate, COLORS);
            const uint32_t zero = 0;
            if (!stage) {
                /* Submit before the full VS user-data write, then leave only its low color base unset. */
                run(executor, setup, 0x50);
                uint32_t user[8]; descriptor(user, at(CORNERS)); descriptor(user + 4, guest_base);
                CHECK(rt_render_record_user_data(missing, 0x10, RT_STAGE_VERTEX, 0, user, 4, error, sizeof error) == 0);
                CHECK(rt_render_record_user_data(missing, 0x20, RT_STAGE_VERTEX, 5, user + 5, 3, error, sizeof error) == 0);
                CHECK(rt_render_record_draw_auto(missing, 0x30, 3, error, sizeof error) == 0);
                expect_error(executor, missing, 0x40, "vertex shader reads unset user data");
            } else {
                uint32_t pixel[RT_PIXEL_SHADER_WORDS]; pixel_registers(pixel, USER_PIXEL_CODE); pixel[3] = 4 << 1;
                CHECK(rt_render_record_pixel_shader(setup, size, pixel, error, sizeof error) == 0); size += 0x10;
                run(executor, setup, size);
                CHECK(rt_render_record_user_data(missing, 0x10, RT_STAGE_PIXEL, 0, &zero, 1, error, sizeof error) == 0);
                CHECK(rt_render_record_draw_auto(missing, 0x20, 3, error, sizeof error) == 0);
                expect_error(executor, missing, 0x30, "pixel shader reads unset user data");
            }
            scenarios++;
            if (!cached) {
                uint32_t guard = 0xdeadbeef;
                CHECK(rt_executor_read_target(executor, target, &guard, 1, 1, error, sizeof error) == -1 && guard == 0xdeadbeef);
                if (!stage) CHECK(rt_render_record_user_data(defined, 0x10, RT_STAGE_VERTEX, 4, &zero, 1, error, sizeof error) == 0);
                else {
                    const uint32_t rest[3] = {0, 0, 0x3f800000};
                    CHECK(rt_render_record_user_data(defined, 0x10, RT_STAGE_PIXEL, 1, rest, 3, error, sizeof error) == 0);
                }
                CHECK(rt_render_record_draw_auto(defined, 0x20, 3, error, sizeof error) == 0);
                run(executor, defined, 0x30);
                scenarios++;
            }
            if (!stage) check_gradient(executor, target, 16, 16, 0);
            else {
                uint32_t pixels[16 * 16];
                int read = rt_executor_read_target(executor, target, pixels, 16, 16, error, sizeof error);
                CHECK(read == 0);
                if (!read) {
                    unsigned bad = 0;
                    for (unsigned i = 0; i < 16 * 16; ++i) if (pixels[i] != 0xff000000) bad++;
                    CHECK(bad == 0);
                }
            }
        }
    }
    rt_executor_close(executor);
}

int main(void)
{
    char name[] = "/tmp/paleblood-metal-pool-XXXXXX";
    int fd = mkstemp(name);
    CHECK(fd >= 0);
    if (fd < 0) return 1;
    unlink(name);
    CHECK(0x1110000 + 16 * 16 * 4 <= POOL_SIZE); /* Last owned render target fits its backing allocation. */
    CHECK(ftruncate(fd, POOL_SIZE) == 0);
    unsigned char *pool = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(pool != MAP_FAILED);
    if (pool == MAP_FAILED) { close(fd); return 1; }
    memcpy(pool + VERTEX_CODE, vertex_position_color, sizeof vertex_position_color);
    memcpy(pool + PIXEL_CODE, pixel_color, sizeof pixel_color);
    memcpy(pool + USER_PIXEL_CODE, pixel_user_color, sizeof pixel_user_color);
    memcpy(pool + UNSUPPORTED_CODE, pixel_other_barycentrics, sizeof pixel_other_barycentrics);
    memcpy(pool + STORE_VERTEX_CODE, vertex_store_witness, sizeof vertex_store_witness);
    memcpy(pool + CLIP_VERTEX_CODE, vertex_clip, sizeof vertex_clip);
    memcpy(pool + CLIP_EIGHT_CODE, vertex_clip_eight, sizeof vertex_clip_eight);
    memcpy(pool + TWO_VERTEX_CODE, vertex_two_parameters, sizeof vertex_two_parameters);
    memcpy(pool + TWO_PIXEL_CODE, pixel_two_parameters, sizeof pixel_two_parameters);
    memcpy(pool + FLOAT_PIXEL_CODE, pixel_user_float32, sizeof pixel_user_float32);
    memcpy(pool + FORMAT_VERTEX_CODE, vertex_descriptor_format, sizeof vertex_descriptor_format);
    memcpy(pool + FORMAT_STORE_CODE, vertex_store_format, sizeof vertex_store_format);
    memcpy(pool + FORMAT_PIXEL_CODE, pixel_descriptor_format, sizeof pixel_descriptor_format);
    memcpy(pool + COMPUTE_CODE, scale_add, sizeof scale_add);
    memcpy(pool + POSITION_PIXEL_CODE, pixel_position_color, sizeof pixel_position_color);
    memcpy(pool + VCC_VERTEX_CODE, vertex_vcc_unknown, sizeof vertex_vcc_unknown);
    memcpy(pool + VCC_PIXEL_CODE, pixel_vcc_unknown, sizeof pixel_vcc_unknown);
    memcpy(pool + QUAD_PIXEL_CODE, pixel_whole_quad, sizeof pixel_whole_quad);
    memcpy(pool + IMAGE_PIXEL_CODE, pixel_unbound_image, sizeof pixel_unbound_image);
    memcpy(pool + IMAGE_MIP_PIXEL_CODE, pixel_unbound_image_mip, sizeof pixel_unbound_image_mip);
    memcpy(pool + SAMPLE_PIXEL_CODE, pixel_unbound_sample, sizeof pixel_unbound_sample);
    memcpy(pool + MEMORY_IMAGE_PIXEL_CODE, pixel_unbound_memory_image, sizeof pixel_unbound_memory_image);
    memcpy(pool + FETCH_CALLEE_CODE, fetch_position_color, sizeof fetch_position_color);
    memcpy(pool + FETCH_CALLER_CODE, vertex_calling_fetch, sizeof vertex_calling_fetch);
    memcpy(pool + STORE_X_VERTEX_CODE, vertex_store_format_x, sizeof vertex_store_format_x);
    const float corners[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}};
    const float colors[3][4] = {{0, 0, 0.25f, 1}, {2, 0, 0.25f, 1}, {0, 2, 0.25f, 1}};
    const float solid[3][4] = {{0.75f, 0.125f, 0.5f, 1}, {0.75f, 0.125f, 0.5f, 1}, {0.75f, 0.125f, 0.5f, 1}};
    memcpy(pool + CORNERS, corners, sizeof corners);
    memcpy(pool + COLORS, colors, sizeof colors);
    memcpy(pool + SOLID_COLORS, solid, sizeof solid);
    /* Indices select vertices 3..5; an accidental Auto draw of 0..2 is degenerate. */
    memcpy(pool + INDEX_CORNERS + 3 * sizeof corners[0], corners, sizeof corners);
    memcpy(pool + INDEX_COLORS + 3 * sizeof colors[0], colors, sizeof colors);
    const uint16_t indices16[] = {0, 3, 4, 5};
    const uint32_t indices32[] = {0, 3, 4, 5};
    memcpy(pool + INDICES16, indices16, sizeof indices16);
    memcpy(pool + INDICES32, indices32, sizeof indices32);
    rt_executor *x = NULL;
    char error[1024] = "";
    const rt_executor_memory memory = {fd, POOL_SIZE, guest_base};
    int opened = rt_executor_open(&memory, &x, error, sizeof error);
    if (opened) fprintf(stderr, "open: %s\n", error);
    CHECK(opened == 0);
    if (opened) { munmap(pool, POOL_SIZE); close(fd); return 1; }
    rt_render_forget();

    const float scale[3] = {8, 8, 1}, translate[3] = {8, 8, 0};
    uint32_t size = record_draw(1, at(0x100000), 16, 16, 0, scale, translate, COLORS);
    if (!run(x, 1, size)) check_gradient(x, at(0x100000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    size = record_draw(2, at(0x200000), 16, 16, 1, scale, translate, COLORS);
    if (!run(x, 2, size)) check_gradient(x, at(0x200000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    const float wide_scale[3] = {16, 8, 1}, wide_translate[3] = {16, 8, 0};
    size = record_draw(3, at(0x300000), 32, 16, 0, wide_scale, wide_translate, COLORS);
    if (!run(x, 3, size)) check_gradient(x, at(0x300000), 32, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    const float negative_y[3] = {8, -8, 1};
    size = record_draw(4, at(0x310000), 16, 16, 0, negative_y, translate, COLORS);
    if (!run(x, 4, size)) check_gradient(x, at(0x310000), 16, 16, 1);
    scenarios++;
    rt_executor_reset(x);

    /* A partial SGPR write changes only the colour descriptor, after the first draw. */
    size = record_draw(5, at(0x320000), 16, 16, 0, scale, translate, COLORS);
    uint32_t changed[4]; descriptor(changed, at(SOLID_COLORS));
    CHECK(rt_render_record_user_data(5, 0x70, RT_STAGE_VERTEX, 4, changed, 4, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(5, 0x80, 3, error, sizeof error) == 0);
    if (!run(x, 5, size)) check_gradient(x, at(0x320000), 16, 16, 0);
    if (!run(x, 5, 0x90)) check_solid(x, at(0x320000));
    scenarios++;
    rt_executor_reset(x);

    /* Writes through the caller's mapping are visible without re-recording or reopening. */
    size = record_draw(6, at(0x330000), 16, 16, 0, scale, translate, COLORS);
    memcpy(pool + COLORS, solid, sizeof solid);
    if (!run(x, 6, size)) check_solid(x, at(0x330000));
    memcpy(pool + COLORS, colors, sizeof colors);
    if (!run(x, 6, size)) check_gradient(x, at(0x330000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    /* The pixel stage has its own user-data range; it must not overwrite vertex descriptors. */
    size = record_draw(15, at(0x3a0000), 16, 16, 0, scale, translate, COLORS);
    uint64_t user_code = at(USER_PIXEL_CODE);
    const uint32_t user_pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(user_code >> 8), (uint32_t)(user_code >> 40),
        2, 4 << 1, 0, 4, 2, 2, 1, 0, 0, 0xf};
    uint32_t pixel_words[4]; memcpy(pixel_words, solid[0], sizeof pixel_words);
    CHECK(rt_render_record_pixel_shader(15, 0x70, user_pixel, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(15, 0x80, RT_STAGE_PIXEL, 0, pixel_words, 4, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(15, 0x90, 3, error, sizeof error) == 0);
    if (!run(x, 15, 0xa0)) check_solid(x, at(0x3a0000));
    scenarios++;
    rt_executor_reset(x);

    /* sRGB encoding must occur once, and channel order must still be BGRA on readback. */
    size = record_draw(17, at(0x3c0000), 16, 16, 0, scale, translate, COLORS);
    uint32_t srgb_target[RT_COLOR_TARGET_WORDS] = {0};
    srgb_target[0] = (uint32_t)(at(0x3d0000) >> 8); srgb_target[1] = 1; srgb_target[2] = 3;
    srgb_target[4] = 0xa << 2 | 6 << 8 | 1 << 11; srgb_target[5] = 31;
    CHECK(rt_render_record_color_target(17, 0x70, 0, srgb_target, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(17, 0x80, 3, error, sizeof error) == 0);
    if (!run(x, 17, 0x90)) {
        uint32_t pixels[16 * 16];
        CHECK(rt_executor_read_target(x, at(0x3d0000), pixels, 16, 16, error, sizeof error) == 0);
        unsigned bad = 0;
        for (unsigned y = 0; y < 16; ++y) for (unsigned col = 0; col < 16; ++col) {
            uint32_t p = pixels[y * 16 + col];
            if (!near(p >> 16 & 255, srgb(((float)col + 0.5f) / 16)) ||
                !near(p >> 8 & 255, srgb(((float)y + 0.5f) / 16)) || !near(p & 255, srgb(0.25f)) || p >> 24 != 255) bad++;
        }
        CHECK(bad == 0);
    }
    scenarios++;
    rt_executor_reset(x);

    /* Calls at the submitted boundary do not run; accepted state carries into the next buffer. */
    size = record_draw(7, at(0x340000), 16, 16, 0, scale, translate, COLORS);
    if (!run(x, 7, 0x60)) {
        uint32_t guard = 0xdeadbeef;
        CHECK(rt_executor_read_target(x, at(0x340000), &guard, 1, 1, error, sizeof error) == -1);
        CHECK(guard == 0xdeadbeef);
    }
    if (!run(x, 7, size)) check_gradient(x, at(0x340000), 16, 16, 0);
    CHECK(rt_render_record_draw_auto(8, 0x10, 3, error, sizeof error) == 0);
    if (!run(x, 8, 0x20)) check_gradient(x, at(0x340000), 16, 16, 0);
    rt_executor_reset(x);
    expect_error(x, 8, 0x20, "no color target");
    check_gradient(x, at(0x340000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    /* Replacing a buffer drops its old calls; an explicit target disable prevents drawing. */
    CHECK(rt_render_record_color_target(7, 0, 0, NULL, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(7, 0x10, 3, error, sizeof error) == 0);
    expect_error(x, 7, 0x20, "no color target");
    scenarios++;
    rt_executor_reset(x);

    /* A small viewport leaves clear pixels outside it, including on a nonsquare surface. */
    const float sub_scale[3] = {8, 4, 1}, sub_translate[3] = {16, 8, 0};
    size = record_draw(9, at(0x350000), 32, 16, 0, sub_scale, sub_translate, COLORS);
    if (!run(x, 9, size)) {
        uint32_t pixels[32 * 16];
        CHECK(rt_executor_read_target(x, at(0x350000), pixels, 32, 16, error, sizeof error) == 0);
        unsigned bad = 0;
        for (unsigned y = 0; y < 16; ++y) for (unsigned col = 0; col < 32; ++col) {
            uint32_t p = pixels[y * 32 + col];
            if (col < 8 || col >= 24 || y < 4 || y >= 12) { if (p) bad++; }
            else if (!near(p >> 16 & 255, ((float)col - 7.5f) / 16) ||
                     !near(p >> 8 & 255, ((float)y - 3.5f) / 8) || !near(p & 255, 0.25f) || p >> 24 != 255) bad++;
        }
        CHECK(bad == 0);
    }
    scenarios++;
    rt_executor_reset(x);

    /* Later draws load existing pixels and apply state only from their recorded position. */
    size = record_draw(18, at(0x3e0000), 32, 16, 0, wide_scale, wide_translate, COLORS);
    CHECK(rt_render_record_viewport(18, 0x70, 0, 0, 1, sub_scale, sub_translate, error, sizeof error) == 0);
    descriptor(changed, at(SOLID_COLORS));
    CHECK(rt_render_record_user_data(18, 0x80, RT_STAGE_VERTEX, 4, changed, 4, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(18, 0x90, 3, error, sizeof error) == 0);
    if (!run(x, 18, 0xa0)) {
        uint32_t pixels[32 * 16];
        CHECK(rt_executor_read_target(x, at(0x3e0000), pixels, 32, 16, error, sizeof error) == 0);
        unsigned bad = 0;
        for (unsigned y = 0; y < 16; ++y) for (unsigned col = 0; col < 32; ++col) {
            uint32_t p = pixels[y * 32 + col];
            int inside = col >= 8 && col < 24 && y >= 4 && y < 12;
            float red = inside ? 0.75f : ((float)col + 0.5f) / 32;
            float green = inside ? 0.125f : ((float)y + 0.5f) / 16;
            if (!near(p >> 16 & 255, red) || !near(p >> 8 & 255, green) ||
                !near(p & 255, inside ? 0.5f : 0.25f) || p >> 24 != 255) bad++;
        }
        CHECK(bad == 0);
    }
    scenarios++;
    rt_executor_reset(x);

    uint32_t cropped[8 * 3];
    CHECK(rt_executor_read_target(x, at(0x100000), cropped, 8, 3, error, sizeof error) == 0);
    unsigned crop_bad = 0;
    for (unsigned y = 0; y < 3; ++y) for (unsigned col = 0; col < 8; ++col) {
        uint32_t p = cropped[y * 8 + col];
        if (!near(p >> 16 & 255, ((float)col + 0.5f) / 16) ||
            !near(p >> 8 & 255, ((float)y + 0.5f) / 16) || !near(p & 255, 0.25f) || p >> 24 != 255) crop_bad++;
    }
    CHECK(crop_bad == 0);
    scenarios++;
    rt_executor_reset(x);

    /* Generic register and instance events validate the supported opaque-triangle profile. */
    size = record_setup(19, at(0x3f0000), 16, 16, 0, scale, translate, COLORS);
    const uint32_t registers[][2] = {{0xa1e0, 0}, {0xa200, 0}, {0xa202, 0xcc0010},
        {0xa08e, 15}, {0xc242, 4}, {0xa10b, 0xffffffff}};
    for (unsigned i = 0; i < sizeof registers / sizeof *registers; ++i) {
        CHECK(rt_render_record_register(19, size, registers[i][0], registers[i][1], error, sizeof error) == 0);
        size += 0x10;
    }
    CHECK(rt_render_record_instances(19, size, 1, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(19, size, 3, error, sizeof error) == 0); size += 0x10;
    if (!run(x, 19, size)) check_gradient(x, at(0x3f0000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    size = record_setup(20, at(0x400000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_register(20, size, 0xa08e, 0, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(20, size, 3, error, sizeof error) == 0); size += 0x10;
    const uint32_t masked_size = size;
    CHECK(rt_render_record_register(20, size, 0xa08e, 15, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(20, size, 3, error, sizeof error) == 0); size += 0x10;
    if (!run(x, 20, masked_size)) {
        check_clipped_gradient(x, at(0x400000), 0, 0, 0, 0);
    }
    if (!run(x, 20, size)) check_gradient(x, at(0x400000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    /* Disabling colour writes cannot suppress observable vertex buffer stores. */
    size = record_draw(40, at(0x600000), 16, 16, 0, scale, translate, COLORS);
    const uint32_t initial_color_size = size;
    uint64_t store_code = at(STORE_VERTEX_CODE);
    const uint32_t store_vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(store_code >> 8), (uint32_t)(store_code >> 40),
        0x43, 12 << 1, 0, 4, 0};
    CHECK(rt_render_record_vertex_shader(40, size, store_vertex, error, sizeof error) == 0); size += 0x10;
    descriptor(changed, at(VERTEX_WRITES));
    CHECK(rt_render_record_user_data(40, size, RT_STAGE_VERTEX, 8, changed, 4, error, sizeof error) == 0); size += 0x10;
    descriptor(changed, at(SOLID_COLORS));
    CHECK(rt_render_record_user_data(40, size, RT_STAGE_VERTEX, 4, changed, 4, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_register(40, size, 0xa08e, 0, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(40, size, 3, error, sizeof error) == 0); size += 0x10;
    const uint32_t masked_color_size = size;
    CHECK(rt_render_record_register(40, size, 0xa08e, 15, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(40, size, 3, error, sizeof error) == 0); size += 0x10;
    uint32_t *witness = (uint32_t *)(pool + VERTEX_WRITES);
    if (!run(x, 40, initial_color_size)) check_gradient(x, at(0x600000), 16, 16, 0);
    memset(witness, 0, 4 * sizeof *witness);
    if (!run(x, 40, masked_color_size)) {
        CHECK(witness[0] == 0x13572468 && witness[1] == 0x13572468 && witness[2] == 0x13572468 && witness[3] == 0);
        check_gradient(x, at(0x600000), 16, 16, 0);
    }
    if (!run(x, 40, size)) check_solid(x, at(0x600000));
    scenarios++;
    rt_executor_reset(x);

    /* Raw index state is retained, but an Auto draw does not read an index buffer. */
    size = record_setup(41, at(0x610000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_index_type(41, size, 0xffffffff, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_index_base(41, size, UINT64_MAX, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_index_size(41, size, 0, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(41, size, 3, error, sizeof error) == 0); size += 0x10;
    if (!run(x, 41, size)) check_gradient(x, at(0x610000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    const float invalid_scales[][3] = {{INFINITY, 8, 1}, {FLT_MAX, 8, 1}};
    const char *invalid_reasons[] = {"non-finite", "coordinate range"};
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t buffer = 42 + i;
        size = record_setup(buffer, at(0x620000), 16, 16, 0, invalid_scales[i], translate, COLORS);
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, invalid_reasons[i]);
        scenarios++;
        rt_executor_reset(x);
    }

    /* The translator initializes only the vertex-ID input; none of the extra hardware profiles is modeled. */
    for (unsigned profile = 1; profile <= 3; ++profile) {
        uint64_t buffer = 43 + profile, code = at(VERTEX_CODE);
        size = record_setup(buffer, at(0x640000), 16, 16, 0, scale, translate, COLORS);
        const uint32_t vertex_profile[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40),
            2 | profile << 24, 8 << 1, 0, 4, 0};
        CHECK(rt_render_record_vertex_shader(buffer, size, vertex_profile, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, "vertex input VGPR profile");
        scenarios++;
        rt_executor_reset(x);
    }

    /* Qualify the common pos1 two-plane clip profile, including each independent plane. */
    for (unsigned mask = 1; mask <= 3; ++mask) {
        uint64_t buffer = 100 + mask, target = at(0x900000 + (mask - 1) * 0x10000);
        uint32_t vertex[RT_VERTEX_SHADER_WORDS]; vertex_registers(vertex, CLIP_VERTEX_CODE);
        vertex[5] = 0x44; vertex[6] = 0x400000 | mask;
        size = record_setup(buffer, target, 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (!run(x, buffer, size)) check_clipped_gradient(x, target, mask & 1 ? 8 : 0, mask & 2 ? 8 : 0, 16, 16);
        scenarios++;
        rt_executor_reset(x);
    }
    /* Every clip-distance slot contributes separately, including sparse masks and both export vectors. */
    for (unsigned i = 0; i < 9; ++i) {
        unsigned mask = i < 8 ? 1u << i : 255;
        uint64_t buffer = 104 + i, target = at(0x930000 + i * 0x10000);
        uint32_t vertex[RT_VERTEX_SHADER_WORDS]; vertex_registers(vertex, CLIP_EIGHT_CODE);
        vertex[5] = 0x444; vertex[6] = 0xc00000 | mask;
        size = record_setup(buffer, target, 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (!run(x, buffer, size)) check_clipped_gradient(x, target, mask & 0x55 ? 8 : 0, mask & 0xaa ? 8 : 0, 16, 16);
        scenarios++;
        rt_executor_reset(x);
    }

    uint32_t two_vertex[RT_VERTEX_SHADER_WORDS], two_pixel[RT_PIXEL_SHADER_WORDS];
    vertex_registers(two_vertex, TWO_VERTEX_CODE); pixel_registers(two_pixel, TWO_PIXEL_CODE);
    two_vertex[4] = 2; two_pixel[8] = 2;
    for (unsigned capacity = 2; capacity <= 4; capacity += 2) {
        uint64_t buffer = 120 + capacity;
        two_vertex[4] = (capacity - 1) << 1; two_pixel[8] = capacity;
        /* Larger register allocation reserves unused slots; it does not invent extra input values. */
        two_vertex[2] = two_pixel[2] = 2 | 3 << 6;
        size = record_setup(buffer, at(0x9c0000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_vertex_shader(buffer, size, two_vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_pixel_shader(buffer, size, two_pixel, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (!run(x, buffer, size)) check_two_parameters(x, at(0x9c0000));
        scenarios++;
        rt_executor_reset(x);
    }
    /* Cached shader interfaces still enforce smaller declared parameter/interpolant allocations. */
    for (unsigned stage = 0; stage < 2; ++stage) {
        uint64_t buffer = 125 + stage;
        two_vertex[4] = stage == 0 ? 0 : 2; two_pixel[8] = stage == 1 ? 1 : 2;
        size = record_setup(buffer, at(0x9c0000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_vertex_shader(buffer, size, two_vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_pixel_shader(buffer, size, two_pixel, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, stage == 0 ? "vertex parameter allocation" : "pixel interpolant allocation");
        scenarios++;
        rt_executor_reset(x);
    }

    uint32_t float_pixel[RT_PIXEL_SHADER_WORDS]; pixel_registers(float_pixel, FLOAT_PIXEL_CODE);
    float_pixel[3] = 4 << 1; float_pixel[5] = 9; float_pixel[8] = 0;
    size = record_setup(127, at(0x9d0000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_pixel_shader(127, size, float_pixel, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_user_data(127, size, RT_STAGE_PIXEL, 0, pixel_words, 4, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(127, size, 3, error, sizeof error) == 0); size += 0x10;
    if (!run(x, 127, size)) check_solid(x, at(0x9d0000));
    scenarios++;
    rt_executor_reset(x);

    /* Clip bounds remain byte-for-byte in the record; only numeric zero (either sign) and one are modeled. */
    const struct { uint32_t zmin, zmax; int supported; } clip_bounds[] = {
        {0, 0x3f800000, 1}, {0x80000000, 0x3f800000, 1},
        {0x3e800000, 0x3f800000, 0}, {0, 0x3f000000, 0},
        {1, 0x3f800000, 0}, {0x80000001, 0x3f800000, 0},
        {0x7fc12345, 0x3f800000, 0}, {0, 0x7fc23456, 0},
        {0xff800000, 0x3f800000, 0}, {0, 0x7f800000, 0},
    };
    for (unsigned i = 0; i < sizeof clip_bounds / sizeof *clip_bounds; ++i) {
        uint64_t buffer = 130 + i, target = at(0xa00000 + i * 0x10000);
        float zmin, zmax; memcpy(&zmin, &clip_bounds[i].zmin, sizeof zmin); memcpy(&zmax, &clip_bounds[i].zmax, sizeof zmax);
        size = record_setup(buffer, target, 16, 16, 0, scale, translate, COLORS);
        uint32_t viewport_offset = size;
        CHECK(rt_render_record_viewport(buffer, size, 0, zmin, zmax, scale, translate, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        size_t count = 0; const rt_render_call *calls = rt_render_submitted(buffer, size, &count);
        const rt_render_call *viewport = NULL;
        for (size_t n = 0; n < count; ++n) if (calls[n].kind == RT_CALL_VIEWPORT && calls[n].offset == viewport_offset) viewport = &calls[n];
        CHECK(viewport != NULL);
        if (viewport) {
            uint32_t low, high; memcpy(&low, &viewport->u.viewport.zmin, sizeof low); memcpy(&high, &viewport->u.viewport.zmax, sizeof high);
            CHECK(low == clip_bounds[i].zmin && high == clip_bounds[i].zmax);
        }
        if (clip_bounds[i].supported) { if (!run(x, buffer, size)) check_gradient(x, target, 16, 16, 0); }
        else expect_error(x, buffer, size, "standard 0,1 clip bounds");
        if (viewport) {
            uint32_t low, high; memcpy(&low, &viewport->u.viewport.zmin, sizeof low); memcpy(&high, &viewport->u.viewport.zmax, sizeof high);
            CHECK(low == clip_bounds[i].zmin && high == clip_bounds[i].zmax);
        }
        scenarios++;
        rt_executor_reset(x);
    }

    /* Change one behavior-bearing register field at a time; unused allocation capacity remains valid. */
    const struct { int pixel; unsigned word; uint32_t value; const char *reason; } shader_fields[] = {
        {0, 5, 0, "SPI_SHADER_POS_FORMAT"}, {0, 5, 5, "SPI_SHADER_POS_FORMAT"}, {0, 5, 0x44, "SPI_SHADER_POS_FORMAT"},
        {0, 6, 1 << 21, "misc or cull"}, {0, 6, 1 << 8, "misc or cull"}, {0, 6, 1, "without their export vector"},
        {0, 6, 1 << 4, "without their export vector"},
        {1, 4, 1, "pixel depth exports"},
        {1, 5, 0, "SPI_SHADER_COL_FORMAT"}, {1, 5, 3, "SPI_SHADER_COL_FORMAT"}, {1, 5, 0x44, "SPI_SHADER_COL_FORMAT"},
        {1, 6, 6, "pixel-shader inputs"}, {1, 7, 0, "pixel-shader inputs"},
        {1, 8, 0, "pixel interpolant allocation"}, {1, 8, 33, "parameter range"},
        {1, 9, 1, "SPI_BARYC_CNTL"}, {1, 9, 0x80000000, "SPI_BARYC_CNTL"},
        {1, 10, 1, "DB_SHADER_CONTROL"}, {1, 10, 0x80000000, "DB_SHADER_CONTROL"},
        {1, 11, 0, "CB_SHADER_MASK"}, {1, 11, 7, "CB_SHADER_MASK"}, {1, 11, 0x1f, "CB_SHADER_MASK"},
    };
    for (unsigned i = 0; i < sizeof shader_fields / sizeof *shader_fields; ++i) {
        uint64_t buffer = 150 + i;
        uint32_t vertex[RT_VERTEX_SHADER_WORDS], pixel[RT_PIXEL_SHADER_WORDS];
        vertex_registers(vertex, VERTEX_CODE); pixel_registers(pixel, PIXEL_CODE);
        if (shader_fields[i].pixel) pixel[shader_fields[i].word] = shader_fields[i].value;
        else vertex[shader_fields[i].word] = shader_fields[i].value;
        size = record_setup(buffer, at(0x9c0000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_pixel_shader(buffer, size, pixel, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, shader_fields[i].reason);
        scenarios++;
        rt_executor_reset(x);
    }
    /* A supported clip layout still needs its matching position-export format. */
    uint32_t mismatched_clip[RT_VERTEX_SHADER_WORDS]; vertex_registers(mismatched_clip, CLIP_VERTEX_CODE);
    mismatched_clip[6] = 0x400003;
    size = record_setup(180, at(0x9c0000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_vertex_shader(180, size, mismatched_clip, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(180, size, 3, error, sizeof error) == 0); size += 0x10;
    expect_error(x, 180, size, "SPI_SHADER_POS_FORMAT"); scenarios++;
    rt_executor_reset(x);

    /* User data must fit its declared SGPR allocation and the translator's sixteen-word model. */
    for (unsigned stage = 0; stage < 2; ++stage) for (unsigned modeled_range = 0; modeled_range < 2; ++modeled_range) {
        uint64_t buffer = 181 + stage * 2 + modeled_range;
        uint32_t vertex[RT_VERTEX_SHADER_WORDS], pixel[RT_PIXEL_SHADER_WORDS];
        vertex_registers(vertex, STORE_VERTEX_CODE); pixel_registers(pixel, USER_PIXEL_CODE);
        vertex[2] = 0x43; vertex[3] = 12 << 1;
        pixel[3] = 12 << 1; pixel[2] = 0x42; pixel[8] = 0;
        if (stage == 0) { vertex[2] = modeled_range ? 0xc3 : 3; vertex[3] = (modeled_range ? 17 : 12) << 1; }
        else { pixel[2] = modeled_range ? 0xc2 : 2; pixel[3] = (modeled_range ? 17 : 12) << 1; }
        size = record_setup(buffer, at(0x9c0000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_pixel_shader(buffer, size, pixel, error, sizeof error) == 0); size += 0x10;
        descriptor(changed, at(VERTEX_WRITES));
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, 8, changed, 4, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_PIXEL, 0, pixel_words, 4, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, modeled_range ? "modeled user-data range" : "declared allocation"); scenarios++;
        rt_executor_reset(x);
    }

    /* Both widths and address forms select vertices 3..5 from the shared pool, not Auto 0..2. */
    for (unsigned type = 0; type < 2; ++type) for (unsigned offset_form = 0; offset_form < 2; ++offset_form) {
        uint64_t buffer = 50 + type * 2 + offset_form, target = at(0x700000 + (type * 2 + offset_form) * 0x10000);
        uint64_t address = at(type ? INDICES32 : INDICES16);
        unsigned width = type ? 4 : 2;
        size = record_index_setup(buffer, target, type);
        if (offset_form) {
            CHECK(rt_render_record_index_base(buffer, size, address, error, sizeof error) == 0); size += 0x10;
            CHECK(rt_render_record_index_size(buffer, size, 1, error, sizeof error) == 0); size += 0x10;
            /* MAX_SIZE is the effective fetch count, not offset + count or the older size state. */
            CHECK(rt_render_record_draw_index_offset_packet(buffer, size, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0);
        } else CHECK(rt_render_record_draw_indexed(buffer, size, draw_indexed_header, 3, address + width, 3, 0, error, sizeof error) == 0);
        size += 0x10;
        if (!run(x, buffer, size)) check_gradient(x, target, 16, 16, 0);
        scenarios++;
        rt_executor_reset(x);
    }

    /* Canonical logical draws carry first/count only, with an optional retained buffer extent. */
    rt_executor *legacy_executor = NULL;
    CHECK(rt_executor_open(&memory, &legacy_executor, error, sizeof error) == 0);
    for (unsigned type = 0; type < 2; ++type) for (unsigned bounded = 0; bounded < 2; ++bounded) {
        uint64_t setup = 1500 + type * 4 + bounded * 2, buffer = setup + 1;
        uint64_t target = at(0xe00000 + (type * 2 + bounded) * 0x10000);
        size = record_index_setup(setup, target, type);
        CHECK(rt_render_record_index_base(setup, size, at(type ? INDICES32 : INDICES16), error, sizeof error) == 0); size += 0x10;
        if (bounded) { CHECK(rt_render_record_index_size(setup, size, 4, error, sizeof error) == 0); size += 0x10; }
        run(legacy_executor, setup, size);
        CHECK(rt_render_record_draw_index_offset(buffer, 0x10, 1, 3, error, sizeof error) == 0);
        if (!run(legacy_executor, buffer, 0x20)) check_gradient(legacy_executor, target, 16, 16, 0);
        scenarios++;
        rt_executor_reset(legacy_executor);
    }
    const struct { uint32_t extent, first; } legacy_ranges[] = {{0, 1}, {3, 1}, {UINT32_MAX, UINT32_MAX}};
    for (unsigned i = 0; i < sizeof legacy_ranges / sizeof legacy_ranges[0]; ++i) {
        uint64_t buffer = 1510 + i;
        size = record_index_setup(buffer, at(0x700000), 1);
        CHECK(rt_render_record_index_base(buffer, size, at(INDICES32), error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_index_size(buffer, size, legacy_ranges[i].extent, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_index_offset(buffer, size, legacy_ranges[i].first, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(legacy_executor, buffer, size, "legacy indexed draw exceeds INDEX_BUFFER_SIZE");
        scenarios++;
        rt_executor_reset(legacy_executor);
    }
    rt_executor_close(legacy_executor);

    /* A zero-count packet still submits a pass, and never fetches its unusable address/offset. */
    for (unsigned offset_form = 0; offset_form < 2; ++offset_form) {
        uint64_t buffer = 54 + offset_form, target = at(0x740000 + offset_form * 0x10000);
        size = record_index_setup(buffer, target, 0);
        if (offset_form)
            CHECK(rt_render_record_draw_index_offset_packet(buffer, size, draw_indexed_offset_header, 0, UINT32_MAX, 0, 0, error, sizeof error) == 0);
        else CHECK(rt_render_record_draw_indexed(buffer, size, draw_indexed_header, 0, UINT64_MAX, 0, 0, error, sizeof error) == 0);
        size += 0x10;
        if (!run(x, buffer, size)) check_clipped_gradient(x, target, 0, 0, 0, 0);
        scenarios++;
        rt_executor_reset(x);
    }

    /* The last fetched element may end exactly at the pool boundary, for either width. */
    for (unsigned type = 0; type < 2; ++type) {
        unsigned width = type ? 4 : 2;
        uint64_t buffer = 56 + type, target = at(0x760000 + type * 0x10000);
        unsigned relative = POOL_SIZE - 3 * width;
        if (type) memcpy(pool + relative, indices32 + 1, 3 * width);
        else memcpy(pool + relative, indices16 + 1, 3 * width);
        size = record_index_setup(buffer, target, type);
        CHECK(rt_render_record_draw_indexed(buffer, size, draw_indexed_header, 3, at(relative), 3, 0, error, sizeof error) == 0);
        size += 0x10;
        if (!run(x, buffer, size)) check_gradient(x, target, 16, 16, 0);
        scenarios++;
        rt_executor_reset(x);
    }

    /* Index reads use the current shared bytes, including when the same record is submitted again. */
    size = record_index_setup(58, at(0x780000), 0);
    CHECK(rt_render_record_draw_indexed(58, size, draw_indexed_header, 3, at(INDICES16) + 2, 3, 0, error, sizeof error) == 0);
    size += 0x10;
    ((uint16_t *)(pool + INDICES16))[2] = 3;
    if (!run(x, 58, size)) check_clipped_gradient(x, at(0x780000), 0, 0, 0, 0);
    ((uint16_t *)(pool + INDICES16))[2] = 4;
    if (!run(x, 58, size)) check_gradient(x, at(0x780000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    /* A direct packet's effect on the CP's saved base is unresolved: require a new explicit base. */
    size = record_index_setup(59, at(0x790000), 0);
    CHECK(rt_render_record_index_base(59, size, at(INDICES16), error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_indexed(59, size, draw_indexed_header, 3, at(INDICES16) + 2, 3, 0, error, sizeof error) == 0);
    size += 0x10;
    CHECK(rt_render_record_draw_index_offset_packet(59, size, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0);
    size += 0x10;
    expect_error(x, 59, size, "fresh INDEX_BASE");
    check_gradient(x, at(0x790000), 16, 16, 0);
    size = record_index_setup(59, at(0x790000), 0);
    CHECK(rt_render_record_draw_indexed(59, size, draw_indexed_header, 3, at(INDICES16) + 2, 3, 0, error, sizeof error) == 0);
    size += 0x10;
    CHECK(rt_render_record_index_base(59, size, at(INDICES16), error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_index_offset_packet(59, size, draw_indexed_offset_header, 3, 1, 3, 0, error, sizeof error) == 0);
    size += 0x10;
    if (!run(x, 59, size)) check_gradient(x, at(0x790000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    const struct {
        unsigned type; uint32_t header, maximum, count, initiator; uint64_t address; const char *reason;
    } invalid_indices[] = {
        {0, draw_indexed_header | 1, 3, 3, 0, at(INDICES16) + 2, "predicated"},
        {0, draw_indexed_header | 2, 3, 3, 0, at(INDICES16) + 2, "packet header"},
        {0, draw_indexed_offset_header, 3, 3, 0, at(INDICES16) + 2, "packet header"},
        {2, draw_indexed_header, 3, 3, 0, at(INDICES16) + 2, "index type"},
        {4, draw_indexed_header, 3, 3, 0, at(INDICES16) + 2, "byte swapping"},
        {0, draw_indexed_header, 3, 3, 2, at(INDICES16) + 2, "draw initiator"},
        {0, draw_indexed_header, 2, 3, 0, at(INDICES16) + 2, "DMA zero filling"},
        {0, draw_indexed_header, 3, 3, 0, at(POOL_SIZE) - 4, "outside the GPU-visible pool"},
        {0, draw_indexed_header, 3, 3, 0, guest_base - 2, "outside the GPU-visible pool"},
        {0, draw_indexed_header, 3, 3, 0, UINT64_MAX - 1, "outside the GPU-visible pool"},
        {0, draw_indexed_header, 3, 3, 0, (1ull << 56) | (at(INDICES16) + 2), "outside the GPU-visible pool"},
        {0, draw_indexed_header, 3, 3, 0, at(INDICES16) + 1, "not aligned"},
        {1, draw_indexed_header, 3, 3, 0, at(INDICES32) + 2, "not aligned"},
        {1, draw_indexed_header, UINT32_MAX, UINT32_MAX, 0, at(INDICES32), "outside the GPU-visible pool"},
    };
    for (unsigned i = 0; i < sizeof invalid_indices / sizeof *invalid_indices; ++i) {
        uint64_t buffer = 60 + i;
        size = record_index_setup(buffer, at(0x7a0000), invalid_indices[i].type);
        CHECK(rt_render_record_draw_indexed(buffer, size, invalid_indices[i].header, invalid_indices[i].maximum,
            invalid_indices[i].address, invalid_indices[i].count, invalid_indices[i].initiator, error, sizeof error) == 0);
        size += 0x10;
        expect_error(x, buffer, size, invalid_indices[i].reason);
        scenarios++;
        rt_executor_reset(x);
    }

    const struct { uint64_t base; uint32_t offset, header; int set_base; const char *reason; } invalid_offsets[] = {
        {0, 1, draw_indexed_offset_header, 0, "requires INDEX_BASE"},
        {UINT64_MAX - 1, 1, draw_indexed_offset_header, 1, "overflows"},
        {at(INDICES32), UINT32_MAX, draw_indexed_offset_header, 1, "outside the GPU-visible pool"},
        {at(INDICES32), 1, 0, 1, "packet header"},
        {at(INDICES32), 1, draw_indexed_offset_header | 1, 1, "predicated"},
        {at(INDICES32), 1, draw_indexed_offset_header | 2, 1, "packet header"},
    };
    for (unsigned i = 0; i < sizeof invalid_offsets / sizeof *invalid_offsets; ++i) {
        uint64_t buffer = 80 + i;
        size = record_index_setup(buffer, at(0x7b0000), 1);
        if (invalid_offsets[i].set_base) {
            CHECK(rt_render_record_index_base(buffer, size, invalid_offsets[i].base, error, sizeof error) == 0); size += 0x10;
        }
        CHECK(rt_render_record_draw_index_offset_packet(buffer, size, invalid_offsets[i].header, 3,
            invalid_offsets[i].offset, 3, 0, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, invalid_offsets[i].reason);
        scenarios++;
        rt_executor_reset(x);
    }
    size = record_setup(85, at(0x7b0000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_draw_indexed(85, size, draw_indexed_header, 3, at(INDICES16) + 2, 3, 0, error, sizeof error) == 0);
    size += 0x10;
    expect_error(x, 85, size, "requires INDEX_TYPE");
    scenarios++;
    rt_executor_reset(x);

    /* Empty scissoring must not become a software no-op: observe the vertex write directly. */
    size = record_setup(90, at(0x7c0000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_vertex_shader(90, size, store_vertex, error, sizeof error) == 0); size += 0x10;
    descriptor(changed, at(VERTEX_WRITES));
    CHECK(rt_render_record_user_data(90, size, RT_STAGE_VERTEX, 8, changed, 4, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_register(90, size, 0xa00c, 0, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_register(90, size, 0xa00d, 0, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(90, size, 3, error, sizeof error) == 0); size += 0x10;
    memset(witness, 0, 4 * sizeof *witness);
    if (!run(x, 90, size)) {
        CHECK(witness[0] == 0x13572468 && witness[1] == 0x13572468 && witness[2] == 0x13572468 && witness[3] == 0);
        check_clipped_gradient(x, at(0x7c0000), 0, 0, 0, 0);
    }
    scenarios++;
    rt_executor_reset(x);

    const struct { uint32_t address, value; const char *reason; } unsupported_registers[] = {
        {0xa1e0, 0x40000000, "blending"}, {0xa200, 2, "depth or stencil"},
        {0xa202, 0, "colour mode"}, {0xa08e, 7, "write mask"},
        {0xc242, 5, "primitive type"}, {0xa000, 0, "does not interpret"},
        {0xc242, 6, "primitive type"}, {0xa010, 0, "does not interpret"},
    };
    for (unsigned i = 0; i < sizeof unsupported_registers / sizeof *unsupported_registers; ++i) {
        uint64_t buffer = 21 + i;
        size = record_setup(buffer, at(0x3f0000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_register(buffer, size, unsupported_registers[i].address,
            unsupported_registers[i].value, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, unsupported_registers[i].reason);
        scenarios++;
        rt_executor_reset(x);
    }
    size = record_setup(27, at(0x3f0000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_instances(27, size, 2, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(27, size, 3, error, sizeof error) == 0); size += 0x10;
    expect_error(x, 27, size, "only one");
    scenarios++;
    rt_executor_reset(x);

    /* Screen scissor fields are signed, clamp negative values, and exclude the bottom/right edge. */
    const struct { int16_t left, top, right, bottom; unsigned l, t, r, b; } scissors[] = {
        {3, 5, 11, 13, 3, 5, 11, 13}, {-7, -5, 8, 9, 0, 0, 8, 9},
        {0, 0, -1, 8, 0, 0, 0, 0}, {10, 10, 2, 2, 0, 0, 0, 0},
        {20, 20, 30, 30, 0, 0, 0, 0}, {0, 0, 16384, 16384, 0, 0, 16, 16},
        {-32768, -32768, 16, 16, 0, 0, 16, 16}, {0, 0, 0, 16, 0, 0, 0, 0},
    };
    for (unsigned i = 0; i < sizeof scissors / sizeof *scissors; ++i) {
        uint64_t buffer = 30 + i, target = at(0x500000 + i * 0x10000);
        size = record_setup(buffer, target, 16, 16, 0, scale, translate, COLORS);
        uint32_t tl = (uint16_t)scissors[i].left | (uint32_t)(uint16_t)scissors[i].top << 16;
        uint32_t br = (uint16_t)scissors[i].right | (uint32_t)(uint16_t)scissors[i].bottom << 16;
        CHECK(rt_render_record_register(buffer, size, 0xa00c, tl, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_register(buffer, size, 0xa00d, br, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (!run(x, buffer, size)) check_clipped_gradient(x, target, scissors[i].l, scissors[i].t, scissors[i].r, scissors[i].b);
        scenarios++;
        rt_executor_reset(x);
    }
    size = record_setup(38, at(0x580000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_register(38, size, 0xa00c, 0, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(38, size, 3, error, sizeof error) == 0); size += 0x10;
    expect_error(x, 38, size, "both TL and BR");
    /* Restart before testing a different invalid state: an earlier failing draw stops submission. */
    size = record_setup(38, at(0x580000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_register(38, size, 0xa00c, 0, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_register(38, size, 0xa00d, 0x7fff7fff, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(38, size, 3, error, sizeof error) == 0); size += 0x10;
    expect_error(x, 38, size, "documented range");
    scenarios++;
    rt_executor_reset(x);

    /* Bounds failures precede readback and preserve the caller's destination. */
    uint32_t guard[4] = {0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef};
    CHECK(rt_executor_read_target(x, at(0x100000), guard, 17, 1, error, sizeof error) == -1);
    CHECK(rt_executor_read_target(x, at(0x390000), guard, 1, 1, error, sizeof error) == -1);
    CHECK(rt_executor_read_target(x, at(0x100000), guard, 0, 1, error, sizeof error) == -1);
    CHECK(rt_executor_read_target(x, at(0x100000), NULL, 1, 1, error, sizeof error) == -1);
    CHECK(guard[0] == 0xdeadbeef && guard[1] == 0xdeadbeef && guard[2] == 0xdeadbeef && guard[3] == 0xdeadbeef);
    scenarios++;
    rt_executor_reset(x);

    /* Unsupported state and code must fail with a reason, without poisoning valid later draws. */
    size = record_draw(10, at(0x100000), 32, 16, 0, wide_scale, wide_translate, COLORS);
    expect_error(x, 10, size, "changed size, format or tiling");
    size = record_draw(11, at(0x360000), 16, 16, 2, scale, translate, COLORS);
    expect_error(x, 11, size, "swap 2");
    size = record_draw(12, at(0x370000), 16, 16, 0, scale, translate, COLORS);
    uint32_t extra[RT_COLOR_TARGET_WORDS] = {0};
    extra[0] = (uint32_t)(at(0x380000) >> 8); extra[1] = 1; extra[2] = 3; extra[4] = 0xa << 2; extra[5] = 31;
    CHECK(rt_render_record_color_target(12, 0x70, 1, extra, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(12, 0x80, 3, error, sizeof error) == 0);
    expect_error(x, 12, 0x90, "beyond slot 0");
    /* Repair the retained extra slot before the next draw, without resetting other state. */
    size = record_setup(12, at(0x370000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_color_target(12, 0x70, 1, extra, error, sizeof error) == 0);
    CHECK(rt_render_record_color_target(12, 0x80, 1, NULL, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(12, 0x90, 3, error, sizeof error) == 0);
    if (!run(x, 12, 0xa0)) check_gradient(x, at(0x370000), 16, 16, 0);
    scenarios++;
    rt_executor_reset(x);

    size = record_draw(13, at(0x380000), 16, 16, 0, scale, translate, COLORS);
    const uint64_t outside = at(POOL_SIZE);
    const uint32_t bad_vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(outside >> 8), (uint32_t)(outside >> 40), 2, 8 << 1, 0, 4, 0};
    CHECK(rt_render_record_vertex_shader(13, 0x70, bad_vertex, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(13, 0x80, 3, error, sizeof error) == 0);
    expect_error(x, 13, 0x90, "outside the GPU-visible pool");
    uint64_t unsupported_code = at(UNSUPPORTED_CODE);
    const uint32_t unsupported_pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(unsupported_code >> 8),
        (uint32_t)(unsupported_code >> 40), 2, 0, 0, 4, 2, 2, 1, 0, 0, 0xf};
    size = record_draw(16, at(0x3b0000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_pixel_shader(16, 0x70, unsupported_pixel, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(16, 0x80, 3, error, sizeof error) == 0);
    expect_error(x, 16, 0x90, "pixel shader translation:");
    CHECK(rt_executor_run(NULL, 0, 0, error, sizeof error) == -1);
    scenarios++;
    rt_executor_reset(x);

    /* Live syscall bindings join the submitted buffer by guest address, in byte-offset order. */
    const uint64_t placed_buffer = 0xc0de0000, other_buffer = 0xc0df0000;
    const uint64_t vertex_code = at(VERTEX_CODE), pixel_code = at(PIXEL_CODE);
    const uint32_t placed_vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(vertex_code >> 8),
        (uint32_t)(vertex_code >> 40), 2, 8 << 1, 0, 4, 0};
    const uint32_t gradient_pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(pixel_code >> 8),
        (uint32_t)(pixel_code >> 40), 2, 0, 0, 4, 2, 2, 1, 0, 0, 0xf};
    CHECK(rt_render_record_vertex_shader_at(placed_buffer + 0x70, placed_vertex) == 0);
    CHECK(rt_render_record_pixel_shader_at(placed_buffer + 0x80, user_pixel) == 0);
    size = record_setup(other_buffer, at(0x810000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_vertex_shader(other_buffer, size, bad_vertex, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(other_buffer, size, 3, error, sizeof error) == 0); size += 0x10;
    expect_error(x, other_buffer, size, "outside the GPU-visible pool");
    size = record_setup(placed_buffer, at(0x820000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_vertex_shader(placed_buffer, 0x60, bad_vertex, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(placed_buffer, 0x90, RT_STAGE_PIXEL, 0, pixel_words, 4, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(placed_buffer, 0xa0, 3, error, sizeof error) == 0);
    if (!run(x, placed_buffer, 0xb0)) check_solid(x, at(0x820000));
    scenarios++;
    rt_executor_reset(x);

    /* New placed calls after submission begin a new use, rather than replaying the prior draw. */
    CHECK(rt_render_record_pixel_shader_at(placed_buffer + 0x80, gradient_pixel) == 0);
    if (!run(x, placed_buffer, 0xb0)) check_solid(x, at(0x820000));
    scenarios++;
    rt_executor_reset(x);

    /* Recorder reuse drops old setup calls; reset makes accidental replay observable. */
    const uint64_t reused_buffer = 0xc0e00000;
    size = record_draw(reused_buffer, at(0x830000), 16, 16, 0, scale, translate, COLORS);
    if (!run(x, reused_buffer, size)) check_gradient(x, at(0x830000), 16, 16, 0);
    rt_executor_reset(x);
    CHECK(rt_render_record_draw_auto(reused_buffer, 0x200, 3, error, sizeof error) == 0);
    expect_error(x, reused_buffer, 0x210, "no color target");
    scenarios++;
    rt_executor_reset(x);

    const uint64_t no_pixel_buffer = 0xc0f00000;
    const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS] = {0};
    size = record_setup(no_pixel_buffer, at(0x840000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_pixel_shader_at(no_pixel_buffer + 0x70, no_pixel_shader) == 0);
    CHECK(rt_render_record_draw_auto(no_pixel_buffer, 0x80, 3, error, sizeof error) == 0);
    expect_error(x, no_pixel_buffer, 0x90, "no pixel shader (a depth-only pass)");
    scenarios++;
    rt_executor_reset(x);

    /* Runtime descriptor formats use the real shared stop word in both shader stages. */
    const uint32_t whole_formats[] = {4, 5, 7}; /* uint, sint and float retain whole dwords */
    for (unsigned f = 0; f < sizeof whole_formats / sizeof whole_formats[0]; ++f) {
        const uint64_t buffer = 300 + f;
        size = record_setup(buffer, at(0x910000), 16, 16, 0, scale, translate, COLORS);
        uint32_t vertex[RT_VERTEX_SHADER_WORDS], user[8];
        vertex_registers(vertex, FORMAT_VERTEX_CODE);
        descriptor(user, at(CORNERS)); descriptor(user + 4, at(COLORS));
        for (unsigned d = 0; d < 2; ++d) {
            user[d * 4 + 1] |= 0xa500; /* descriptor flags above the proven 40-bit base */
            user[d * 4 + 3] = DST_XYZW | whole_formats[f] << 12 | 14u << 15;
        }
        CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (!run(x, buffer, size)) check_gradient(x, at(0x910000), 16, 16, 0);
        scenarios++;
        rt_executor_reset(x);
    }
    uint32_t format_vertex[RT_VERTEX_SHADER_WORDS], format_user[12];
    vertex_registers(format_vertex, FORMAT_STORE_CODE); format_vertex[2] = 0x43; format_vertex[3] = 12 << 1;
    descriptor(format_user, at(CORNERS)); descriptor(format_user + 4, at(COLORS));
    descriptor(format_user + 8, at(FORMAT_WRITES));
    uint32_t *format_writes = (uint32_t *)(pool + FORMAT_WRITES);
    /* Successful raw stores, unsupported formats, then packed stores through the same cached shader. */
    for (unsigned f = 0; f < 5; ++f) {
        const uint64_t buffer = 310 + f;
        size = record_setup(buffer, at(0x920000), 16, 16, 0, scale, translate, COLORS);
        format_user[11] = f == 1 ? DST_XYZW | 5u << 15 :
            f == 2 ? DST_XYZW | 10u << 15 : f == 3 ? DST_ZYXW | 10u << 15 : DST_XYZW | 7u << 12 | 14u << 15;
        memset(format_writes, 0xaa, 3 * 16);
        CHECK(rt_render_record_vertex_shader(buffer, size, format_vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, 0, format_user, 8, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, 8, format_user + 8, 4, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (f == 1) {
            expect_error(x, buffer, size, "shader runtime stop: 4-component buffer store, DATA_FORMAT 5");
            for (unsigned i = 0; i < 12; ++i) CHECK(format_writes[i] == 0xaaaaaaaa);
        } else if (!run(x, buffer, size)) {
            if (f == 0 || f == 4) CHECK(memcmp(format_writes, colors, sizeof colors) == 0);
            else for (unsigned v = 0; v < 3; ++v) {
                const uint32_t expected = f == 2 ? 0xff400000u | (v == 1 ? 255u : 0) | (v == 2 ? 255u << 8 : 0) :
                    0xff000040u | (v == 1 ? 255u << 16 : 0) | (v == 2 ? 255u << 8 : 0);
                CHECK(format_writes[v * 4] == expected);
                for (unsigned c = 1; c < 4; ++c) CHECK(format_writes[v * 4 + c] == 0xaaaaaaaa);
            }
            check_gradient(x, at(0x920000), 16, 16, 0);
        }
        scenarios++;
        rt_executor_reset(x);
    }
    /* A one-component store uses only its real selector; unused component selectors are irrelevant. */
    uint32_t store_x_vertex[RT_VERTEX_SHADER_WORDS]; vertex_registers(store_x_vertex, STORE_X_VERTEX_CODE);
    store_x_vertex[2] = 0x43; store_x_vertex[3] = 12 << 1;
    const uint32_t store_x_selectors[] = {4, DST_XYZW, 6};
    for (unsigned i = 0; i < sizeof store_x_selectors / sizeof store_x_selectors[0]; ++i) {
        uint64_t buffer = 400 + i; uint32_t destination[4]; descriptor(destination, at(FORMAT_WRITES));
        destination[3] = store_x_selectors[i] | 4u << 12 | 4u << 15;
        memset(format_writes, 0, 4 * sizeof *format_writes);
        size = record_setup(buffer, at(0x920000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_vertex_shader(buffer, size, store_x_vertex, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, 8, destination, 4, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (i == 2) {
            expect_error(x, buffer, size, "1-component buffer store");
            CHECK(format_writes[0] == 0 && format_writes[1] == 0 && format_writes[2] == 0 && format_writes[3] == 0);
        } else if (!run(x, buffer, size)) {
            CHECK(format_writes[0] == 0x13572468 && format_writes[1] == 0x13572468 &&
                format_writes[2] == 0x13572468 && format_writes[3] == 0);
            check_gradient(x, at(0x920000), 16, 16, 0);
        }
        scenarios++;
        rt_executor_reset(x);
    }

    /* Fragment-stage format swizzles, reserved-selector stops and cached recovery. */
    for (unsigned f = 0; f < 5; ++f) {
        const uint64_t buffer = 320 + f;
        size = record_setup(buffer, at(0x930000), 16, 16, 0, scale, translate, COLORS);
        uint32_t pixel[RT_PIXEL_SHADER_WORDS], user[4]; pixel_registers(pixel, FORMAT_PIXEL_CODE);
        pixel[3] = 4 << 1; pixel[5] = 9; pixel[8] = 0;
        descriptor(user, at(SOLID_COLORS));
        user[3] = f == 1 ? DST_XYZW | 5u << 15 :
            (f == 2 ? DST_ZYXW : f == 3 ? (DST_XYZW & ~7u) | 2 : DST_XYZW) | 7u << 12 | 14u << 15;
        CHECK(rt_render_record_pixel_shader(buffer, size, pixel, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_PIXEL, 0, user, 4, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (f == 1 || f == 3) expect_error(x, buffer, size, "shader runtime stop: 4-component buffer load");
        else if (!run(x, buffer, size)) {
            if (f != 2) check_solid(x, at(0x930000));
            else {
                uint32_t pixels[16 * 16];
                int read = rt_executor_read_target(x, at(0x930000), pixels, 16, 16, error, sizeof error);
                CHECK(read == 0);
                if (!read) {
                    unsigned bad = 0;
                    for (unsigned i = 0; i < 16 * 16; ++i)
                        if (!near(pixels[i] >> 16 & 255, 0.5f) || !near(pixels[i] >> 8 & 255, 0.125f) ||
                            !near(pixels[i] & 255, 0.75f) || pixels[i] >> 24 != 255) bad++;
                    CHECK(bad == 0);
                }
            }
        }
        scenarios++;
        rt_executor_reset(x);
    }

    /* The shared translator's packed UNORM8 table must compile and load/swizzle in both stages. */
    rt_executor *format_executor = NULL;
    CHECK(rt_executor_open(&memory, &format_executor, error, sizeof error) == 0);
    const uint32_t packed_colors[3] = {0xff8020bf, 0xff8020bf, 0xff8020bf};
    memcpy(pool + PACKED_COLORS, packed_colors, sizeof packed_colors);
    for (unsigned stage = 0; stage < 2; ++stage) for (unsigned swap = 0; swap < 2; ++swap) {
        const uint64_t buffer = 410 + stage * 2 + swap, target = at(0xf00000 + (stage * 2 + swap) * 0x10000);
        uint32_t user[8];
        size = record_setup(buffer, target, 16, 16, 0, scale, translate, COLORS);
        if (stage == 0) {
            uint32_t vertex[RT_VERTEX_SHADER_WORDS]; vertex_registers(vertex, FORMAT_VERTEX_CODE);
            descriptor(user, at(CORNERS)); user[3] = DST_XYZW | 7u << 12 | 14u << 15;
            descriptor(user + 4, at(PACKED_COLORS)); user[5] = (uint32_t)(at(PACKED_COLORS) >> 32) | 4u << 16;
            user[6] = 3; user[7] = (swap ? DST_ZYXW : DST_XYZW) | 10u << 15;
            CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0); size += 0x10;
            CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0); size += 0x10;
        } else {
            uint32_t pixel[RT_PIXEL_SHADER_WORDS]; pixel_registers(pixel, FORMAT_PIXEL_CODE);
            pixel[3] = 4 << 1; pixel[5] = 9; pixel[8] = 0;
            descriptor(user, at(PACKED_COLORS)); user[1] = (uint32_t)(at(PACKED_COLORS) >> 32) | 4u << 16;
            user[2] = 3; user[3] = (swap ? DST_ZYXW : DST_XYZW) | 10u << 15;
            CHECK(rt_render_record_pixel_shader(buffer, size, pixel, error, sizeof error) == 0); size += 0x10;
            CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_PIXEL, 0, user, 4, error, sizeof error) == 0); size += 0x10;
        }
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        if (!run(format_executor, buffer, size)) {
            uint32_t pixels[16 * 16];
            int read = rt_executor_read_target(format_executor, target, pixels, 16, 16, error, sizeof error);
            CHECK(read == 0);
            if (!read) {
                unsigned bad = 0;
                for (unsigned i = 0; i < 16 * 16; ++i) {
                    uint32_t value = pixels[i];
                    if (abs((int)(value >> 16 & 255) - (swap ? 128 : 191)) > 1 ||
                        abs((int)(value >> 8 & 255) - 32) > 1 ||
                        abs((int)(value & 255) - (swap ? 191 : 128)) > 1 || value >> 24 != 255) bad++;
                }
                CHECK(bad == 0);
            }
        }
        scenarios++;
        rt_executor_reset(format_executor);
    }
    rt_executor_close(format_executor);

    /* A one-component format at the last pool dword: selectors supply zero and float one. */
    memcpy(pool + POOL_SIZE - 4, solid[0], sizeof(float));
    size = record_setup(340, at(0x930000), 16, 16, 0, scale, translate, COLORS);
    uint32_t constant_pixel[RT_PIXEL_SHADER_WORDS], constant_user[4];
    pixel_registers(constant_pixel, FORMAT_PIXEL_CODE); constant_pixel[3] = 4 << 1; constant_pixel[5] = 9; constant_pixel[8] = 0;
    descriptor(constant_user, at(POOL_SIZE - 4)); constant_user[3] = 4 | 0 << 3 | 1 << 6 | 1 << 9 | 7u << 12 | 4u << 15;
    CHECK(rt_render_record_pixel_shader(340, size, constant_pixel, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_user_data(340, size, RT_STAGE_PIXEL, 0, constant_user, 4, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(340, size, 3, error, sizeof error) == 0); size += 0x10;
    if (!run(x, 340, size)) {
        uint32_t pixels[16 * 16]; int read = rt_executor_read_target(x, at(0x930000), pixels, 16, 16, error, sizeof error);
        CHECK(read == 0);
        if (!read) {
            unsigned bad = 0;
            for (unsigned i = 0; i < 16 * 16; ++i)
                if (!near(pixels[i] >> 16 & 255, 0.75f) || pixels[i] >> 8 & 255 || (pixels[i] & 255) != 255 || pixels[i] >> 24 != 255) bad++;
            CHECK(bad == 0);
        }
    }
    scenarios++;
    rt_executor_reset(x);

    /* Position x/y inputs are decoded upstream, but are outside this explicitly barycentric-only profile. */
    const uint32_t positions[] = {0x102, 0x202, 0x302};
    for (unsigned i = 0; i < sizeof positions / sizeof positions[0]; ++i) {
        uint64_t buffer = 350 + i; uint32_t pixel[RT_PIXEL_SHADER_WORDS]; pixel_registers(pixel, POSITION_PIXEL_CODE);
        pixel[5] = 9; pixel[8] = 0; pixel[6] = pixel[7] = positions[i];
        size = record_setup(buffer, at(0x930000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_pixel_shader(buffer, size, pixel, error, sizeof error) == 0); size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, "inputs beyond perspective-center barycentrics");
        scenarios++;
        rt_executor_reset(x);
    }
    /* A scalar VCC half write leaves the lane mask unknown in both draw stages. */
    for (unsigned stage = 0; stage < 2; ++stage) {
        uint64_t buffer = 360 + stage;
        size = record_setup(buffer, at(0x930000), 16, 16, 0, scale, translate, COLORS);
        if (stage == 0) {
            uint32_t vertex[RT_VERTEX_SHADER_WORDS]; vertex_registers(vertex, VCC_VERTEX_CODE);
            CHECK(rt_render_record_vertex_shader(buffer, size, vertex, error, sizeof error) == 0);
        } else {
            uint32_t pixel[RT_PIXEL_SHADER_WORDS]; pixel_registers(pixel, VCC_PIXEL_CODE); pixel[5] = 9; pixel[8] = 0;
            CHECK(rt_render_record_pixel_shader(buffer, size, pixel, error, sizeof error) == 0);
        }
        size += 0x10;
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, "VCC lane mask is unknown after a scalar half write");
        size = record_draw(buffer, at(0x930000), 16, 16, 0, scale, translate, COLORS);
        if (!run(x, buffer, size)) check_gradient(x, at(0x930000), 16, 16, 0);
        scenarios++;
        rt_executor_reset(x);
    }
    size = record_setup(370, at(0x930000), 16, 16, 0, scale, translate, COLORS);
    uint32_t quad_pixel[RT_PIXEL_SHADER_WORDS]; pixel_registers(quad_pixel, QUAD_PIXEL_CODE); quad_pixel[5] = 9; quad_pixel[8] = 0;
    CHECK(rt_render_record_pixel_shader(370, size, quad_pixel, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(370, size, 3, error, sizeof error) == 0); size += 0x10;
    expect_error(x, 370, size, "whole-quad shader operations are outside the Metal draw profile");
    scenarios++;
    rt_executor_reset(x);

    /* Shader accesses require actual resource descriptors before a native image can be bound. */
    const struct { unsigned code, user_sgprs; int from_memory; const char *reason; } missing_image_descriptors[] = {
        {IMAGE_PIXEL_CODE, 8, 0, "pixel descriptor reads unset user data"},
        {IMAGE_MIP_PIXEL_CODE, 8, 0, "pixel descriptor reads unset user data"},
        {SAMPLE_PIXEL_CODE, 12, 0, "pixel descriptor reads unset user data"},
        {MEMORY_IMAGE_PIXEL_CODE, 14, 1, "image DATA_FORMAT"},
    };
    for (unsigned i = 0; i < sizeof missing_image_descriptors / sizeof missing_image_descriptors[0]; ++i) {
        uint64_t buffer = 380 + i; uint32_t pixel[RT_PIXEL_SHADER_WORDS]; pixel_registers(pixel, missing_image_descriptors[i].code);
        pixel[2] = 0x42; pixel[3] = missing_image_descriptors[i].user_sgprs << 1; pixel[5] = 9; pixel[8] = 0;
        size = record_setup(buffer, at(0x930000), 16, 16, 0, scale, translate, COLORS);
        CHECK(rt_render_record_pixel_shader(buffer, size, pixel, error, sizeof error) == 0); size += 0x10;
        if (missing_image_descriptors[i].from_memory) {
            /* The owned pointer is valid; its zero descriptor must fail before any GPU texel access. */
            const uint64_t address = at(0x24000);
            const uint32_t pointer[2] = {(uint32_t)address, (uint32_t)(address >> 32)};
            CHECK(rt_render_record_user_data(buffer, size, RT_STAGE_PIXEL, 12, pointer, 2, error, sizeof error) == 0); size += 0x10;
        }
        CHECK(rt_render_record_draw_auto(buffer, size, 3, error, sizeof error) == 0); size += 0x10;
        expect_error(x, buffer, size, missing_image_descriptors[i].reason);
        scenarios++;
        rt_executor_reset(x);
    }

    check_fetch_shaders(x, pool);

    /* Compute binding is state only; a dispatch still requires that state. */
    const uint64_t compute_buffer = 0xc1000000;
    const uint64_t compute_code = at(COMPUTE_CODE);
    const uint32_t compute[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(compute_code >> 8),
        (uint32_t)(compute_code >> 40), 2, 8 << 1, 1, 1, 1};
    CHECK(rt_render_record_compute_shader_at(compute_buffer + 0x10, compute) == 0);
    CHECK(run(x, compute_buffer, 0x20) == 0);
    scenarios++;
    rt_executor_reset(x);
    const uint32_t groups[3] = {1, 1, 1};
    CHECK(rt_render_record_dispatch(330, 0x10, groups, error, sizeof error) == 0);
    expect_error(x, 330, 0x20, "a dispatch requires a compute shader");
    scenarios++;
    rt_executor_reset(x);
    const rt_shader_stage unsupported_stages[] = {RT_STAGE_GEOMETRY, RT_STAGE_EXPORT, RT_STAGE_HULL, RT_STAGE_LOCAL};
    const uint32_t stage_word = 0x12345678;
    for (unsigned i = 0; i < sizeof unsupported_stages / sizeof unsupported_stages[0]; ++i) {
        CHECK(rt_render_record_user_data(332 + i, 0x10, unsupported_stages[i], 0, &stage_word, 1, error, sizeof error) == 0);
        expect_error(x, 332 + i, 0x20, "only vertex and pixel user data");
        scenarios++;
        rt_executor_reset(x);
    }
    size = record_setup(331, at(0xc00000), 16, 16, 0, scale, translate, COLORS);
    CHECK(rt_render_record_register(331, size, 0xc242, 17, error, sizeof error) == 0); size += 0x10;
    CHECK(rt_render_record_draw_auto(331, size, 3, error, sizeof error) == 0); size += 0x10;
    expect_error(x, 331, size, "rectangle lists require a geometry expansion profile");
    uint32_t unattached = 0xdeadbeef;
    CHECK(rt_executor_read_target(x, at(0xc00000), &unattached, 1, 1, error, sizeof error) == -1);
    CHECK(unattached == 0xdeadbeef);
    check_solid(x, at(0x820000)); /* Neither rejected event nor primitive changed the completed target. */
    scenarios++;
    rt_executor_reset(x);

    check_persistent_state(x, pool);
    check_written_user_data(&memory, pool);

    /* Invalid opening contracts cannot create or overwrite an executor. */
    rt_executor *empty = NULL;
    rt_executor_memory invalid = memory;
    CHECK(rt_executor_open(NULL, &empty, error, sizeof error) == -1 && empty == NULL);
    CHECK(rt_executor_open(&memory, NULL, error, sizeof error) == -1);
    invalid.size--;
    CHECK(rt_executor_open(&invalid, &empty, error, sizeof error) == -1 && empty == NULL);
    invalid = memory; invalid.size *= 2;
    CHECK(rt_executor_open(&invalid, &empty, error, sizeof error) == -1 && empty == NULL);
    invalid = memory; invalid.guest_base = UINT64_MAX - POOL_SIZE / 2;
    CHECK(rt_executor_open(&invalid, &empty, error, sizeof error) == -1 && empty == NULL);
    scenarios++;
    rt_executor_reset(x);

    rt_executor_close(x);
    x = NULL;
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(rt_executor_open(&memory, &x, error, sizeof error) == 0);
        rt_executor_close(x); x = NULL;
    }
    CHECK(rt_executor_open(&memory, &x, error, sizeof error) == 0);
    /* The executor owns its second mapping even after the caller closes and unmaps its pool. */
    close(fd);
    munmap(pool, POOL_SIZE);
    size = record_draw(14, at(0x100000), 16, 16, 0, scale, translate, COLORS);
    if (!run(x, 14, size)) check_gradient(x, at(0x100000), 16, 16, 0);

    /* Queued calls survive the recorder being cleared. GPU steps can split the
       array between state setup and its draw without resetting that state. */
    CHECK(rt_executor_run_calls(x, NULL, 0, error, sizeof error) == 0);
    CHECK(rt_executor_run_calls(NULL, NULL, 0, error, sizeof error) == -1);
    scenarios++;
    for (unsigned split = 0; split < 2; ++split) {
        const uint64_t target = at(0xd60000 + split * 0x10000);
        rt_executor_reset(x);
        size = record_draw(420 + split, target, 16, 16, 0, scale, translate, COLORS);
        size_t count = 0;
        const rt_render_call *recorded = rt_render_submitted(420 + split, size, &count);
        CHECK(count > 1);
        rt_render_call *owned = malloc(count * sizeof *owned);
        CHECK(owned != NULL);
        if (owned && count > 1) {
            memcpy(owned, recorded, count * sizeof *owned);
            rt_render_forget();
            if (split) {
                CHECK(rt_executor_run_calls(x, owned, count - 1, error, sizeof error) == 0);
                CHECK(rt_executor_run_calls(x, owned + count - 1, 1, error, sizeof error) == 0);
            } else {
                CHECK(rt_executor_run_calls(x, owned, count, error, sizeof error) == 0);
            }
            check_gradient(x, target, 16, 16, 0);
        }
        free(owned);
        scenarios++;
    }
    rt_executor_close(x); x = NULL;
    rt_executor_close(NULL);
    scenarios++;
    rt_render_forget();
    printf("Metal executor: %u scenarios, %u checks, %u failures\n", scenarios, checks, failures);
    return failures != 0;
}
