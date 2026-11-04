// SPDX-License-Identifier: GPL-2.0-or-later
// Release the owner's storage; the only known caller is 0x02713870.
#include "owner_storage.h"

extern "C" void bb_engine_release_owner_storage(engine::storage::StorageOwner *owner)
{
    if (owner->storage) {
        owner->allocator->free(owner->storage);
        owner->storage = nullptr;
    }
}
