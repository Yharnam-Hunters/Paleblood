// SPDX-License-Identifier: GPL-2.0-or-later
// Select the first matching component arrangement from four packed three-bit
// codes. The encoding byte chooses single, pair, triple, or four-code rules.
// Rejected arrangements leave the optional output untouched. Only AL is Boolean.
#include <array>
#include <cstring>

#include "record_encoding.h"

namespace {
constexpr uint32_t component_count_4 = 4;
}  // namespace


extern "C" uint8_t bb_render_record_encoding_components(const uint32_t *encoding, uint32_t *output)
{
    constexpr uint32_t encoding_mask = 0xff;
    constexpr uint32_t component_mask = 7;
    constexpr uint32_t first_shift = 12, second_shift = 15, third_shift = 18, fourth_shift = 21;
    constexpr uint32_t first_code = 4, second_code = 5, third_code = 6, fourth_code = 7;
    constexpr uint32_t single_rule = 1, pair_rule = 2;
    constexpr uint64_t triple_encodings = 0x04000103000120c0ULL;
    uint32_t packed;
    std::memcpy(&packed, encoding, sizeof(packed));
    const uint32_t a = (packed >> first_shift) & component_mask;
    const uint32_t b = (packed >> second_shift) & component_mask;
    const uint32_t c = (packed >> third_shift) & component_mask;
    const uint32_t d = (packed >> fourth_shift) & component_mask;
    const uint32_t index = packed & encoding_mask;
    const bool has_table_entry = index < render::record_encoding_metadata_entries;
    const uint32_t rule = has_table_entry ? render::record_encoding_component_rules.get()[index] : 0;
    std::array<bool, component_count_4> matches;
    if (rule == single_rule) {
        matches = {a == first_code, b == first_code, c == first_code, d == first_code};
    } else if (rule == pair_rule) {
        matches = {a == first_code && b == second_code,
                   a == first_code && d == second_code,
                   b == first_code && a == second_code,
                   d == first_code && a == second_code};
    } else if (has_table_entry && ((triple_encodings >> index) & 1) != 0) {
        matches = {a == first_code && b == second_code && c == third_code,
                   a == first_code && b == second_code && d == third_code,
                   c == first_code && b == second_code && a == third_code,
                   d == first_code && b == second_code && a == third_code};
    } else {
        matches = {a == first_code && b == second_code && c == third_code && d == fourth_code,
                   c == first_code && b == second_code && a == third_code && d == fourth_code,
                   d == first_code && c == second_code && b == third_code && a == fourth_code,
                   d == first_code && a == second_code && b == third_code && c == fourth_code};
    }
    for (uint32_t component = 0; component < matches.size(); ++component) {
        if (!matches[component]) continue;
        if (output != nullptr) std::memcpy(output, &component, sizeof(component));
        return 1;
    }
    return 0;
}
