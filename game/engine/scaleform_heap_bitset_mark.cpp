// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: marking an allocated block in a Scaleform heap page's bitset (the code in
// scaleform_heap.h).
//
// engine_scaleform_heap_bitset_mark_027ec640 (0x027ec640) takes a page's bitset, the block's first
// unit and its length in units. Every allocated block ends with code 1 in its last unit; its first
// units say how long it is, read back by scaleform_heap_bitset_units.cpp:
//   - 1 unit: code 1; 2 units: code 2;
//   - 3 to 5 units: 3, then the length less 3;
//   - 6 to 0x25 units: 3, 3, then the length less 6 in three codes (high to low);
//   - longer: 3, 3, 3, and the length as a whole 32-bit word of the bitset further on.
// Codes between are left as they were.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

constexpr uint32_t code_end = 1;
constexpr uint32_t code_two_units = 2;
constexpr uint32_t code_continues = 3;
constexpr uint64_t two_units = 2;
constexpr uint64_t short_bias = 3;              // 3 to 5 units: the second code is the length less this
constexpr uint64_t short_lengths = 3;
constexpr uint64_t medium_bias = 6;
constexpr uint64_t longest_medium = 0x25;
constexpr uint32_t medium_high_shift = 4;
constexpr uint32_t medium_middle_shift = 2;

}  // namespace

extern "C" void bb_engine_scaleform_heap_bitset_mark_027ec640(uint32_t *bitset, uint64_t unit, uint64_t units)
{
    if (units - short_bias < short_lengths) {
        set_unit_code(bitset, unit, code_continues);
        set_unit_code(bitset, unit + second_code_unit, static_cast<uint32_t>(units - short_bias));
    } else if (units == two_units) {
        set_unit_code(bitset, unit, code_two_units);
    } else if (units <= 1) {
        set_unit_code(bitset, unit, code_end);  // the block's only unit is its end
        return;
    } else if (units <= longest_medium) {
        const uint32_t rest = static_cast<uint32_t>(units - medium_bias);
        set_unit_code(bitset, unit, code_continues);
        set_unit_code(bitset, unit + second_code_unit, code_continues);
        set_unit_code(bitset, unit + third_code_unit, rest >> medium_high_shift);
        set_unit_code(bitset, unit + fourth_code_unit, (rest >> medium_middle_shift) & unit_code_mask);
        set_unit_code(bitset, unit + fifth_code_unit, rest & unit_code_mask);
    } else {
        set_unit_code(bitset, unit, code_continues);
        set_unit_code(bitset, unit + second_code_unit, code_continues);
        set_unit_code(bitset, unit + third_code_unit, code_continues);
        bitset[long_length_word(unit)] = static_cast<uint32_t>(units);
    }
    set_unit_code(bitset, unit + units - 1, code_end);
}
