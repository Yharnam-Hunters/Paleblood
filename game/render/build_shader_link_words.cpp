// SPDX-License-Identifier: GPL-2.0-or-later
// Match byte-pair keys and construct SPI_PS_INPUT_CNTL entries. The four stores
// remain ordered because the output may overlap either input array.
#include "shader_link.h"

extern "C" void bb_render_build_shader_link_words(void *output, const void *first_pairs,
                                                   uint32_t first_count,
                                                   const void *second_pairs,
                                                   uint32_t count)
{
    using namespace render;
    constexpr uint32_t use_default_offset = 0x20; // OFFSET selects DEFAULT_VAL.
    constexpr unsigned source_offset_shift = 7;
    constexpr uint32_t source_offset_bits_mask = 0x1e0;
    constexpr uint32_t offset_mask = 0x3f;
    constexpr uint32_t default_value_mask = 3;
    constexpr unsigned default_value_shift = 8;
    constexpr unsigned flat_shade_bit = 10;
    constexpr unsigned second_flat_shade_source_bit = 12;
    auto *out = static_cast<Field<uint32_t> *>(output);
    const auto *first = static_cast<const ShaderLinkInputPair *>(first_pairs);
    const auto *second = static_cast<const ShaderLinkInputPair *>(second_pairs);
    for (uint64_t i = 0; i != count; ++i) {
        uint32_t chosen = use_default_offset;
        for (uint64_t j = 0; j != first_count; ++j) {
            const uint8_t first_key = first[j].key;
            const uint8_t second_key = second[i].key;
            if (first_key != second_key) continue;
            chosen = first[j].value;
            break;
        }
        // These intervening reads are live when output aliases either input array.
        out[i].set(0);
        const uint32_t first_word = second[i].word();
        uint32_t value = (((first_word >> source_offset_shift) & source_offset_bits_mask) |
                          chosen) & offset_mask;
        out[i].set(value);
        const uint32_t high_byte = second[i].value;
        value |= (high_byte & default_value_mask) << default_value_shift;
        out[i].set(value);
        const uint32_t final_word = second[i].word();
        value |= (((final_word >> flat_shade_bit) | (final_word >> second_flat_shade_source_bit)) & 1)
                 << flat_shade_bit;
        out[i].set(value);
    }
}
