// SPDX-License-Identifier: GPL-2.0-or-later
// Input: the key mapper and the input's set of held inputs, and the three lookups the key assign
// (key_assign.cpp) asks of them (replaced in key_mapper.cpp). Each lookup is a std::map find on a
// map keyed by a signed 32-bit number (int_map.h).
#pragma once

#include <cstddef>
#include <cstdint>

#include "int_map.h"
#include "runtime/original.h"

namespace input {

// What the lookups return when the map has no entry for the key.
constexpr int32_t no_index = -1;

struct KeyMapper {
    uint8_t unknown_0x00[0x40];
    IntMap<int32_t> *button_indexes;   // logical key -> the input's button index
    IntMap<int32_t> alternate_inputs;  // button index -> the alternate input (another device's)
};
static_assert(offsetof(KeyMapper, button_indexes) == 0x40);
static_assert(offsetof(KeyMapper, alternate_inputs.head) == 0x50);

struct HeldInputSet {
    uint8_t unknown_0x00[0x10];
    IntMap<uint8_t> inputs;   // input -> held (any nonzero byte counts)
};
static_assert(offsetof(HeldInputSet, inputs.head) == 0x18);

// The input's button index assigned to logical key `key`, or no_index.
RT_ORIGINAL(0x01390ba0, key_mapper_button_index, int32_t(KeyMapper *mapper, int32_t key));
// The second input a button index is assigned to, or no_index.
RT_ORIGINAL(0x0138d060, key_mapper_alternate_input, int32_t(KeyMapper *mapper, int32_t button));
// Whether `input` is held (in the set with a nonzero flag), in the low byte of the result.
RT_ORIGINAL(0x013994a0, held_set_contains, uint8_t(HeldInputSet *set, int32_t input));

}  // namespace input
