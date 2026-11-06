// SPDX-License-Identifier: GPL-2.0-or-later
// Acquire a presentation buffer, append its previous record to the block chain,
// then submit the saved command snapshot. Allocation failure declines capacity.
#pragma once

#include "byte_field.h"

#include "present_command_finish.h"
#include "present_buffer_pool.h"

namespace render {
using ManagedCapacityAllocate = uintptr_t (*)(FinishBlockOwner *, uint64_t bytes,
                                             uint32_t alignment);
struct ManagedCapacityOwnerVtable {
    uint8_t unknown_0x00[0x58];
    Field<ManagedCapacityAllocate> allocate;
};
static_assert(offsetof(ManagedCapacityOwnerVtable, allocate) == 0x58);
struct ManagedCapacityOwnerView {
    Field<ManagedCapacityOwnerVtable *> vtable;
};
static_assert(offsetof(ManagedCapacityOwnerView, vtable) == 0x00);

namespace managed_capacity_calls {
// Retry while the buffer pool is empty; this caller ignores the step's result.
RT_ORIGINAL(0x0247f970, managed_capacity_retry_step, void());
}  // namespace managed_capacity_calls
}  // namespace render
