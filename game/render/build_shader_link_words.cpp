// SPDX-License-Identifier: GPL-2.0-or-later
// Match byte-pair keys, then publish each packed word in the native four-store order.
#include "shader_link.h"

extern "C" void bb_render_build_shader_link_words(void *output, const void *first_pairs,
                                                   uint32_t first_count,
                                                   const void *second_pairs,
                                                   uint32_t count)
{
    using namespace render;
    constexpr uint32_t missing_value = 32;
    constexpr unsigned source_middle_shift = 7;
    constexpr uint32_t source_middle_mask = 0x1e0;
    constexpr uint32_t low_mask = 0x3f;
    constexpr uint32_t middle_mask = 3;
    constexpr unsigned middle_shift = 8;
    constexpr unsigned first_flag_bit = 10;
    constexpr unsigned second_flag_bit = 12;
    auto *out = static_cast<Field<uint32_t> *>(output);
    const auto *first = static_cast<const ShaderLinkInputPair *>(first_pairs);
    const auto *second = static_cast<const ShaderLinkInputPair *>(second_pairs);
    for (uint64_t i = 0; i != count; ++i) {
        uint32_t chosen = missing_value;
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
        uint32_t value = (((first_word >> source_middle_shift) & source_middle_mask) |
                          chosen) & low_mask;
        out[i].set(value);
        const uint32_t high_byte = second[i].value;
        value |= (high_byte & middle_mask) << middle_shift;
        out[i].set(value);
        const uint32_t final_word = second[i].word();
        value |= (((final_word >> first_flag_bit) | (final_word >> second_flag_bit)) & 1)
                 << first_flag_bit;
        out[i].set(value);
    }
}
