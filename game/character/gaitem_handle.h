// SPDX-License-Identifier: GPL-2.0-or-later
// Character: the item manager's slot table and the reference counting of item handles (the
// game's SprjGaitem), for the files that work on handles. gaitem_handle.cpp describes a handle;
// it was written before this header and declares the same table for itself.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"

namespace character {

constexpr uint32_t gaitem_managed_bit = 0x800000;
constexpr uint32_t gaitem_slot_index_mask = 0xffff;
constexpr uint32_t gaitem_no_slot = 0xffff;
// The manager's table of slots in use, and the index table's range.
constexpr uint32_t gaitem_slot_capacity = 0x1000;

struct GaitemItem;

struct GaitemSlot {
    uint32_t handle;      // the handle of the item in the slot
    int32_t references;   // handles that refer to it
};
static_assert(sizeof(GaitemSlot) == 8);

struct GaitemManager {
    uint8_t unknown_0x0000[0x8];
    GaitemItem *items[gaitem_slot_capacity];      // the item in each slot, or null
    GaitemSlot slots[gaitem_slot_capacity];
    int32_t free_indices[gaitem_slot_capacity];   // a ring of freed slot indices
    uint8_t unknown_0x14008[0x4];
    int32_t free_tail;                            // the ring position written last
    uint8_t unknown_0x14010[0x14028 - 0x14010];
    const int32_t *slot_of_index;   // slot index for each handle index
    bool busy;                      // must be clear to resolve a handle
    bool ready;                     // must be set to resolve a handle
};
static_assert(offsetof(GaitemManager, items) == 0x8 && offsetof(GaitemManager, slots) == 0x8008);
static_assert(offsetof(GaitemManager, free_indices) == 0x10008 && offsetof(GaitemManager, free_tail) == 0x1400c);
static_assert(offsetof(GaitemManager, slot_of_index) == 0x14028);
static_assert(offsetof(GaitemManager, busy) == 0x14030 && offsetof(GaitemManager, ready) == 0x14031);

RT_GLOBAL(0x0593e990, gaitem_manager, GaitemManager *);
RT_GLOBAL(0x04d35616, gaitem_manager_name, const char);

// Frees the item of a handle whose last reference was released.
RT_ORIGINAL(0x01e86e60, gaitem_free_item, void(GaitemManager *manager, uint32_t handle));

// The item manager, which the handle code requires before it looks at a slot: a missing one is
// reported by name and read again.
inline GaitemManager *require_gaitem_manager()
{
    return engine::require(gaitem_manager, gaitem_manager_name.address());
}

// The live slot `handle` names, or null: the handle is managed, has a slot index, and the slot
// holds that same handle.
inline GaitemSlot *gaitem_live_slot(GaitemManager *items, uint32_t handle)
{
    const uint32_t index = handle & gaitem_slot_index_mask;
    if (!(handle & gaitem_managed_bit) || index == gaitem_no_slot)
        return nullptr;
    // The index is not checked against the slot table: every index below 0xffff is used.
    GaitemSlot *slot = items->slots + index;
    return slot->handle == handle ? slot : nullptr;
}

// One more reference on the live slot `handle` names, if it names one.
inline void gaitem_add_reference(GaitemManager *items, uint32_t handle)
{
    if (GaitemSlot *slot = gaitem_live_slot(items, handle))
        ++slot->references;
}

// One reference less on the live slot `handle` names, if it names one; the last one frees the
// item.
inline void gaitem_release_reference(GaitemManager *items, uint32_t handle)
{
    GaitemSlot *slot = gaitem_live_slot(items, handle);
    if (!slot)
        return;
    if (slot->references > 1) {
        --slot->references;
        return;
    }
    // The last reference (a count already at zero or below is treated the same).
    slot->references = 0;
    gaitem_free_item(items, handle);
}

}  // namespace character
