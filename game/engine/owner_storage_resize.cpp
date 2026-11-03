// SPDX-License-Identifier: GPL-2.0-or-later
// Resize the owner's storage; the only known caller is 0x02713870.
#include "owner_storage.h"

extern "C" void bb_engine_resize_owner_storage(engine::storage::StorageOwner *owner, uint32_t byte_count)
{
    if (owner->storage) {
        owner->allocator->free(owner->storage);
        owner->storage = nullptr;
    }
    // Reload the allocator after free: the callback may change the owner.
    if (byte_count) {
        owner->storage = owner->allocator->allocate(byte_count, engine::storage::storage_alignment);
    }
}
