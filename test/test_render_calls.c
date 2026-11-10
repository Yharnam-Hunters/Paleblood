/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The renderer's record of the GPU library's calls (runtime/renderer/render_calls.c): render-target
   registers decoded as AMD's field layout defines them, and each command buffer's calls taken in
   order up to the submitted size, a restarted buffer dropping its older calls. */
#include "runtime/render_calls.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { COLOR_8_8_8_8 = 0xa, NUMBER_SRGB = 6, SWAP_ALT = 1 };

/* A 1920 x 1080 COLOR_8_8_8_8 sRGB target at 0x12_3456_7800, tile mode 10, alternate swap, one
   sample: words worked out by hand from the field layout. */
static void target_1080p(uint32_t words[RT_COLOR_TARGET_WORDS])
{
    memset(words, 0, RT_COLOR_TARGET_WORDS * sizeof *words);
    words[0] = 0x12345678;                        /* BASE: address >> 8 */
    words[1] = 1920 / 8 - 1;                      /* PITCH.TILE_MAX */
    words[2] = 1920 * 1080 / 64 - 1;              /* SLICE.TILE_MAX */
    words[3] = 0;                                 /* VIEW: slice 0 only */
    words[4] = COLOR_8_8_8_8 << 2 | NUMBER_SRGB << 8 | SWAP_ALT << 11;
    words[5] = 10;                                /* ATTRIB.TILE_MODE_INDEX, NUM_SAMPLES 0 */
}

static void decoding(void)
{
    uint32_t words[RT_COLOR_TARGET_WORDS];
    rt_color_target t;
    char error[128] = "";
    target_1080p(words);
    CHECK(rt_color_target_decode(words, &t, error, sizeof error) == 1);
    CHECK(t.address == 0x1234567800ull && t.pitch == 1920 && t.height == 1080);
    CHECK(t.format == COLOR_8_8_8_8 && t.number_type == NUMBER_SRGB && t.component_swap == SWAP_ALT);
    CHECK(t.tile_mode_index == 10 && t.samples == 1 && !t.fast_clear && !t.compression);

    words[3] = 2 | 5 << 13;                       /* VIEW: slices 2 to 5 */
    words[4] |= 1 << 13 | 1 << 14;                /* FAST_CLEAR, COMPRESSION */
    words[5] = 3 | 2 << 12;                       /* tile mode 3, four samples */
    CHECK(rt_color_target_decode(words, &t, error, sizeof error) == 1);
    CHECK(t.first_slice == 2 && t.last_slice == 5 && t.fast_clear && t.compression);
    CHECK(t.tile_mode_index == 3 && t.samples == 4);

    words[4] = 0;                                 /* format COLOR_INVALID: the slot is off */
    CHECK(rt_color_target_decode(words, &t, error, sizeof error) == 0);

    target_1080p(words);
    words[4] = 0xd << 2;                          /* COLOR_RESERVED_13 */
    CHECK(rt_color_target_decode(words, &t, error, sizeof error) == -1 && strstr(error, "reserved"));
    words[4] = 0x17 << 2;                         /* past the last defined format */
    CHECK(rt_color_target_decode(words, &t, error, sizeof error) == -1);

    target_1080p(words);
    words[1] = 1000 / 8 - 1;                      /* 1920 * 1080 pixels are not whole 1000-pixel rows */
    CHECK(rt_color_target_decode(words, &t, error, sizeof error) == -1 && strstr(error, "rows"));
}

/* Register blocks worked out by hand from the field layout: code at 0x12_3456_7800 (MEM_BASE
   0x12345678 in LO, 0 in HI), 24 VGPRs, 16 SGPRs, VGPR_COMP_CNT 3, 4 user SGPRs, 3 parameter
   vectors, position 0 and 1 exported, clip distances 0 and 1 in pos1. */
static void shader_blocks(void)
{
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {
        0x12345678, 0x00000000,
        (24 / 4 - 1) | (16 / 8 - 1) << 6 | 3u << 24,  /* RSRC1 */
        4 << 1,                                       /* RSRC2: USER_SGPR 4 */
        (3 - 1) << 1,                                 /* SPI_VS_OUT_CONFIG */
        0x44,                                         /* SPI_SHADER_POS_FORMAT */
        0x400003,                                     /* PA_CL_VS_OUT_CNTL */
    };
    rt_vertex_shader v;
    rt_vertex_shader_decode(vertex, &v);
    CHECK(v.code == 0x1234567800ull && v.vgprs == 24 && v.sgprs == 16 && v.vgpr_inputs == 3);
    CHECK(v.user_sgprs == 4 && v.parameter_count == 3 && v.position_format == 0x44 && v.output_control == 0x400003);

    /* PGM_HI keeps only MEM_BASE's 8 bits: 0xffffffff (the shader files' placeholder) gives 0xff */
    const uint32_t high[RT_VERTEX_SHADER_WORDS] = {0x12345678, 0xffffffff};
    rt_vertex_shader_decode(high, &v);
    CHECK(v.code == 0xff1234567800ull);

    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {
        0x00abcdef, 0x01,
        (8 / 4 - 1) | (24 / 8 - 1) << 6,              /* RSRC1: 8 VGPRs, 24 SGPRs */
        2 << 1,                                       /* RSRC2: USER_SGPR 2 */
        0, 0x4,                                       /* Z_FORMAT, COL_FORMAT */
        0x1002, 0x1002,                               /* INPUT_ENA, INPUT_ADDR: perspective centre, front face */
        32 | 1 << 6,                                  /* IN_CONTROL: NUM_INTERP 32 (all six bits), PARAM_GEN */
        0, 0x10, 0xf,                                 /* BARYC_CNTL, DB_SHADER_CONTROL, CB_SHADER_MASK */
    };
    rt_pixel_shader p;
    rt_pixel_shader_decode(pixel, &p);
    CHECK(p.code == 0x100abcdef00ull && p.vgprs == 8 && p.sgprs == 24 && p.user_sgprs == 2);
    CHECK(p.color_formats == 4 && p.input_enable == 0x1002 && p.input_address == 0x1002 && p.interpolated == 32);
    CHECK(p.depth_control == 0x10 && p.color_mask == 0xf && p.depth_format == 0 && p.barycentric_control == 0);

    char error[128] = "";
    size_t count = 0;
    rt_render_forget();
    CHECK(rt_render_record_vertex_shader(0x400000000ull, 0x20, vertex, error, sizeof error) == 0);
    CHECK(rt_render_record_pixel_shader(0x400000000ull, 0x60, pixel, error, sizeof error) == 0);
    const rt_render_call *calls = rt_render_submitted(0x400000000ull, 0x100, &count);
    CHECK(count == 2);
    if (count == 2) {
        CHECK(calls[0].kind == RT_CALL_VERTEX_SHADER && calls[0].u.vertex_shader.output_control == 0x400003);
        CHECK(calls[1].kind == RT_CALL_PIXEL_SHADER && calls[1].u.pixel_shader.interpolated == 32);
    }
    rt_render_forget();
}

static void user_data(void)
{
    enum { BUFFER = 0x500000000ull };
    const uint32_t descriptor[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444}, one = 0x55;
    const uint32_t eight[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    char error[160] = "";
    size_t count = 0;
    rt_render_forget();
    CHECK(rt_render_record_user_data(BUFFER, 0x10, RT_STAGE_VERTEX, 0, descriptor, 4, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(BUFFER, 0x20, RT_STAGE_PIXEL, 15, &one, 1, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(BUFFER, 0x30, RT_STAGE_LOCAL, 8, eight, 8, error, sizeof error) == 0);
    const rt_render_call *calls = rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 3);
    if (count == 3) {
        CHECK(calls[0].kind == RT_CALL_USER_DATA && calls[0].u.user_data.stage == RT_STAGE_VERTEX && calls[0].u.user_data.slot == 0 &&
              calls[0].u.user_data.count == 4 && calls[0].u.user_data.words[3] == 0x44444444);
        CHECK(calls[1].u.user_data.stage == RT_STAGE_PIXEL && calls[1].u.user_data.slot == 15 && calls[1].u.user_data.words[0] == 0x55);
        CHECK(calls[2].u.user_data.count == 8 && calls[2].u.user_data.words[7] == 8);
    }
    /* refused: an unknown stage, no words, too many words, slots past the sixteenth */
    CHECK(rt_render_record_user_data(BUFFER, 0x40, RT_SHADER_STAGES, 0, &one, 1, error, sizeof error) == -1);
    CHECK(rt_render_record_user_data(BUFFER, 0x40, RT_STAGE_VERTEX, 0, &one, 0, error, sizeof error) == -1);
    CHECK(rt_render_record_user_data(BUFFER, 0x40, RT_STAGE_VERTEX, 0, eight, 9, error, sizeof error) == -1);
    CHECK(rt_render_record_user_data(BUFFER, 0x40, RT_STAGE_VERTEX, 9, eight, 8, error, sizeof error) == -1 && strstr(error, "slots 9-16"));
    /* Check the first slot before subtracting it: slot+count can wrap. */
    const struct { unsigned slot, count; } invalid_ranges[] = {
        {UINT32_MAX, 1}, {UINT32_MAX - 7, 8}, {UINT32_MAX - 6, 8}, {16, 1}, {15, 2},
    };
    for (size_t i = 0; i < sizeof invalid_ranges / sizeof *invalid_ranges; ++i)
        CHECK(rt_render_record_user_data(BUFFER, 0x40, RT_STAGE_VERTEX, invalid_ranges[i].slot,
                                         eight, invalid_ranges[i].count, error, sizeof error) == -1);
    rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 3);
    rt_render_forget();
}

static void recording(void)
{
    enum { BUFFER = 0x200000000ull, OTHER = 0x200100000ull };
    const float scale[3] = {960, -540, 0.5f}, translate[3] = {960, 540, 0.5f};
    uint32_t words[RT_COLOR_TARGET_WORDS];
    char error[128] = "";
    size_t count = 0;
    target_1080p(words);
    rt_render_forget();

    CHECK(rt_render_record_color_target(BUFFER, 0x10, 0, words, error, sizeof error) == 0);
    CHECK(rt_render_record_viewport(BUFFER, 0x40, 0, 0, 1, scale, translate, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(OTHER, 0x8, 6, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(BUFFER, 0x80, 3, error, sizeof error) == 0);

    const rt_render_call *calls = rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 3);
    if (count == 3) {
        CHECK(calls[0].kind == RT_CALL_COLOR_TARGET && calls[0].u.color_target.enabled &&
              calls[0].u.color_target.target.pitch == 1920);
        CHECK(calls[1].kind == RT_CALL_VIEWPORT && calls[1].u.viewport.scale[1] == -540 &&
              calls[1].u.viewport.translate[0] == 960 && calls[1].u.viewport.zmax == 1);
        CHECK(calls[2].kind == RT_CALL_DRAW_AUTO && calls[2].u.draw_auto.index_count == 3);
    }
    rt_render_submitted(BUFFER, 0x80, &count);    /* the draw at 0x80 is past a submission of 0x80 bytes */
    CHECK(count == 2);
    calls = rt_render_submitted(OTHER, 0x100, &count);
    CHECK(count == 1 && calls[0].u.draw_auto.index_count == 6);
    rt_render_submitted(0x300000000ull, 0x100, &count); /* a buffer with no calls */
    CHECK(count == 0);

    /* the buffer starts over: a call at a smaller offset drops the older ones */
    CHECK(rt_render_record_color_target(BUFFER, 0x10, 1, NULL, error, sizeof error) == 0);
    calls = rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 1 && calls[0].kind == RT_CALL_COLOR_TARGET && calls[0].u.color_target.slot == 1 &&
          !calls[0].u.color_target.enabled);

    /* refused calls record nothing */
    CHECK(rt_render_record_color_target(BUFFER, 0x20, RT_COLOR_TARGETS, words, error, sizeof error) == -1);
    CHECK(rt_render_record_viewport(BUFFER, 0x20, RT_VIEWPORTS, 0, 1, scale, translate, error, sizeof error) == -1);
    words[4] = 0xf << 2;
    CHECK(rt_render_record_color_target(BUFFER, 0x20, 0, words, error, sizeof error) == -1);
    rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 1);
    rt_render_forget();
    rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 0);
}

/* Registers the library sets (blend, depth, the primitive type, ...) and the instance count. */
static void registers_and_instances(void)
{
    enum { BUFFER = 0x200000000ull };
    char error[160];
    size_t count = 0;
    rt_render_forget();
    CHECK(rt_render_record_register(BUFFER, 0x10, 0xa1e0, 0x20010001u, error, sizeof error) == 0); /* CB_BLEND0_CONTROL */
    CHECK(rt_render_record_register(BUFFER, 0x20, 0xc242, 4, error, sizeof error) == 0);           /* VGT_PRIMITIVE_TYPE */
    CHECK(rt_render_record_instances(BUFFER, 0x30, 2, error, sizeof error) == 0);
    CHECK(rt_render_record_register(BUFFER, 0x40, 0x2c0c, 1, error, sizeof error) == -1 && strstr(error, "0x2c0c"));
    CHECK(rt_render_record_register(BUFFER, 0x40, 0x9fff, 1, error, sizeof error) == -1);
    CHECK(rt_render_record_register(BUFFER, 0x40, 0xa400, 1, error, sizeof error) == -1);
    CHECK(rt_render_record_instances(BUFFER, 0x40, 0, error, sizeof error) == -1);
    const rt_render_call *calls = rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 3);
    if (count == 3) {
        CHECK(calls[0].kind == RT_CALL_REGISTER && calls[0].u.reg.address == 0xa1e0 && calls[0].u.reg.value == 0x20010001u);
        CHECK(calls[1].kind == RT_CALL_REGISTER && calls[1].u.reg.address == 0xc242 && calls[1].u.reg.value == 4);
        CHECK(calls[2].kind == RT_CALL_INSTANCES && calls[2].u.instances.count == 2);
    }
    rt_render_forget();
}

/* Shaders the runtime's own GNM calls bind are placed by guest address and join, at submission, the
   buffer that holds them, in order among its other calls; a submitted buffer starts over at its next
   use. */
static void placed_calls(void)
{
    enum { BUFFER = 0x500000000ull };
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {0x11111111};
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {0x22222222};
    char error[128] = "";
    size_t count = 0;
    rt_render_forget();
    CHECK(rt_render_record_draw_auto(BUFFER, 0x80, 3, error, sizeof error) == 0);
    CHECK(rt_render_record_vertex_shader_at(BUFFER + 0x40, vertex) == 0);
    CHECK(rt_render_record_pixel_shader_at(BUFFER + 0x60, pixel) == 0);
    CHECK(rt_render_record_pixel_shader_at(BUFFER + 0x200, pixel) == 0);  /* past this submission */
    CHECK(rt_render_record_vertex_shader_at(0x600000000ull, vertex) == 0); /* another buffer's */
    const rt_render_call *calls = rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 3);
    if (count == 3) {
        CHECK(calls[0].kind == RT_CALL_VERTEX_SHADER && calls[0].offset == 0x40 && calls[0].u.vertex_shader.code == 0x1111111100ull);
        CHECK(calls[1].kind == RT_CALL_PIXEL_SHADER && calls[1].offset == 0x60 && calls[1].u.pixel_shader.code == 0x2222222200ull);
        CHECK(calls[2].kind == RT_CALL_DRAW_AUTO && calls[2].offset == 0x80);
    }
    rt_render_submitted(BUFFER, 0x100, &count);  /* submitted again: the same calls, none twice */
    CHECK(count == 3);
    calls = rt_render_submitted(0x600000000ull, 0x10, &count);
    CHECK(count == 1 && calls[0].kind == RT_CALL_VERTEX_SHADER && calls[0].offset == 0);

    /* the next use of BUFFER: a placed call alone starts it over, without the old draw */
    CHECK(rt_render_record_vertex_shader_at(BUFFER + 0x90, vertex) == 0);
    calls = rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 1 && calls[0].kind == RT_CALL_VERTEX_SHADER && calls[0].offset == 0x90);
    /* and a recorded call after a submission starts it over too, even at a larger offset */
    CHECK(rt_render_record_draw_auto(BUFFER, 0xa0, 6, error, sizeof error) == 0);
    calls = rt_render_submitted(BUFFER, 0x400, &count);  /* the call placed at 0x200 joins now */
    CHECK(count == 2 && calls[0].kind == RT_CALL_DRAW_AUTO && calls[1].kind == RT_CALL_PIXEL_SHADER &&
          calls[1].offset == 0x200);
    rt_render_forget();
    rt_render_submitted(0x600000000ull, 0x10, &count);
    CHECK(count == 0);
}

/* A compute shader's register block and a dispatch. */
static void compute_calls(void)
{
    enum { BUFFER = 0x700000000ull };
    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {
        0x12345678, 0x00000000,
        (32 / 4 - 1) | (16 / 8 - 1) << 6,                    /* RSRC1: 32 VGPRs, 16 SGPRs */
        12 << 1 | 1u << 7 | 1u << 10 | 2u << 11,             /* RSRC2: 12 user SGPRs, TGID_X, TG_SIZE, ids in v0-v2 */
        64, 2, 1,
    };
    rt_compute_shader c;
    rt_compute_shader_decode(shader, &c);
    CHECK(c.code == 0x1234567800ull && c.vgprs == 32 && c.sgprs == 16 && c.user_sgprs == 12);
    CHECK(c.workgroup_id_enables == 1 && c.workgroup_size_enabled && c.thread_id_components == 2);
    CHECK(c.threads[0] == 64 && c.threads[1] == 2 && c.threads[2] == 1);
    char error[128] = "";
    size_t count = 0;
    rt_render_forget();
    CHECK(rt_render_record_compute_shader_at(BUFFER + 0x10, shader) == 0);
    const uint32_t groups[3] = {4, 2, 1}, none[3] = {4, 0, 1};
    CHECK(rt_render_record_dispatch(BUFFER, 0x20, groups, error, sizeof error) == 0);
    CHECK(rt_render_record_dispatch(BUFFER, 0x30, none, error, sizeof error) == -1 && strstr(error, "none to run"));
    const rt_render_call *calls = rt_render_submitted(BUFFER, 0x100, &count);
    CHECK(count == 2 && calls[0].kind == RT_CALL_COMPUTE_SHADER && calls[0].offset == 0x10 && calls[1].kind == RT_CALL_DISPATCH &&
          calls[1].u.dispatch.groups[0] == 4 && calls[1].u.dispatch.groups[1] == 2);
    rt_render_forget();
}

/* Index state keeps the emitted raw words and follows each buffer's submission lifetime. */
static void index_state(void)
{
    const uint64_t buffer = 0x600000000ull, other = 0x600100000ull;
    char error[128];
    size_t count;
    rt_render_forget();
    CHECK(RT_CALL_REGISTER == 6 && RT_CALL_INSTANCES == 7 && RT_CALL_INDEX_TYPE == 12);
    CHECK(rt_render_record_index_type(buffer, 0x10, 0x80000001u, error, sizeof error) == 0);
    CHECK(rt_render_record_index_base(buffer, 0x18, 0xef12345678ull, error, sizeof error) == 0);
    CHECK(rt_render_record_index_size(buffer, 0x24, 0xffffffffu, error, sizeof error) == 0);
    CHECK(rt_render_record_index_type(other, 0, 1, error, sizeof error) == 0);
    const rt_render_call *calls = rt_render_submitted(buffer, 0x30, &count);
    CHECK(count == 3);
    if (count == 3) {
        CHECK(calls[0].kind == RT_CALL_INDEX_TYPE && calls[0].u.index_type.type == 0x80000001u);
        CHECK(calls[1].kind == RT_CALL_INDEX_BASE && calls[1].u.index_base.address == 0xef12345678ull);
        CHECK(calls[2].kind == RT_CALL_INDEX_SIZE && calls[2].u.index_size.count == 0xffffffffu);
    }
    rt_render_submitted(buffer, 0x24, &count);
    CHECK(count == 2);
    calls = rt_render_submitted(other, 0x10, &count);
    CHECK(count == 1 && calls[0].u.index_type.type == 1);
    CHECK(rt_render_record_index_size(buffer, 0, 0, error, sizeof error) == 0);
    calls = rt_render_submitted(buffer, 0x30, &count);
    CHECK(count == 1 && calls[0].kind == RT_CALL_INDEX_SIZE && calls[0].u.index_size.count == 0);
    rt_render_forget();
    rt_render_submitted(buffer, 0x30, &count);
    CHECK(count == 0);
}

/* Raw draw payloads distinguish an address from a base-relative index offset. */
static void indexed_draws(void)
{
    const uint64_t buffer = 0x700000000ull;
    const uint64_t full_address = 0xabcdef1234567891ull;
    char error[128];
    size_t count;
    rt_render_forget();
    CHECK(RT_CALL_INDEX_SIZE == 11 && RT_CALL_DRAW_INDEXED == 14 && RT_CALL_DRAW_INDEX_OFFSET == 13);
    CHECK(rt_render_record_draw_indexed(buffer, 0x10, 0xc0042701, 0x80000003, full_address, 0xffffffff, 0x12345678,
                                        error, sizeof error) == 0);
    CHECK(rt_render_record_draw_index_offset_packet(buffer, 0x30, 0xc0033501, 0x11223344, 0xfedcba98, 0x88776655, 0x87654321,
                                               error, sizeof error) == 0);
    const rt_render_call *calls = rt_render_submitted(buffer, 0x40, &count);
    CHECK(count == 2);
    if (count == 2) {
        CHECK(calls[0].kind == RT_CALL_DRAW_INDEXED && calls[0].u.draw_indexed.max_size == 0x80000003);
        CHECK(calls[0].u.draw_indexed.header == 0xc0042701 && calls[0].u.draw_indexed.address == full_address && calls[0].u.draw_indexed.index_count == 0xffffffff);
        CHECK(calls[0].u.draw_indexed.initiator == 0x12345678);
        CHECK(calls[1].kind == RT_CALL_DRAW_INDEX_OFFSET && calls[1].u.draw_index.max_size == 0x11223344);
        CHECK(calls[1].u.draw_index.header == 0xc0033501 && calls[1].u.draw_index.first == 0xfedcba98);
        CHECK(calls[1].u.draw_index.count == 0x88776655 && calls[1].u.draw_index.initiator == 0x87654321);
    }
    rt_render_submitted(buffer, 0x30, &count);
    CHECK(count == 1);
    CHECK(rt_render_record_draw_indexed(buffer, 0, 0, 0, 0, 0, 0, error, sizeof error) == 0);
    calls = rt_render_submitted(buffer, 0x40, &count);
    CHECK(count == 1 && calls[0].kind == RT_CALL_DRAW_INDEXED && calls[0].u.draw_indexed.index_count == 0);
    rt_render_forget();
    CHECK(rt_render_record_draw_index_offset(buffer, 0, 136, 60, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_index_offset_packet(buffer, 4, 0, 2, 136, 60, 0, error, sizeof error) == 0);
    calls = rt_render_submitted(buffer, 8, &count);
    CHECK(count == 2);
    if (count == 2) {
        CHECK(calls[0].kind == RT_CALL_DRAW_INDEX_OFFSET && calls[0].u.draw_index.first == 136);
        CHECK(calls[0].u.draw_index.count == 60 && calls[0].u.draw_index.has_packet_fields == 0);
        CHECK(calls[0].u.draw_index.header == 0 && calls[0].u.draw_index.max_size == 0 && calls[0].u.draw_index.initiator == 0);
        CHECK(calls[1].kind == RT_CALL_DRAW_INDEX_OFFSET && calls[1].u.draw_index.has_packet_fields == 1);
        CHECK(calls[1].u.draw_index.header == 0 && calls[1].u.draw_index.max_size == 2);
        CHECK(calls[1].u.draw_index.first == 136 && calls[1].u.draw_index.count == 60);
    }
    rt_render_forget();
}

int main(void)
{
    decoding();
    index_state();
    indexed_draws();
    registers_and_instances();
    shader_blocks();
    user_data();
    recording();
    placed_calls();
    compute_calls();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
