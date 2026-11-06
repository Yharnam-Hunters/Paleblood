// SPDX-License-Identifier: GPL-2.0-or-later
// Capture the two packed words in native order, then reconstruct the encoding
// through the shared scalar packer. No source pointer is passed to that packer.
#include "record_descriptor_decoder.h"

extern "C" uint32_t bb_render_record_descriptor_encoding(const render::RecordDescriptorPrefix *record)
{
    constexpr unsigned first_shift = 20, second_shift = 26;
    constexpr unsigned fourth_shift = 3, fifth_shift = 6, sixth_shift = 9;
    constexpr uint32_t first_mask = 63, second_mask = 15, component_mask = 7;
    const uint32_t first_word = record->pair_00.high.get();
    const uint32_t second_word = record->pair_08.high.get();
    return render::compose_record_encoding_fields(
        static_cast<uint8_t>((first_word >> first_shift) & first_mask),
        (first_word >> second_shift) & second_mask, second_word & component_mask,
        (second_word >> fourth_shift) & component_mask,
        (second_word >> fifth_shift) & component_mask,
        (second_word >> sixth_shift) & component_mask);
}
