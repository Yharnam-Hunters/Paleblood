// SPDX-License-Identifier: GPL-2.0-or-later
// Views for allocating swap-chain resource information and preparing its descriptor.
// Owner and service callbacks provide storage and process the initial entries.
#pragma once

#include "byte_field.h"
#include "command_packets.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace render {
struct ResourceInfoOwner;
struct ResourceInfoControl;
// Partial processor view: packet storage plus the service pointer read by finalization.
// Later, unmodeled processor fields occupy the following service-object padding.
struct ResourceInfoProcessorServices;
struct ResourceInfoProcessor {
    PacketBuffer commands;
    uint8_t unknown_28[0x18];
    Field<ResourceInfoProcessorServices *> services;
};
static_assert(offsetof(ResourceInfoProcessor, services) == 0x40);
static_assert(sizeof(ResourceInfoProcessor) == 0x48);

// Normalization reads one optional DWORD through this embedded payload.
struct ResourceInfoSuppliedParameter { Field<uint32_t> value; };
using ResourceInfoAllocate = uintptr_t (*)(ResourceInfoOwner *, uint64_t, uint64_t);
using ResourceInfoRelease = uintptr_t (*)(ResourceInfoOwner *, uintptr_t);
using ResourceInfoBeginProcessing = uintptr_t (*)(ResourceInfoControl *, uint64_t);
using ResourceInfoEndProcessing = uintptr_t (*)(ResourceInfoControl *);
struct ResourceInfoOwnerVtable {
    uint8_t unknown_00[0x58];
    Field<ResourceInfoAllocate> allocate;
    uint8_t unknown_60[0x10];
    Field<ResourceInfoRelease> release;
};
struct ResourceInfoOwner { Field<ResourceInfoOwnerVtable *> vtable; };
struct ResourceInfoControlVtable {
    uint8_t unknown_00[0x18];
    Field<ResourceInfoBeginProcessing> begin_processing;
    uint8_t unknown_20[8];
    Field<ResourceInfoEndProcessing> end_processing;
};
struct ResourceInfoControl { Field<ResourceInfoControlVtable *> vtable; };
struct ResourceInfoServices {
    uint8_t unknown_00[0x40];
    ResourceInfoProcessor processor;
    uint8_t unknown_88[0x118 - 0x88];
    Field<ResourceInfoControl *> control;
};
struct ResourceInfoParameters {
    Field<uint32_t> width;
    Field<uint32_t> height;
    Field<uint32_t> level_count;
    Field<uint32_t> entries_per_level;
    Field<uint32_t> selector;
    Field<uint32_t> layout;
    Field<uint32_t> layout_parameter;
    Field<uint32_t> compatibility_kind;
    Field<uint32_t> flags;
    Field<uint32_t> selection;
    Field<uint32_t> secondary_flags;
};
struct ResourceInfoOptions {
    Field<uint8_t> capacity_enabled;
    Field<uint8_t> span_copy_enabled;
    Field<uint8_t> payload_enabled;
    Field<uint8_t> alternate_path;
    Field<uint32_t> capacity;
    Field<uint32_t> alignment;
    ResourceInfoSuppliedParameter payload;
};
struct ResourceInfoInitialEntry {
    Field<uintptr_t> data;
    Field<uint32_t> value;
    uint8_t unknown_0c[4];
};
struct ResourceInfoDescriptor {
    static constexpr auto storage_bytes = 0x20;
    uint8_t bytes[storage_bytes]; };
struct ResourceInfoLevelDescriptor {
    static constexpr auto storage_bytes = 0x30;
    uint8_t bytes[storage_bytes]; };
struct ResourceInfoRequirements {
    Field<uint32_t> bytes;
    Field<uint32_t> alignment;
};
struct ResourceInfoOutput {
    Field<ResourceInfoOwner *> owner;
    Field<uintptr_t> allocation;
    Field<uint32_t> mode;
    Field<uint32_t> required_bytes;
    Field<uint32_t> allocation_bytes;
    Field<uint32_t> alignment;
    Field<uint8_t> kind;
    Field<uint8_t> source_kind;
    Field<uint8_t> selection;
    Field<uint8_t> flags;
    Field<uint32_t> secondary_flags;
    Field<uint32_t> classification;
    Field<uint16_t> width;
    Field<uint16_t> height;
    Field<uint16_t> entries_per_level;
    Field<uint8_t> levels;
    Field<uint8_t> selector;
    Field<uint16_t> layout;
    Field<uint16_t> layout_parameter;
};

static_assert(sizeof(uintptr_t) == 8);
static_assert(offsetof(ResourceInfoOwnerVtable, allocate) == 0x58);
static_assert(offsetof(ResourceInfoOwnerVtable, release) == 0x70);
static_assert(offsetof(ResourceInfoControlVtable, begin_processing) == 0x18);
static_assert(offsetof(ResourceInfoControlVtable, end_processing) == 0x28);
static_assert(offsetof(ResourceInfoServices, processor) == 0x40);
static_assert(offsetof(ResourceInfoServices, control) == 0x118);
static_assert(offsetof(ResourceInfoParameters, width) == 0x0);
static_assert(offsetof(ResourceInfoParameters, height) == 0x4);
static_assert(offsetof(ResourceInfoParameters, level_count) == 0x8);
static_assert(offsetof(ResourceInfoParameters, entries_per_level) == 0xc);
static_assert(offsetof(ResourceInfoParameters, selector) == 0x10);
static_assert(offsetof(ResourceInfoParameters, layout) == 0x14);
static_assert(offsetof(ResourceInfoParameters, layout_parameter) == 0x18);
static_assert(offsetof(ResourceInfoParameters, compatibility_kind) == 0x1c);
static_assert(offsetof(ResourceInfoParameters, flags) == 0x20);
static_assert(offsetof(ResourceInfoParameters, selection) == 0x24);
static_assert(offsetof(ResourceInfoParameters, secondary_flags) == 0x28);
static_assert(sizeof(ResourceInfoParameters) == 0x2c);
static_assert(offsetof(ResourceInfoOptions, capacity) == 4);
static_assert(offsetof(ResourceInfoOptions, alignment) == 8);
static_assert(offsetof(ResourceInfoOptions, payload) == 0xc);
static_assert(sizeof(ResourceInfoSuppliedParameter) == 4);
static_assert(sizeof(ResourceInfoInitialEntry) == 0x10);
static_assert(sizeof(ResourceInfoRequirements) == 8);
static_assert(offsetof(ResourceInfoOutput, allocation) == 8);
static_assert(offsetof(ResourceInfoOutput, mode) == 0x10);
static_assert(offsetof(ResourceInfoOutput, required_bytes) == 0x14);
static_assert(offsetof(ResourceInfoOutput, allocation_bytes) == 0x18);
static_assert(offsetof(ResourceInfoOutput, alignment) == 0x1c);
static_assert(offsetof(ResourceInfoOutput, kind) == 0x20);
static_assert(offsetof(ResourceInfoOutput, source_kind) == 0x21);
static_assert(offsetof(ResourceInfoOutput, selection) == 0x22);
static_assert(offsetof(ResourceInfoOutput, flags) == 0x23);
static_assert(offsetof(ResourceInfoOutput, secondary_flags) == 0x24);
static_assert(offsetof(ResourceInfoOutput, classification) == 0x28);
static_assert(offsetof(ResourceInfoOutput, width) == 0x2c);
static_assert(offsetof(ResourceInfoOutput, height) == 0x2e);
static_assert(offsetof(ResourceInfoOutput, entries_per_level) == 0x30);
static_assert(offsetof(ResourceInfoOutput, layout) == 0x34);
static_assert(offsetof(ResourceInfoOutput, levels) == 0x32);
static_assert(offsetof(ResourceInfoOutput, layout_parameter) == 0x36);
static_assert(sizeof(ResourceInfoOutput) == 0x38);

namespace resource_info_calls {
// Descriptor preparation receives the second allocator candidate as its sixth argument.
RT_ORIGINAL(0x02fa6af0, prepare_descriptor,
            uint32_t(ResourceInfoRequirements *, ResourceInfoDescriptor *, ResourceInfoParameters *,
                     ResourceInfoSuppliedParameter *, ResourceInfoInitialEntry *, ResourceInfoOwner *));
RT_ORIGINAL(0x01489910, descriptor_levels, uint32_t(ResourceInfoDescriptor *));
RT_ORIGINAL(0x02abaef0, current_mode, uint32_t());
RT_ORIGINAL(0x014898f0, descriptor_classification, uint32_t(ResourceInfoDescriptor *));
RT_ORIGINAL(0x02abb4b0, needs_initial_copy, uint8_t(uint32_t));
RT_ORIGINAL(0x02a1a1b0, copy_initial_span, uintptr_t(uintptr_t, uintptr_t, uint64_t));
RT_ORIGINAL(0x02abadd0, storage_admitted, uint8_t(uintptr_t));
RT_ORIGINAL(0x02ab4230, process_initial_entry,
            uintptr_t(ResourceInfoProcessor *, ResourceInfoOutput *, ResourceInfoInitialEntry *,
                      uint64_t, uint64_t));
RT_ORIGINAL(0x02ab1930, finish_processing_first, uintptr_t(ResourceInfoProcessor *));
RT_ORIGINAL(0x02ab18c0, finish_processing_second, uintptr_t(ResourceInfoProcessor *));
RT_ORIGINAL(0x02abad70, alternate_copy_admitted, uint8_t(uintptr_t));
RT_ORIGINAL(0x014a2c40, calculate_level_offset,
            uint32_t(Field<uint64_t> *, void *, ResourceInfoDescriptor *, uint32_t, uint32_t));
RT_ORIGINAL(0x014a1e80, initialize_level_descriptor,
            uintptr_t(ResourceInfoLevelDescriptor *, ResourceInfoDescriptor *, uint32_t, uint32_t));
RT_ORIGINAL(0x014a3410, copy_initial_level,
            uint32_t(uintptr_t, uintptr_t, ResourceInfoLevelDescriptor *));
RT_ORIGINAL(0x02abb4a0, note_offset_failure, uintptr_t(uint32_t));
}  // namespace resource_info_calls
}  // namespace render

// The options pointer is an ordinary seventh argument. Its pointee may change
// during lower calls; no callback is given the address of the incoming slot.
extern "C" uint32_t bb_render_prepare_swap_chain_resource_info(
    render::ResourceInfoOutput *, render::ResourceInfoServices *, render::ResourceInfoParameters *,
    render::ResourceInfoInitialEntry *, render::ResourceInfoOwner *, render::ResourceInfoOwner *,
    render::ResourceInfoOptions *);
