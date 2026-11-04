// SPDX-License-Identifier: GPL-2.0-or-later
// Decode the encoding's category nibble. Rejected nibbles leave the optional
// output untouched; accepted categories can be queried without an output.
#include <cstdint>
#include <cstring>

namespace {
constexpr uint32_t category_nibble_count = 16;
constexpr uint32_t invalid_category = 0xffffffff;
// Index by the packed category nibble. Nibble 9 maps to category 6;
// all unsupported nibbles reject without publishing an output.
constexpr uint32_t category_by_nibble[category_nibble_count] = {
    0, 1, invalid_category, invalid_category, 4, 5, invalid_category, 7,
    invalid_category, 6, invalid_category, invalid_category,
    invalid_category, invalid_category, invalid_category, invalid_category,
};
}  // namespace

extern "C" uint8_t bb_render_record_encoding_category(const uint32_t *encoding, uint32_t *output)
{
    constexpr uint32_t category_shift = 8;
    constexpr uint32_t category_mask = 0xf;
    uint32_t packed;
    std::memcpy(&packed, encoding, sizeof(packed));
    const uint32_t category = category_by_nibble[(packed >> category_shift) & category_mask];
    if (category == invalid_category) return 0;
    if (output != nullptr) std::memcpy(output, &category, sizeof(category));
    return 1;
}
