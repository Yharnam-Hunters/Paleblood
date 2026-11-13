// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the swap-chain setup's object creation (swap_chain_setup.h `create_object`), a thunk
// into the selected owner's storage allocator (vtable slot 0x58, `allocate_58`).
// - render_setup_create_object_0247c4e0: calls owner->allocate_58(owner, first, second, first)
//   and returns its result. Like the original, it hands the allocator the remaining argument
//   registers as they were: the second value again as the fifth argument and the candidate owner
//   (the sixth) unchanged. The values keep all 64 bits of their registers.
#include <cstddef>
#include <cstdint>

#include "swap_chain_setup.h"

namespace {

using render::SetupOwner;

// The allocator slot, seen with every register the thunk passes on.
using FullAllocate = uintptr_t (*)(SetupOwner *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

}  // namespace

extern "C" uintptr_t bb_render_setup_create_object_0247c4e0(uint64_t first, uint64_t second, SetupOwner *owner,
                                                           uint64_t /*services*/, uint64_t /*info*/,
                                                           uint64_t candidate)
{
    const auto *vtable = reinterpret_cast<const uint8_t *>(owner->vtable.get());
    const uintptr_t slot = render::Field<uintptr_t>::read(vtable + offsetof(render::SetupOwnerVtable, allocate_58));
    const auto allocate = reinterpret_cast<FullAllocate>(slot);
    return allocate(owner, first, second, first, second, candidate);
}
