// SPDX-License-Identifier: GPL-2.0-or-later
// Input: queries of the key assign (the game's FD4 key assign), which turns the game's logical
// keys (menu up, confirm, ...) into the state of the physical buttons and sticks they are
// assigned to.
//
// A logical key is answered only when the key assign is enabled, the key has an assignment (the
// key mapper gives the input's button index for it) and its entry in the key state map says it
// is enabled and not suppressed. The map is looked up twice, once per flag, as the original
// does.
//
// The same file has the search for the first free id among a table's assignments.
#include <cstddef>
#include <cstdint>

#include "../engine/msvc_tree.h"
#include "runtime/original.h"

namespace {

// A node of the key state map, MSVC's std::map<uint32_t, KeyState>. Its key and value form a
// 4-byte aligned pair, so the key sits at +0x1c (engine::TreeNode's pairs are 8-byte aligned).
struct KeyStateNode {
    KeyStateNode *left;
    KeyStateNode *parent;
    KeyStateNode *right;
    uint8_t color;
    uint8_t is_nil;
    uint8_t unknown_0x1a[0x2];
    uint32_t key;
    uint8_t enabled;      // any nonzero value counts
    uint8_t suppressed;   // any nonzero value counts
};
static_assert(offsetof(KeyStateNode, is_nil) == 0x19 && offsetof(KeyStateNode, key) == 0x1c);
static_assert(offsetof(KeyStateNode, enabled) == 0x20 && offsetof(KeyStateNode, suppressed) == 0x21);

struct KeyStateMap {
    uint8_t unknown_0x00[0x8];
    KeyStateNode *head;
};
static_assert(offsetof(KeyStateMap, head) == 0x8);

// The key's node, or the head when the map has none (std::map::find).
KeyStateNode *find_key_state(const KeyStateMap *map, uint32_t key)
{
    KeyStateNode *head = map->head;
    KeyStateNode *found = head;
    for (KeyStateNode *node = head->parent; !node->is_nil;) {
        if (node->key < key) {
            node = node->right;
        } else {
            found = node;
            node = node->left;
        }
    }
    return found != head && !(key < found->key) ? found : head;
}

// The input's sampled state: an analog value and a pressed bit per button index.
constexpr uint32_t bits_per_word = 32;
struct InputState {
    uint8_t unknown_0x00[0x28];
    const float *analog_values;
    uint8_t unknown_0x30[0x28];
    const uint32_t *pressed_bits;
};
static_assert(offsetof(InputState, analog_values) == 0x28 && offsetof(InputState, pressed_bits) == 0x58);

// The set of held inputs that 0x013994a0 searches: a std::set whose tree it reaches at +0x18.
struct HeldInputSet {
    uint8_t unknown_0x00[0x18];
    void *tree;
};

struct InputSource {
    uint8_t unknown_0x00[0x8];
    const InputState *state;
    uint8_t unknown_0x10[0x28];
    HeldInputSet held_inputs;
};
static_assert(offsetof(InputSource, held_inputs) == 0x38);

struct KeyMapper;

struct KeyAssign {
    uint8_t unknown_0x00[0x10];
    InputSource *input;
    uint8_t unknown_0x18[0x8];
    uint8_t enabled;   // any nonzero value counts
    uint8_t unknown_0x21[0x7];
    KeyMapper *mapper;
    uint8_t unknown_0x30[0x10];
    const KeyStateMap *key_states;
};
static_assert(offsetof(KeyAssign, input) == 0x10 && offsetof(KeyAssign, enabled) == 0x20);
static_assert(offsetof(KeyAssign, mapper) == 0x28 && offsetof(KeyAssign, key_states) == 0x40);

constexpr int32_t no_index = -1;

// The input's button index assigned to `key`, or no_index.
RT_ORIGINAL(0x01390ba0, mapper_button_index, int32_t(KeyMapper *mapper, uint32_t key));
// The second input a button index is assigned to (another device's), or no_index.
RT_ORIGINAL(0x0138d060, mapper_alternate_input, int32_t(KeyMapper *mapper, int32_t button));
// Whether `input` is in the set (std::set::find != end), in the low byte of the result.
RT_ORIGINAL(0x013994a0, input_set_contains, uint8_t(HeldInputSet *set, int32_t input));

// Whether `key`'s state entry exists and lets it be read: enabled, then not suppressed, each
// checked in a lookup of its own.
bool key_readable(const KeyAssign *keys, uint32_t key)
{
    const KeyStateNode *head = keys->key_states->head;
    const KeyStateNode *state = find_key_state(keys->key_states, key);
    if (state == head || state->enabled == 0)
        return false;
    state = find_key_state(keys->key_states, key);
    return state != head && state->suppressed == 0;
}

}  // namespace

// Whether logical key `key` is pressed: its button's bit in the input's pressed state, or else
// the alternate input the mapper names for that button being in the input's set. False when the
// key assign has no mapper, is disabled, or the key has no button or may not be read.
extern "C" uint8_t bb_input_key_assign_pressed_01391e30(KeyAssign *keys, uint32_t key)
{
    if (keys->mapper == nullptr)
        return false;
    const int32_t button = mapper_button_index(keys->mapper, key);
    if (button == no_index || keys->enabled == 0 || !key_readable(keys, key))
        return false;
    // The bit is picked from the 32-bit word with the index's low five bits.
    const uint32_t index = static_cast<uint32_t>(button);
    if ((keys->input->state->pressed_bits[index / bits_per_word] >> (index % bits_per_word)) & 1)
        return true;
    const int32_t alternate = mapper_alternate_input(keys->mapper, button);
    if (alternate == no_index)
        return false;
    return input_set_contains(&keys->input->held_inputs, alternate);
}

// The analog value of logical key `key` (a stick axis or a pressure), 0 when the key has no
// button, the key assign is disabled or the key may not be read. Unlike the pressed query it
// does not check for a mapper first.
extern "C" float bb_input_key_assign_analog_value_01391f60(KeyAssign *keys, uint32_t key)
{
    const int32_t button = mapper_button_index(keys->mapper, key);
    if (button == no_index || keys->enabled == 0 || !key_readable(keys, key))
        return 0.0f;
    return keys->input->state->analog_values[button];
}

namespace {

// An assignment in one of the assignment tables; only its id is read here.
struct Assignment {
    uint8_t unknown_0x00[0x24];
    uint32_t id;   // the original reads it through a leaf getter (a family member)
};
static_assert(offsetof(Assignment, id) == 0x24);

using AssignmentNode = engine::TreeNode<uint32_t, Assignment *>;
static_assert(offsetof(AssignmentNode, value) == 0x28);

struct AssignmentMap {
    uint8_t unknown_0x00[0x8];
    AssignmentNode *head;
};
static_assert(offsetof(AssignmentMap, head) == 0x8);

struct AssignmentTables {
    uint8_t unknown_0x00[0x38];
    AssignmentMap *tables[1];   // indexed by the caller's table number, unchecked
};
static_assert(offsetof(AssignmentTables, tables) == 0x38);

constexpr uint32_t id_count = 8;   // ids 0 to 7
constexpr uint32_t no_free_id = 0xffffffff;

bool id_in_use(const AssignmentMap *map, uint32_t id)
{
    // Every entry is visited; the walk does not stop at the first match.
    bool used = false;
    for (AssignmentNode *node = map->head->left; node != map->head; node = engine::tree_next(node)) {
        if (node->value->id == id)
            used = true;
    }
    return used;
}

}  // namespace

// The smallest id from 0 to 7 that no assignment of table `table` has, or 0xffffffff when all
// eight are taken.
extern "C" uint32_t bb_input_key_assign_first_free_id_013983e0(AssignmentTables *owner, uint32_t table)
{
    const AssignmentMap *map = owner->tables[table];
    for (uint32_t id = 0; id < id_count; ++id) {
        if (!id_in_use(map, id))
            return id;
    }
    return no_free_id;
}
