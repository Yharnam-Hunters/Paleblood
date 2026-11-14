// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: reading a block's length from a Scaleform heap page's bitset (the code in
// scaleform_heap.h).
//
// engine_scaleform_heap_bitset_units_027ec890 (0x027ec890) takes a page's bitset and the unit an
// allocated block starts at, and returns the block's length in units, as the marking
// (scaleform_heap_bitset_mark.cpp) encoded it: a first code of 1 or 2 is the length itself; 3 means
// longer, and the next code (when not 3) is the length less 3; after two 3s, the next code (when not
// 3) and the two after it are six bits of the length less 6; after three 3s, the length is the whole
// 32-bit word the marking stored for it.
#include <cstdint>

#include "scaleform_heap.h"

namespace {

using namespace engine::scaleform_heap;

constexpr uint32_t code_continues = 3;
constexpr uint32_t short_bias = 3;              // a second code holds the length less this
constexpr uint32_t medium_bias = 6;             // two 3s: the next three codes hold the length less this
constexpr uint32_t medium_high_shift = 4;
constexpr uint32_t medium_middle_shift = 2;

}  // namespace

extern "C" uint32_t bb_engine_scaleform_heap_bitset_units_027ec890(const uint32_t *bitset, uint64_t unit)
{
    const uint32_t first = unit_code(bitset, unit);
    if (first != code_continues)
        return first;
    const uint32_t second = unit_code(bitset, unit + second_code_unit);
    if (second != code_continues)
        return second + short_bias;
    const uint32_t third = unit_code(bitset, unit + third_code_unit);
    if (third != code_continues) {
        return ((third << medium_high_shift) | (unit_code(bitset, unit + fourth_code_unit) << medium_middle_shift) |
                unit_code(bitset, unit + fifth_code_unit)) + medium_bias;
    }
    return bitset[long_length_word(unit)];
}
