// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize and allocate a swap-chain resource after validating its parameters.
// Swap-chain setup calls this function; lower resource preparation supplies the descriptor.
#pragma once

#include "byte_field.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace render {

struct SwapResourceOwner;
struct SwapResourceObject;
struct ResourceInfoOptions;
using SwapResourceAllocate = SwapResourceObject *(*)(SwapResourceOwner *, uint64_t, uint64_t);
using SwapResourceRelease = uintptr_t (*)(SwapResourceOwner *, uintptr_t);
struct SwapResourceOwnerVtable {
    uint8_t unknown_00[0x58];
    Field<SwapResourceAllocate> allocate;
    uint8_t unknown_60[0x10];
    Field<SwapResourceRelease> release;
};
struct SwapResourceOwner {
    Field<SwapResourceOwnerVtable *> vtable;
};

// All three capability words must be nonzero; their individual features are unknown.
struct SwapResourceServices {
    Field<uint64_t> first_capability;
    Field<uint64_t> second_capability;
    uint8_t unknown_10[0x10];
    Field<uint64_t> third_capability;
    uint8_t unknown_28[0xdc10 - 0x28];
    Field<SwapResourceOwner *> default_allocator;
    uint8_t unknown_dc18[8];
    Field<SwapResourceOwner *> default_owner;
};
struct SwapResourceParameters {
    Field<uint32_t> width;
    Field<uint32_t> height;
    Field<uint32_t> level_count;
    Field<uint32_t> entries_per_level;
    uint8_t unknown_10[4];
    Field<uint32_t> count_mode;
    uint8_t unknown_18[4];
    Field<uint32_t> kind;
    Field<uint32_t> flags;
    Field<uint32_t> selection;
    Field<uint32_t> secondary_flags;
};
struct SwapResourceInitialEntry {
    uint8_t unknown_00[8];
    Field<uint32_t> value;
    uint8_t unknown_0c[4];
};
struct SwapResourceInfo {
    Field<SwapResourceOwner *> owner;
    Field<uintptr_t> resource;
    Field<uint64_t> mode_and_required_bytes;
    Field<uint64_t> capacity_and_alignment;
    Field<uint64_t> kind_and_secondary_flags;
    Field<uint64_t> classification_and_dimensions;
    Field<uint64_t> levels_and_layout;
};
struct SwapResourceObject {
    Field<uintptr_t> vtable;
    Field<SwapResourceOwner *> allocator;
    // Opaque object state initialized to 1; other states are not modeled here.
    Field<uint32_t> state;
    uint8_t unknown_14[4];
    SwapResourceInfo info;
};
struct SwapResourceOutput {
    Field<SwapResourceObject *> object;
};

static_assert(sizeof(uintptr_t) == 8);
static_assert(sizeof(SwapResourceAllocate) == 8);
static_assert(sizeof(SwapResourceRelease) == 8);
static_assert(offsetof(SwapResourceOwnerVtable, allocate) == 0x58);
static_assert(offsetof(SwapResourceOwnerVtable, release) == 0x70);
static_assert(offsetof(SwapResourceOwner, vtable) == 0);
static_assert(offsetof(SwapResourceServices, second_capability) == 8);
static_assert(offsetof(SwapResourceServices, third_capability) == 0x20);
static_assert(offsetof(SwapResourceServices, default_allocator) == 0xdc10);
static_assert(offsetof(SwapResourceServices, default_owner) == 0xdc20);
static_assert(offsetof(SwapResourceParameters, count_mode) == 0x14);
static_assert(offsetof(SwapResourceParameters, kind) == 0x1c);
static_assert(offsetof(SwapResourceParameters, flags) == 0x20);
static_assert(offsetof(SwapResourceParameters, selection) == 0x24);
static_assert(offsetof(SwapResourceParameters, secondary_flags) == 0x28);
static_assert(sizeof(SwapResourceParameters) == 0x2c);
static_assert(offsetof(SwapResourceInitialEntry, value) == 8);
static_assert(sizeof(SwapResourceInitialEntry) == 0x10);
static_assert(offsetof(SwapResourceInfo, resource) == 8);
static_assert(offsetof(SwapResourceInfo, mode_and_required_bytes) == 0x10);
static_assert(offsetof(SwapResourceInfo, capacity_and_alignment) == 0x18);
static_assert(offsetof(SwapResourceInfo, kind_and_secondary_flags) == 0x20);
static_assert(offsetof(SwapResourceInfo, classification_and_dimensions) == 0x28);
static_assert(offsetof(SwapResourceInfo, levels_and_layout) == 0x30);
static_assert(sizeof(SwapResourceInfo) == 0x38);
static_assert(offsetof(SwapResourceObject, allocator) == 8);
static_assert(offsetof(SwapResourceObject, state) == 0x10);
static_assert(offsetof(SwapResourceObject, unknown_14) == 0x14);
static_assert(offsetof(SwapResourceObject, info) == 0x18);
static_assert(sizeof(SwapResourceObject) == 0x50);
static_assert(sizeof(SwapResourceOutput) == 8);

namespace swap_resource_calls {
RT_ORIGINAL(0x02571d60, prepare_resource_info,
            uint32_t(SwapResourceInfo *, SwapResourceServices *, SwapResourceParameters *,
                     SwapResourceInitialEntry *, SwapResourceOwner *, SwapResourceOwner *,
                     ResourceInfoOptions *));
RT_GLOBAL(0x057b6240, initial_vtable, uint8_t);
RT_GLOBAL(0x0575f3e0, final_vtable, uint8_t);
}  // namespace swap_resource_calls
}  // namespace render

// Initialize one swap-chain resource after compatibility checks, using the default
// or caller-supplied owner and allocator. Swap-chain setup calls this function.
// The result is a status zero-extended from 32 bits.
extern "C" uint32_t bb_render_initialize_swap_chain_resource(
    render::SwapResourceServices *, render::SwapResourceOutput *,
    render::SwapResourceParameters *, render::SwapResourceInitialEntry *,
    render::SwapResourceOwner *, render::SwapResourceOwner *, render::ResourceInfoOptions *);
