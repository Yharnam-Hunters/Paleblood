// SPDX-License-Identifier: GPL-2.0-or-later
// Snapshot exactly the original unaligned DWORD before extracting its fields.
#include "record_mapping_word.h"

#include <cstring>

extern "C" uint64_t bb_render_read_record_mapping_word(
    const render::RecordMappingSource *source)
{
    uint32_t word;
    std::memcpy(&word, source->mapping_word_10, sizeof(word));

    constexpr uint32_t first_shift = 2;
    constexpr uint32_t first_mask = 0x1fu;
    constexpr uint32_t second_shift = 8;
    constexpr uint32_t second_mask = 0x07u;
    constexpr uint32_t mode_shift = 11;
    constexpr uint32_t mode_mask = 0x03u;
    const uint32_t first = (word >> first_shift) & first_mask;
    const uint32_t second = (word >> second_shift) & second_mask;
    const uint32_t mode = (word >> mode_shift) & mode_mask;
    return render::pack_record_mapping_word(first, second, mode);
}
