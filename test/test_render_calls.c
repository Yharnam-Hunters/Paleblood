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

int main(void)
{
    decoding();
    recording();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
