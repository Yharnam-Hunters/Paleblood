// SPDX-License-Identifier: GPL-2.0-or-later
// Acquire a presentation buffer, append its previous record to the block chain,
// then submit the saved command snapshot. Allocation failure declines capacity.
#pragma once

#include "byte_field.h"

#include "present_command_finish.h"

namespace render {
struct ManagedCapacityProcessorView {
    uint8_t unknown_0x00[0x08];
    Field<uint64_t> slots;
};
static_assert(offsetof(ManagedCapacityProcessorView, slots) == 0x08);
static_assert(sizeof(ManagedCapacityProcessorView) == 0x10);

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
RT_ORIGINAL(0x02ad3d40, capture_managed_capacity_record,
            uint64_t(void *processor, uintptr_t begin));
RT_ORIGINAL(0x02ad3b80, pop_managed_capacity_buffer,
            uint32_t(void *processor, uintptr_t *acquired_buffer));
// Retry while the buffer pool is empty; this caller ignores the step's result.
RT_ORIGINAL(0x0247f970, managed_capacity_retry_step, void());
}  // namespace managed_capacity_calls
}  // namespace render
