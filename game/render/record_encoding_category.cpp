// SPDX-License-Identifier: GPL-2.0-or-later
// Decode the encoding's category nibble. Rejected nibbles leave the optional
// output untouched; accepted categories can be queried without an output.
#include <cstdint>
#include <cstring>

namespace {
constexpr uint32_t category_code_4 = 4;
constexpr uint32_t category_code_5 = 5;
constexpr uint32_t category_code_7 = 7;
constexpr uint32_t category_code_6 = 6;
constexpr uint32_t category_code_9 = 9;
}  // namespace


extern "C" uint8_t bb_render_record_encoding_category(const uint32_t *encoding, uint32_t *output)
{
    constexpr uint32_t category_shift = 8;
    constexpr uint32_t category_mask = 0xf;
    uint32_t packed;
    std::memcpy(&packed, encoding, sizeof(packed));
    uint32_t category;
    switch ((packed >> category_shift) & category_mask) {
    case 0: category = 0; break;
    case 1: category = 1; break;
    case category_code_4: category = category_code_4; break;
    case category_code_5: category = category_code_5; break;
    case category_code_7: category = category_code_7; break;
    case category_code_9: category = category_code_6; break;
    default: return 0;
    }
    if (output != nullptr) std::memcpy(output, &category, sizeof(category));
    return 1;
}
