// SPDX-License-Identifier: GPL-2.0-or-later
// Storage and call boundaries observed by the linked YEBIS interface cleanup.
#pragma once

#include "yebis_interface.h"
#include "runtime/original.h"

namespace yebis {

// This is a minimum view; the full original class is unknown.
struct LinkedInterfaceStorage {
    InterfaceStorage interface;
    InterfaceStorage *other_interface;
};
static_assert(offsetof(LinkedInterfaceStorage, interface) == 0);
static_assert(offsetof(LinkedInterfaceStorage, other_interface) == 0x18);
static_assert(sizeof(LinkedInterfaceStorage) == 0x20);

// Referenced storage uses a different slot from InterfaceVtablePrefix.cleanup.
// Only its vptr and count are accessed; the boundary pointer does not prove
// another instance member or a complete class size.
struct ReferencedInterfaceVtablePrefix {
    void *unknown_slots[4];
    void (*on_last_reference)(InterfaceStorage *);
};
static_assert(offsetof(ReferencedInterfaceVtablePrefix, on_last_reference) == 0x20);
static_assert(sizeof(ReferencedInterfaceVtablePrefix) == 0x28);

RT_GLOBAL(0x056b3670, linked_interface_vtable, const InterfaceVtablePrefix);
RT_ORIGINAL(0x00fb6da0, destruct_interface, void(InterfaceStorage *));

}  // namespace yebis
