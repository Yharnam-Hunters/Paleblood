// SPDX-License-Identifier: GPL-2.0-or-later
// Character: the handles of the game's item instances (the game's SprjGaitem, going by the
// source file and singleton names its code reports).
//
// An item handle is a 32-bit word. Zero is no item. A handle that names a slot of the item
// manager has bit 23 set and its slot index in the low 16 bits (0xffff: no slot); the slot is
// live while it holds that same handle, so a stale handle (its slot reused) names nothing. Each
// slot counts the handles that refer to it: copying a handle counts one more, releasing one counts
// one less, and the last release frees the item.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"

namespace {

constexpr uint32_t managed_bit = 0x800000;
constexpr uint32_t slot_index_mask = 0xffff;
constexpr uint32_t no_slot = 0xffff;
// The manager's table of slots in use, and the index table's range.
constexpr uint32_t slot_capacity = 0x1000;

struct Slot {
    uint32_t handle;      // the handle of the item in the slot
    int32_t references;   // handles that refer to it
};
static_assert(sizeof(Slot) == 8);

struct GaitemManager {
    uint8_t unknown_0x0000[0x8008];
    Slot slots[slot_capacity];
    uint8_t unknown_0x10008[0x14028 - 0x10008];
    const int32_t *slot_of_index;  // slot index for each handle index
    bool busy;                     // must be clear to resolve a handle
    bool ready;                    // must be set to resolve a handle
};
static_assert(offsetof(GaitemManager, slots) == 0x8008);
static_assert(offsetof(GaitemManager, slot_of_index) == 0x14028);
static_assert(offsetof(GaitemManager, busy) == 0x14030 && offsetof(GaitemManager, ready) == 0x14031);

RT_GLOBAL(0x0593e990, gaitem_manager, GaitemManager *);
RT_GLOBAL(0x04d35616, gaitem_manager_name, const char);

// Frees the item of a handle whose last reference was released.
RT_ORIGINAL(0x01e86e60, free_item, void(GaitemManager *manager, uint32_t handle));

// The resolve checks' reports (the item source file and the empty message).
RT_GLOBAL(0x04d3309c, gaitem_source_file, const char);
RT_GLOBAL(0x04b388a8, empty_message, const char);
constexpr int32_t missing_index_table_line = 0x3c9;
constexpr int32_t busy_line = 0x3ca;
constexpr int32_t not_ready_line = 0x3cb;

GaitemManager *manager()
{
    return engine::require(gaitem_manager, gaitem_manager_name.address());
}

// The live slot `handle` names, or null. The handle is nonzero.
Slot *live_slot(GaitemManager *items, uint32_t handle)
{
    const uint32_t index = handle & slot_index_mask;
    if (!(handle & managed_bit) || index == no_slot)
        return nullptr;
    // The index is not checked against the slot table: every index below 0xffff is used.
    Slot *slot = items->slots + index;
    return slot->handle == handle ? slot : nullptr;
}

void add_reference(uint32_t handle)
{
    if (handle == 0)
        return;
    if (Slot *slot = live_slot(manager(), handle))
        ++slot->references;
}

void release(uint32_t handle)
{
    if (handle == 0)
        return;
    GaitemManager *items = manager();
    Slot *slot = live_slot(items, handle);
    if (!slot)
        return;
    if (slot->references > 1) {
        --slot->references;
        return;
    }
    // The last reference (a count already at zero or below is treated the same).
    slot->references = 0;
    free_item(items, handle);
}

void report(int32_t line)
{
    engine::fatal_error(gaitem_source_file.address(), line, empty_message.address());
}

}  // namespace

// Copy-constructs `target` from `source`: one more reference on the item's slot. The game has two
// copies of this code.
extern "C" void bb_character_gaitem_handle_copy_01e8bae0(uint32_t *target, const uint32_t *source)
{
    *target = *source;
    add_reference(*target);
}

extern "C" void bb_character_gaitem_handle_copy_01e8bb70(uint32_t *target, const uint32_t *source)
{
    *target = *source;
    add_reference(*target);
}

// Destroys a handle: one reference less on its slot; releasing the last one frees the item.
extern "C" void bb_character_gaitem_handle_release_01e8bc00(const uint32_t *handle)
{
    release(*handle);
}

// Move-assigns `source` to `target`: the old handle is released, `target` takes `source`'s handle
// and `source` is cleared. Nothing happens when both hold the same handle.
extern "C" uint32_t *bb_character_gaitem_handle_move_assign_01e8bcb0(uint32_t *target, uint32_t *source)
{
    if (*target == *source)
        return target;
    release(*target);
    *target = *source;
    *source = 0;
    return target;
}

// Writes to `out` the handle that `handle` stands for: a managed handle goes through the
// manager's index table to the handle in that slot (zero when its index or the table's slot is
// out of range); any other handle is written as it is. The manager must have its index table, be
// ready and not busy; each failed check is reported, and the handle resolved all the same.
extern "C" uint32_t *bb_character_gaitem_resolve_handle_01e88da0(uint32_t *out, const GaitemManager *items,
                                                                 const uint32_t *handle)
{
    if (!items->slot_of_index)
        report(missing_index_table_line);
    if (items->busy)
        report(busy_line);
    if (!items->ready)
        report(not_ready_line);

    *out = *handle;
    if (!(*out & managed_bit))
        return out;
    *out = 0;
    // An index without a slot (0xffff) is out of range too.
    const uint32_t index = *handle & slot_index_mask;
    if (index >= slot_capacity)
        return out;
    // A negative table entry is out of range too (compared as an unsigned 64-bit number).
    const uint64_t slot = static_cast<uint64_t>(static_cast<int64_t>(items->slot_of_index[index]));
    if (slot < slot_capacity)
        *out = items->slots[slot].handle;
    return out;
}
