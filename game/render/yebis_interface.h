// SPDX-License-Identifier: GPL-2.0-or-later
// Known IPfxInterface prefix shared by derived YEBIS objects.
#pragma once

#include <cstddef>
#include <cstdint>

namespace yebis {

// This is the initialized prefix; derived objects have different storage sizes.
struct InterfaceStorage {
    const void *vtable;
    uint32_t reference_count;
    std::byte unknown_0x000c[4];
    void *instance;
};
static_assert(offsetof(InterfaceStorage, vtable) == 0);
static_assert(offsetof(InterfaceStorage, reference_count) == 0x08);
static_assert(offsetof(InterfaceStorage, instance) == 0x10);
static_assert(sizeof(InterfaceStorage) == 0x18);

}  // namespace yebis
