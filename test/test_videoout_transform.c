/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Synthetic layout and wire checks; original-library alias comparisons are reported separately. */
#include "runtime/video_out_transform.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint32_t word(const void *p) { uint32_t value; memcpy(&value, p, sizeof value); return value; }
int main(void)
{
    rt_video_out_transform_state state;
    rt_video_out_transform_staging_prefix staging;
    memset(&state, 0xa5, sizeof state);
    memset(&staging, 0x5a, sizeof staging);
    CHECK(rt_video_out_transform_initialize(&state, &staging) == (uint64_t)(uintptr_t)&state.banks[0]);
    CHECK(word(state.header_00) == 0x3f800000 && word(state.header_00 + 4) == 1);
    CHECK(word(state.header_08) == 0 && word(state.header_08 + 4) == UINT32_MAX);
    CHECK(word(state.header_20) == 3 && word(state.header_20 + 4) == 10 && word(state.row_count_28) == 16);
    for (size_t i = 0; i < sizeof state.unknown_10; i++) CHECK(state.unknown_10[i] == 0xa5);
    const unsigned char *cleared = (const unsigned char *)&state + offsetof(rt_video_out_transform_state, lane_2c);
    for (size_t i = 0; i < sizeof state - offsetof(rt_video_out_transform_state, lane_2c); i++) CHECK(cleared[i] == 0);
    CHECK(word(staging.control_00) == 1 && word(staging.word_294) == 0);
    for (size_t i = 0; i < sizeof staging.rows; i++) CHECK(((unsigned char *)staging.rows)[i] == 0);
    for (size_t i = 0; i < sizeof staging.tail_284; i++) CHECK(staging.tail_284[i] == 0);

    const uint32_t control = 0xfedcba98, trailing = 0x12345678;
    memcpy(staging.control_00, &control, sizeof control);
    memcpy(staging.word_294, &trailing, sizeof trailing);
    for (size_t i = 0; i < sizeof staging.tail_284; i++) staging.tail_284[i] = (uint8_t)(0xe0 + i);
    for (size_t row = 0; row < RT_VIDEO_OUT_TRANSFORM_ROWS; row++) {
        for (size_t index = 0; index < 8; index++) {
            const uint32_t value = 0xabcd0000u + (uint32_t)(row * 16 + index);
            memcpy(staging.rows[row].leading_00 + index * 4, &value, sizeof value);
        }
        memset(staging.rows[row].word_20, 0x56, 4);
        memset(staging.rows[row].word_24, 0x78, 4);
    }
    struct { uint8_t before[7]; rt_video_out_transform_packed output; uint8_t after[9]; } guarded;
    memset(&guarded, 0xa5, sizeof guarded);
    CHECK(rt_video_out_transform_pack(&staging, &guarded.output) == 0);
    CHECK(word(guarded.output.zero_00) == 0 && word(guarded.output.control_04) == control && word(guarded.output.word_08) == trailing);
    for (size_t i = 0; i < sizeof guarded.output.reserved_0c; i++) CHECK(guarded.output.reserved_0c[i] == 0);
    CHECK(!memcmp(guarded.output.tail_10, staging.tail_284, sizeof staging.tail_284));
    for (size_t row = 0; row < RT_VIDEO_OUT_TRANSFORM_ROWS; row++) {
        for (size_t index = 0; index < 8; index++) {
            uint16_t value;
            memcpy(&value, guarded.output.rows[row].leading_00 + index * 2, sizeof value);
            CHECK(value == row * 16 + index);
        }
        CHECK(guarded.output.rows[row].word_10[0] == 0x56 && guarded.output.rows[row].word_10[1] == 0x56);
        CHECK(guarded.output.rows[row].word_12[0] == 0x78 && guarded.output.rows[row].word_12[1] == 0x78);
    }
    for (size_t i = 0; i < sizeof guarded.before; i++) CHECK(guarded.before[i] == 0xa5);
    for (size_t i = 0; i < sizeof guarded.after; i++) CHECK(guarded.after[i] == 0xa5);
    puts("videoout transform: synthetic layout and native wires passed");
    return 0;
}
