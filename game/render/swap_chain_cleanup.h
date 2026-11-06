// SPDX-License-Identifier: GPL-2.0-or-later
// Two swap-chain cleanup records share the owner/resource prefix. Cleanup calls
// the owner's virtual method, then clears only the resource field.
#pragma once

#include "byte_field.h"

#include <cstddef>
#include <cstdint>

namespace render {

struct SwapChainCleanupOwner;
using SwapChainCleanupCallback = void (*)(SwapChainCleanupOwner *, uintptr_t);

struct SwapChainCleanupVtable {
    uint8_t unknown_00[0x70];
    Field<SwapChainCleanupCallback> callback;
};
struct SwapChainCleanupOwner {
    Field<SwapChainCleanupVtable *> vtable;
};
struct SwapChainCleanupRecord {
    Field<SwapChainCleanupOwner *> owner;
    Field<uintptr_t> resource;
};

static_assert(sizeof(uintptr_t) == 8);
static_assert(sizeof(SwapChainCleanupCallback) == 8);
static_assert(offsetof(SwapChainCleanupVtable, callback) == 0x70);
static_assert(offsetof(SwapChainCleanupOwner, vtable) == 0);
static_assert(offsetof(SwapChainCleanupRecord, owner) == 0);
static_assert(offsetof(SwapChainCleanupRecord, resource) == 8);
static_assert(sizeof(SwapChainCleanupRecord) == 0x10);

inline void clear_swap_chain_cleanup_record(SwapChainCleanupRecord *record)
{
    SwapChainCleanupOwner *owner = record->owner.get();
    if (owner == nullptr) return;
    const uintptr_t resource = record->resource.get();
    if (resource == 0) return;
    SwapChainCleanupVtable *vtable = owner->vtable.get();
    const SwapChainCleanupCallback callback = vtable->callback.get();
    callback(owner, resource);
    // The callback may replace either field. Only resource is cleared afterward.
    record->resource.set(0);
}
}  // namespace render
