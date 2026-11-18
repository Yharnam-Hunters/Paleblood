// SPDX-License-Identifier: GPL-2.0-or-later
// Allocator dispatch view first used by the YEBIS storage free wrapper.
#pragma once

#include "yebis_context.h"
#include "runtime/original.h"

namespace yebis {

struct AllocatorFreeVtablePrefix {
    std::byte unknown_0x0000[0x20];
    void (*free_storage)(AllocatorInterface *allocator, void *allocation);
};
static_assert(offsetof(AllocatorFreeVtablePrefix, free_storage) == 0x20);
static_assert(sizeof(AllocatorFreeVtablePrefix) == 0x28);

RT_GLOBAL(0x05866700, allocation_count, uint32_t);

}  // namespace yebis
