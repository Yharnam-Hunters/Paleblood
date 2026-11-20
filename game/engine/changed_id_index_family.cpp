// SPDX-License-Identifier: GPL-2.0-or-later
// selects_index_of_changed_id_with_0x01348fe0 (symbols/families.csv): the copies of a refresh.
// When the object follows an id (a pointer at +0x28) whose value differs from the one it last
// saw (+0x20), it keeps the new value, looks for the first entry of a table (24-byte entries,
// the id at +0x10) that has it, and hands that entry's index (-1 when none has it) to
// 0x01348fe0 with the target its owner keeps at +0x110 and 0.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct IdEntry {
    uint8_t unknown_0x00[0x10];
    int32_t id;
    uint8_t unknown_0x14[0x4];
};
static_assert(offsetof(IdEntry, id) == 0x10 && sizeof(IdEntry) == 0x18);

struct IdTable {
    uint8_t unknown_0x00[0x18];
    IdEntry *begin;
    IdEntry *end;
};
static_assert(offsetof(IdTable, begin) == 0x18 && offsetof(IdTable, end) == 0x20);

struct SelectionTarget;
struct SelectionOwner {
    uint8_t unknown_0x000[0x110];
    SelectionTarget *target;
};
static_assert(offsetof(SelectionOwner, target) == 0x110);

struct IdFollower {
    uint8_t unknown_0x00[0x18];
    SelectionOwner *owner;
    int32_t seen_id;
    uint8_t unknown_0x24[0x4];
    const int32_t *followed_id;
    IdTable *table;
};
static_assert(offsetof(IdFollower, owner) == 0x18 && offsetof(IdFollower, seen_id) == 0x20);
static_assert(offsetof(IdFollower, followed_id) == 0x28 && offsetof(IdFollower, table) == 0x30);

RT_ORIGINAL(0x01348fe0, apply_index, void(SelectionTarget *, uint32_t index, uint32_t option));

constexpr uint32_t no_index = 0xffffffff;

}  // namespace

extern "C" void bb_engine_selects_index_of_changed_id_with_0x01348fe0(IdFollower *follower)
{
    const int32_t *followed = follower->followed_id;
    if (!followed)
        return;
    const int32_t id = *followed;
    if (id == follower->seen_id)
        return;
    follower->seen_id = id;
    const IdTable *table = follower->table;
    // The entry count as the original computes it: 32 bits of the entry difference.
    const auto count = static_cast<uint32_t>(table->end - table->begin);
    uint32_t index = no_index;
    for (uint32_t i = 0; i < count; ++i) {
        if (table->begin[i].id == id) {
            index = i;
            break;
        }
    }
    apply_index(follower->owner->target, index, 0);
}
