// SPDX-License-Identifier: GPL-2.0-or-later
// Known IPfxInterface prefix shared by derived YEBIS objects.
#pragma once

#include <cstddef>
#include <cstdint>

namespace yebis {

struct InterfaceStorage;

// The observed derived tables use a non-deleting cleanup at this slot.
struct InterfaceVtablePrefix {
    void *unknown_slots[3];
    void (*cleanup)(InterfaceStorage *);
};
static_assert(offsetof(InterfaceVtablePrefix, cleanup) == 0x18);
static_assert(sizeof(InterfaceVtablePrefix) == 0x20);

// This is the initialized prefix; derived objects have different storage sizes.
struct InterfaceStorage {
    const InterfaceVtablePrefix *vtable;
    uint32_t reference_count;
    std::byte unknown_0x000c[4];
    void *instance;
};
static_assert(offsetof(InterfaceStorage, vtable) == 0);
static_assert(offsetof(InterfaceStorage, reference_count) == 0x08);
static_assert(offsetof(InterfaceStorage, instance) == 0x10);
static_assert(sizeof(InterfaceStorage) == 0x18);

}  // namespace yebis
