// SPDX-License-Identifier: GPL-2.0-or-later
// Input: the lookups the key assign (game/input/key_assign.cpp) asks of the key mapper and of the
// input's set of held inputs. Each is a std::map find on a map keyed by a signed 32-bit number
// (input/int_map.h): the mapper's map from logical key to button index, its map from button index
// to the alternate input (another device's), and the held set's map from input to a held flag.
#include <cstddef>
#include <cstdint>

#include "int_map.h"
#include "runtime/original.h"

namespace {

using input::IntMap;
using input::IntMapNode;

constexpr int32_t no_index = -1;

struct KeyMapper {
    uint8_t unknown_0x00[0x40];
    IntMap<int32_t> *button_indexes;   // logical key -> the input's button index
    IntMap<int32_t> alternate_inputs;  // button index -> the alternate input
};
static_assert(offsetof(KeyMapper, button_indexes) == 0x40);
static_assert(offsetof(KeyMapper, alternate_inputs.head) == 0x50);

struct HeldInputSet {
    uint8_t unknown_0x00[0x10];
    IntMap<uint8_t> inputs;   // input -> held (any nonzero byte counts)
};
static_assert(offsetof(HeldInputSet, inputs.head) == 0x18);
static_assert(offsetof(IntMapNode<int32_t>, key) == 0x1c && offsetof(IntMapNode<int32_t>, value) == 0x20);

// The value stored under `key`, or no_index when the map has none.
int32_t value_or_none(IntMapNode<int32_t> *head, int32_t key)
{
    const IntMapNode<int32_t> *node = input::int_map_find(head, key);
    return node != head ? node->value : no_index;
}

}  // namespace

// The input's button index assigned to logical key `key`, or -1.
extern "C" int32_t bb_input_key_mapper_button_index_01390ba0(KeyMapper *mapper, int32_t key)
{
    return value_or_none(mapper->button_indexes->head, key);
}

// The second input that button index `button` is assigned to, or -1.
extern "C" int32_t bb_input_key_mapper_alternate_input_0138d060(KeyMapper *mapper, int32_t button)
{
    return value_or_none(mapper->alternate_inputs.head, button);
}

// Whether `input` is in the held set with a nonzero flag; 0 when it is missing.
extern "C" uint8_t bb_input_held_set_contains_013994a0(HeldInputSet *set, int32_t input)
{
    IntMapNode<uint8_t> *head = set->inputs.head;
    const IntMapNode<uint8_t> *node = input::int_map_find(head, input);
    return node != head && node->value != 0;
}
