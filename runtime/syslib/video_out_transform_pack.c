/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Pack VideoOut's staging words into a 352-byte internal output record.

   The complete 208-byte leaf at module offset 0x8750 in the supplied
   libSceVideoOut (SHA-256 33a6f16349f07637e168db0d6338f0b2ae8aea626b86bc3d28325ecb5155e2c1)
   uses two pointers and a fixed byte shuffle, without calls, checks or a guard.
   Clearing precedes every source read; later rows and trailing WORDs stay live
   when the two areas alias. There is no saturation or numeric interpretation.

   This implementation describes ordinary accessible memory.
   Exact compiler access widths, machine-fault progress and native SIMD/register
   side effects remain unverified. No public SCE export/device format is claimed. */
#include "runtime/video_out_transform.h"

#include <string.h>

enum {
    clear_window_count = 10,
    captured_group_bytes = 16,
    dword_group_count = 4,
    low_word_bytes = 2,
    packed_group_bytes = dword_group_count * low_word_bytes,
};

/* The first native zero window overlaps the following one by twelve bytes.
   These byte views keep that window and the ten descending stores explicit. */
typedef struct {
    uint8_t unknown_00[0x0c];
    uint8_t windows_0c[clear_window_count][RT_VIDEO_OUT_TRANSFORM_VECTOR_BYTES];
    uint8_t unknown_14c[0x14];
} packed_clear_windows;

typedef struct {
    uint8_t unknown_00[0x140];
    uint8_t window_140[RT_VIDEO_OUT_TRANSFORM_VECTOR_BYTES];
} packed_last_clear_window;

_Static_assert(offsetof(packed_clear_windows, windows_0c) == 0x0c, "clear windows");
_Static_assert(sizeof(packed_clear_windows) == sizeof(rt_video_out_transform_packed), "clear size");
_Static_assert(offsetof(packed_last_clear_window, window_140) == 0x140, "last window");
_Static_assert(sizeof(packed_last_clear_window) == sizeof(rt_video_out_transform_packed), "last size");

static const uint8_t zero_vector[RT_VIDEO_OUT_TRANSFORM_VECTOR_BYTES] = {0};

uint64_t rt_video_out_transform_pack(const void *source_storage, void *output_storage)
{
    const rt_video_out_transform_staging_prefix *source = source_storage;
    rt_video_out_transform_packed *output = output_storage;
    packed_clear_windows *clear = output_storage;
    packed_last_clear_window *last = output_storage;

    memcpy(last->window_140, zero_vector, sizeof zero_vector);
    for (size_t window = clear_window_count; window > 0; --window)
        memcpy(clear->windows_0c[window - 1], zero_vector, sizeof zero_vector);

    uint8_t captured_tail[RT_VIDEO_OUT_TRANSFORM_TAIL_BYTES];
    memcpy(captured_tail, source->tail_284, sizeof captured_tail);
    memcpy(output->tail_10, captured_tail, sizeof captured_tail);
    const uint32_t zero_word = 0;
    memcpy(output->zero_00, &zero_word, sizeof zero_word);

    uint32_t captured_word;
    memcpy(&captured_word, source->control_00, sizeof captured_word);
    memcpy(output->control_04, &captured_word, sizeof captured_word);
    memcpy(&captured_word, source->word_294, sizeof captured_word);
    memcpy(output->word_08, &captured_word, sizeof captured_word);

    for (size_t row = 0; row < RT_VIDEO_OUT_TRANSFORM_ROWS; ++row) {
        uint8_t high[captured_group_bytes];
        uint8_t low[captured_group_bytes];
        uint8_t packed[captured_group_bytes];
        memcpy(high, source->rows[row].leading_00 + captured_group_bytes, sizeof high);
        memcpy(low, source->rows[row].leading_00, sizeof low);
        for (size_t word = 0; word < dword_group_count; ++word) {
            memcpy(packed + low_word_bytes * word,
                   low + RT_VIDEO_OUT_TRANSFORM_WORD_BYTES * word, low_word_bytes);
            memcpy(packed + packed_group_bytes + low_word_bytes * word,
                   high + RT_VIDEO_OUT_TRANSFORM_WORD_BYTES * word, low_word_bytes);
        }
        memcpy(output->rows[row].leading_00, packed, sizeof packed);

        /* Each read follows the previous store, including within the same row. */
        uint8_t trailing_word[low_word_bytes];
        memcpy(trailing_word, source->rows[row].word_20, sizeof trailing_word);
        memcpy(output->rows[row].word_10, trailing_word, sizeof trailing_word);
        memcpy(trailing_word, source->rows[row].word_24, sizeof trailing_word);
        memcpy(output->rows[row].word_12, trailing_word, sizeof trailing_word);
    }
    return 0;
}
