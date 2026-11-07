/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Initialize the two work areas used by VideoOut's record transformer.

   The complete 286-byte native leaf at module offset 0x8820 in the supplied
   libSceVideoOut (SHA-256 33a6f16349f07637e168db0d6338f0b2ae8aea626b86bc3d28325ecb5155e2c1)
   has no input reads, calls, globals, checks or guard. Its two write bases may
   alias, so retain the interleaved stores. No state-base store targets bytes
   0x10..0x1f; an aliased staging base can still change them. The return is the
   full address of the first state bank.

   This implementation describes ordinary writable storage;
   raw word copies target supported little-endian hosts. Exact compiler store
   lowering, machine-fault progress and native SIMD/register
   side effects remain unverified. It does not implement an exported SCE API. */
#include "runtime/video_out_transform.h"

#include <string.h>

static const uint64_t initial_header_00 = UINT64_C(0x000000013f800000);
static const uint64_t initial_header_08 = UINT64_C(0xffffffff00000000);
static const uint64_t initial_header_20 = UINT64_C(0x0000000a00000003);
static const uint8_t zero_vector[RT_VIDEO_OUT_TRANSFORM_VECTOR_BYTES] = {0};

enum { initial_staging_control = 1 };

static void store_word(uint8_t field[RT_VIDEO_OUT_TRANSFORM_WORD_BYTES], uint32_t value)
{
    memcpy(field, &value, sizeof value);
}

uint64_t rt_video_out_transform_initialize(void *state_storage, void *staging_storage)
{
    rt_video_out_transform_state *state = state_storage;
    rt_video_out_transform_staging_prefix *staging = staging_storage;

    memcpy(state->header_00, &initial_header_00, sizeof initial_header_00);
    memcpy(state->header_08, &initial_header_08, sizeof initial_header_08);
    store_word(staging->control_00, initial_staging_control);
    memcpy(state->header_20, &initial_header_20, sizeof initial_header_20);
    store_word(state->row_count_28, RT_VIDEO_OUT_TRANSFORM_ROWS);
    memcpy(staging->tail_284, zero_vector, sizeof staging->tail_284);
    store_word(staging->word_294, 0);

    for (size_t row = 0; row < RT_VIDEO_OUT_TRANSFORM_ROWS; ++row) {
        store_word(state->lane_2c[row], 0);
        store_word(state->lane_6c[row], 0);
        store_word(state->lane_ac[row], 0);

        for (size_t bank = 0; bank < RT_VIDEO_OUT_TRANSFORM_BANKS; ++bank)
            memcpy(state->banks[bank][row].leading_00, zero_vector, sizeof zero_vector);
        memcpy(staging->rows[row].leading_00, zero_vector, sizeof zero_vector);

        for (size_t bank = 0; bank < RT_VIDEO_OUT_TRANSFORM_BANKS; ++bank)
            store_word(state->banks[bank][row].word_20, 0);
        store_word(staging->rows[row].word_20, 0);

        for (size_t bank = 0; bank < RT_VIDEO_OUT_TRANSFORM_BANKS; ++bank)
            store_word(state->banks[bank][row].word_24, 0);
        store_word(staging->rows[row].word_24, 0);
    }
    return (uint64_t)(uintptr_t)&state->banks[0];
}
