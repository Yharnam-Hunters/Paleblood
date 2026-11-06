// SPDX-License-Identifier: GPL-2.0-or-later
// Allocate and register swap-chain buffers, cached resources and tone curves.
// Called by the outer setup function at 0x02ad5a80; lower allocators remain original.
#pragma once

#include "byte_field.h"

#include "runtime/original.h"

namespace render {
struct SetupObject;
struct SetupServices;
struct SetupResourceSlot;
struct SetupOwner;
using SetupAllocate = uintptr_t (*)(SetupOwner *, uint64_t);
// The storage allocator also receives the alignment and the pre-rounded byte sum;
// whether it uses the fourth argument is not established.
using SetupAllocate58 = uintptr_t (*)(SetupOwner *, uint64_t, uint64_t, uint64_t);
using SetupRelease = void (*)(SetupOwner *, uintptr_t);
struct SetupOwnerVtable {
    uint8_t unknown_0x00[0x50];
    Field<SetupAllocate> allocate_50;
    Field<SetupAllocate58> allocate_58;
    uint8_t unknown_0x60[0x10];
    Field<SetupRelease> release_70;
};
struct SetupOwner { Field<SetupOwnerVtable *> vtable; };
static_assert(offsetof(SetupOwnerVtable, allocate_50) == 0x50);
static_assert(offsetof(SetupOwnerVtable, allocate_58) == 0x58);
static_assert(offsetof(SetupOwnerVtable, release_70) == 0x70);
struct SetupObjectSlot { Field<SetupObject *> value; };

struct SetupRecord {
    static constexpr auto payload_qword_count = 5;
    Field<uint64_t> words[payload_qword_count];
    Field<uint32_t> tail_28;
};
static_assert(offsetof(SetupRecord, tail_28) == 0x28);
static_assert(sizeof(SetupRecord) == 0x2c);

struct SetupInfo {
    Field<uint32_t> width;
    Field<uint32_t> height;
    uint8_t unknown_0x08[8];
    Field<uint32_t> format_selector;
    uint8_t unknown_0x14[8];
    Field<uint32_t> first_constructor_parameter;
    Field<uint32_t> second_constructor_parameter;
    Field<uint32_t> resource_options;
    Field<uint32_t> buffer_count;
    uint8_t unknown_0x2c[0x10];
    Field<uint32_t> resource_selector_override;
    Field<uint32_t> resource_secondary_flags;
    uint8_t unknown_0x44[4];
};
static_assert(offsetof(SetupInfo, format_selector) == 0x10);
static_assert(offsetof(SetupInfo, first_constructor_parameter) == 0x1c);
static_assert(offsetof(SetupInfo, second_constructor_parameter) == 0x20);
static_assert(offsetof(SetupInfo, resource_options) == 0x24);
static_assert(offsetof(SetupInfo, buffer_count) == 0x28);
static_assert(offsetof(SetupInfo, resource_selector_override) == 0x3c);
static_assert(offsetof(SetupInfo, resource_secondary_flags) == 0x40);
static_assert(sizeof(SetupInfo) == 0x48);

struct SetupCachedRecord {
    Field<SetupOwner *> owner;
    Field<uintptr_t> storage;
    Field<uint32_t> storage_mode;
    Field<uint32_t> required_bytes;
    uint8_t unknown_0x18[8];
    Field<uint8_t> record_kind;
    Field<uint8_t> compatibility_kind;
    Field<uint8_t> selection;
    Field<uint8_t> resource_flags;  // Packed descriptor control flags.
    Field<uint32_t> secondary_flags;
    Field<uint32_t> classification;
    Field<uint16_t> width;
    Field<uint16_t> height;
    Field<uint16_t> entries_per_level;
    Field<uint8_t> level_count;
    Field<uint8_t> selector;
    Field<uint16_t> first_constructor_parameter;
    Field<uint16_t> second_constructor_parameter;
};
static_assert(offsetof(SetupCachedRecord, storage) == 8);
static_assert(offsetof(SetupCachedRecord, storage_mode) == 0x10);
static_assert(offsetof(SetupCachedRecord, required_bytes) == 0x14);
static_assert(offsetof(SetupCachedRecord, record_kind) == 0x20);
static_assert(offsetof(SetupCachedRecord, secondary_flags) == 0x24);
static_assert(offsetof(SetupCachedRecord, classification) == 0x28);
static_assert(offsetof(SetupCachedRecord, width) == 0x2c);
static_assert(offsetof(SetupCachedRecord, second_constructor_parameter) == 0x36);
static_assert(sizeof(SetupCachedRecord) == 0x38);

constexpr std::size_t setup_curve_entries = 0x401;
struct SetupCurveOutput {
    uint8_t unknown_0x00[0x38];
    static constexpr auto curve_channel_count = 3;
    Field<float> triplets[setup_curve_entries][curve_channel_count];
};
static_assert(offsetof(SetupCurveOutput, triplets) == 0x38);
struct SetupState {
    static constexpr auto record_prefix_bytes = 0x2c0;
    uint8_t record_storage[record_prefix_bytes];
    Field<SetupOwner *> manager_2c0;
    Field<SetupOwner *> allocator_2c8;
    Field<uintptr_t> storage_2d0;
    Field<uint64_t> service_2d8;
    Field<uint64_t> zero_2e0;
    Field<uintptr_t> last_2e8;
    Field<int32_t> video_handle;
    uint8_t unknown_0x2f4[4];
    Field<uint64_t> event_queue;
    static constexpr auto info_copy_qwords = 9;
    Field<uint64_t> info_copy[info_copy_qwords];
    SetupCachedRecord cached_record;
    SetupCurveOutput curve;
};
static_assert(offsetof(SetupState, manager_2c0) == 0x2c0);
static_assert(offsetof(SetupState, allocator_2c8) == 0x2c8);
static_assert(offsetof(SetupState, storage_2d0) == 0x2d0);
static_assert(offsetof(SetupState, service_2d8) == 0x2d8);
static_assert(offsetof(SetupState, zero_2e0) == 0x2e0);
static_assert(offsetof(SetupState, last_2e8) == 0x2e8);
static_assert(offsetof(SetupState, video_handle) == 0x2f0);
static_assert(offsetof(SetupState, event_queue) == 0x2f8);
static_assert(offsetof(SetupState, info_copy) == 0x300);
static_assert(offsetof(SetupState, cached_record) == 0x348);
static_assert(offsetof(SetupState, curve) == 0x380);

// VideoOut initializes the six public fields and its 16 reserved bytes.
// The setup scratch reserves four additional bytes after that public attribute.
struct SetupVideoAttribute {
    Field<uint32_t> pixel_format;
    Field<uint32_t> tiling_mode;
    Field<uint32_t> aspect_ratio;
    Field<uint32_t> width;
    Field<uint32_t> height;
    Field<uint32_t> pixel_pitch;
    static constexpr std::size_t reserved_bytes = 16;
    uint8_t reserved[reserved_bytes];
    uint8_t unknown_0x28[4];
};
static_assert(offsetof(SetupVideoAttribute, pixel_format) == 0);
static_assert(offsetof(SetupVideoAttribute, tiling_mode) == 4);
static_assert(offsetof(SetupVideoAttribute, aspect_ratio) == 8);
static_assert(offsetof(SetupVideoAttribute, width) == 0xc);
static_assert(offsetof(SetupVideoAttribute, height) == 0x10);
static_assert(offsetof(SetupVideoAttribute, pixel_pitch) == 0x14);
static_assert(offsetof(SetupVideoAttribute, reserved) == 0x18);
static_assert(sizeof(SetupVideoAttribute) == 0x2c);
struct SetupParameters {
    Field<uint32_t> width;
    Field<uint32_t> height;
    Field<uint32_t> level_count;
    Field<uint32_t> entries_per_level;
    Field<uint32_t> selector;
    Field<uint32_t> count_mode;
    Field<uint32_t> layout_parameter;
    Field<uint32_t> compatibility_kind;
    Field<uint32_t> flags;
    Field<uint32_t> selection;
    Field<uint32_t> secondary_flags;
    Field<uint32_t> trailing_parameter;
};
struct SetupChannel {
    Field<uintptr_t> data;
    Field<uint32_t> bytes;
    Field<uint32_t> zero;
};
static_assert(offsetof(SetupParameters, width) == 0x0);
static_assert(offsetof(SetupParameters, height) == 0x4);
static_assert(offsetof(SetupParameters, level_count) == 0x8);
static_assert(offsetof(SetupParameters, entries_per_level) == 0xc);
static_assert(offsetof(SetupParameters, selector) == 0x10);
static_assert(offsetof(SetupParameters, count_mode) == 0x14);
static_assert(offsetof(SetupParameters, layout_parameter) == 0x18);
static_assert(offsetof(SetupParameters, compatibility_kind) == 0x1c);
static_assert(offsetof(SetupParameters, flags) == 0x20);
static_assert(offsetof(SetupParameters, selection) == 0x24);
static_assert(offsetof(SetupParameters, secondary_flags) == 0x28);
static_assert(offsetof(SetupParameters, trailing_parameter) == 0x2c);
static_assert(sizeof(SetupParameters) == 0x30);
static_assert(sizeof(SetupChannel) == 0x10);
struct SetupTemporaryAllocator {
    Field<const void *> vtable;
    Field<uintptr_t> token;
};
struct SetupTemporaryVtable {
    uint8_t unknown_0x00[0x10];
    Field<const void *> first_virtual_function;
};
static_assert(offsetof(SetupTemporaryVtable, first_virtual_function) == 0x10);
struct SetupAuxiliarySettings {
    static constexpr auto settings_bytes = 0x20;
    uint8_t bytes[settings_bytes];
};
struct SetupQueueName {
    static constexpr auto queue_name_bytes = 0xf;
    char bytes[queue_name_bytes];
};

namespace swap_setup_calls {
// Object creation receives the two scalar choices, selected owner, services,
// setup information and candidate owner in this order.
RT_ORIGINAL(0x0247c4e0, create_object,
            SetupObject *(uint32_t, uint32_t, SetupOwner *, SetupServices *, SetupInfo *, SetupOwner *));
RT_ORIGINAL(0x02adbcb0, initialize_object, void(SetupObject *, void *));
RT_ORIGINAL(0x02adbcc0, destroy_object, void(SetupObject *));
RT_ORIGINAL(0x02ab7310, map_selector, uint32_t(Field<uint32_t> *, uint32_t));
RT_ORIGINAL(0x0149cb30, resolve_unit,
            uint32_t(Field<uint32_t> *, uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x02abb4a0, note_unit_failure, void(uint32_t));
RT_ORIGINAL(0x02ab7a20, first_parameter, uint32_t(uint32_t, uint32_t));
RT_ORIGINAL(0x02ab7ab0, second_parameter, uint32_t(uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x01485f90, construct_record,
            uint64_t(SetupRecord *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     uint32_t, uint32_t, uint64_t, uint64_t));
RT_ORIGINAL(0x02abadd0, storage_admitted, uint8_t(uintptr_t));
RT_ORIGINAL(0x02fbe688, fill_storage, void *(uintptr_t, uint32_t, uint64_t));
RT_ORIGINAL(0x02abaeb0, encode_address, uint32_t(uintptr_t));
RT_ORIGINAL(0x01486c50, set_record_address, void(SetupRecord *, uintptr_t));
RT_ORIGINAL(0x01486ca0, get_record_address, uint32_t(SetupRecord *));
RT_ORIGINAL(0x025676b0, storage_service, uint64_t(SetupServices *));
RT_ORIGINAL(0x02571c60, clear_curve, void(SetupCurveOutput *));
RT_ORIGINAL(0x02572290, clear_cached_record, void(SetupCachedRecord *));
RT_ORIGINAL(0x02fbf568, video_open, int32_t(uint32_t, uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x02fbf578, video_flip_rate, int32_t(int32_t, uint32_t));
RT_ORIGINAL(0x02fbf588, create_event_queue,
            int32_t(Field<uint64_t> *, const char *));
RT_ORIGINAL(0x02fbf598, add_flip_event, int32_t(uint64_t, int32_t, uintptr_t));
RT_ORIGINAL(0x02fbf5a8, set_video_attribute,
            void(SetupVideoAttribute *, uint32_t, uint32_t, uint32_t,
                 uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x02fbf5b8, delete_event_queue, void(uint64_t));
RT_ORIGINAL(0x02fbf5c8, video_close, void(int32_t));
RT_ORIGINAL(0x02fbf5d8, register_video_buffers,
            int32_t(int32_t, uint32_t, const void *, uint32_t, SetupVideoAttribute *));
// RSI is a borrowed output-QWORD slot; RCX is a full optional array pointer.
// This setup call supplies nullptr for the optional array.
RT_ORIGINAL(0x02565f90, initialize_cached_record,
            uint64_t(SetupServices *, SetupResourceSlot *, SetupParameters *, void *,
                     SetupOwner *, SetupOwner *, uint64_t));
RT_ORIGINAL(0x02abaef0, current_storage_mode, uint32_t());
RT_ORIGINAL(0x0247d5b0, capture_upload_token, uintptr_t());
RT_ORIGINAL(0x0247d4b0, allocate_upload, uintptr_t(SetupTemporaryAllocator *, uint64_t));
RT_ORIGINAL(0x02567890, curve_service, uintptr_t(SetupServices *));
RT_ORIGINAL(0x02571790, initialize_curve,
            uint32_t(SetupCurveOutput *, uintptr_t, const SetupAuxiliarySettings *,
                     SetupChannel *, SetupOwner *, SetupOwner *, uint64_t));
RT_ORIGINAL(0x0247d5f0, release_upload_token, void(uintptr_t));
RT_GLOBAL(0x04c707dc, queue_name, SetupQueueName);
RT_GLOBAL(0x04c707d8, curve_multiplier, float);
RT_GLOBAL(0x04c707ec, curve_settings, SetupAuxiliarySettings);
RT_GLOBAL(0x056b49e0, temporary_vtable, SetupTemporaryVtable);
}  // namespace swap_setup_calls
}  // namespace render
