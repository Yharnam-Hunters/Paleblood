// SPDX-License-Identifier: GPL-2.0-or-later
// Color-buffer and depth/stencil register blocks used by the preparation consumer.
// Byte-backed fields preserve unaligned input and overlap with packet output or the live cursor.
#pragma once

#include "command_reservation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace render {
struct PacketBuffer;
struct PresentRecordView;
struct RecordPreparationState;

RT_ORIGINAL(0x014738b0, render_set_render_target,
            void(PacketBuffer *, uint32_t slot, const void *descriptor));
RT_ORIGINAL(0x014739b0, render_set_depth_render_target,
            void(PacketBuffer *, const void *descriptor));

// Narrow views retain the exact validity and NOP-payload reads without
// widening them to the overlapping QWORD register pairs copied by the writers.
struct PreparationRecordPredicateSource {
    Field<uint32_t> word;
};
static_assert(offsetof(PreparationRecordPredicateSource, word) == 0x00);
static_assert(sizeof(PreparationRecordPredicateSource) == 0x04);

// Read the full color-buffer validity word, including noncanonical values.
// Byte-backed storage permits the original unaligned read.
inline uint32_t prepared_record_payload_word(const void *descriptor)
{
    return static_cast<const PreparationRecordPredicateSource *>(descriptor)->word.get();
}

struct PreparationSelectorPredicateSource {
    uint8_t unknown_0x00[0x08];
    Field<uint32_t> first;
    Field<uint32_t> second;
};
static_assert(offsetof(PreparationSelectorPredicateSource, first) == 0x08);
static_assert(offsetof(PreparationSelectorPredicateSource, second) == 0x0c);
static_assert(sizeof(PreparationSelectorPredicateSource) == 0x10);

// Read the full depth/stencil validity word, including noncanonical values.
// Byte-backed storage permits the original unaligned read.
inline uint32_t prepared_selector_second_word(const void *descriptor)
{
    return static_cast<const PreparationSelectorPredicateSource *>(descriptor)->second.get();
}

// Read the full depth/stencil validity word, including noncanonical values.
// Byte-backed storage permits the original unaligned read.
inline uint32_t prepared_selector_first_word(const void *descriptor)
{
    return static_cast<const PreparationSelectorPredicateSource *>(descriptor)->first.get();
}

struct PreparationRecordNopSource {
    uint8_t unknown_0x00[0x18];
    Field<uint32_t> word;
};
static_assert(offsetof(PreparationRecordNopSource, word) == 0x18);
static_assert(sizeof(PreparationRecordNopSource) == 0x1c);

struct PreparationRecordSource {
    Field<uint64_t> base_and_pitch;
    Field<uint64_t> slice_and_view;
    Field<uint64_t> info_and_attrib;
    Field<uint64_t> reserved_and_cmask;
    Field<uint64_t> cmask_slice_and_fmask;
    Field<uint32_t> fmask_slice;
};
static_assert(offsetof(PreparationRecordSource, base_and_pitch) == 0x00);
static_assert(offsetof(PreparationRecordSource, slice_and_view) == 0x08);
static_assert(offsetof(PreparationRecordSource, info_and_attrib) == 0x10);
static_assert(offsetof(PreparationRecordSource, reserved_and_cmask) == 0x18);
static_assert(offsetof(PreparationRecordSource, cmask_slice_and_fmask) == 0x20);
static_assert(offsetof(PreparationRecordSource, fmask_slice) == 0x28);
static_assert(sizeof(PreparationRecordSource) == 0x2c);

struct PreparationSelectorSource {
    Field<uint64_t> z_and_stencil_info;
    Field<uint64_t> z_and_stencil_read_base;
    Field<uint64_t> z_and_stencil_write_base;
    Field<uint64_t> depth_size_and_slice;
    Field<uint32_t> depth_view;
    Field<uint32_t> htile_data_base;
    Field<uint32_t> htile_surface;
    Field<uint32_t> depth_info;
    Field<uint32_t> nop_payload;
};
static_assert(offsetof(PreparationSelectorSource, z_and_stencil_info) == 0x00);
static_assert(offsetof(PreparationSelectorSource, z_and_stencil_read_base) == 0x08);
static_assert(offsetof(PreparationSelectorSource, z_and_stencil_write_base) == 0x10);
static_assert(offsetof(PreparationSelectorSource, depth_size_and_slice) == 0x18);
static_assert(offsetof(PreparationSelectorSource, depth_view) == 0x20);
static_assert(offsetof(PreparationSelectorSource, htile_data_base) == 0x24);
static_assert(offsetof(PreparationSelectorSource, htile_surface) == 0x28);
static_assert(offsetof(PreparationSelectorSource, depth_info) == 0x2c);
static_assert(offsetof(PreparationSelectorSource, nop_payload) == 0x30);
static_assert(sizeof(PreparationSelectorSource) == 0x34);

struct PreparationWordPacket {
    Field<uint32_t> header;
    Field<uint32_t> index;
    Field<uint32_t> value;
};
static_assert(offsetof(PreparationWordPacket, header) == 0x00);
static_assert(offsetof(PreparationWordPacket, index) == 0x04);
static_assert(offsetof(PreparationWordPacket, value) == 0x08);
static_assert(sizeof(PreparationWordPacket) == 0x0c);

struct PreparedRecordPacket {
    Field<uint32_t> header;
    Field<uint32_t> index;
    Field<uint64_t> base_and_pitch;
    Field<uint64_t> slice_and_view;
    Field<uint64_t> info_and_attrib;
    Field<uint64_t> reserved_and_cmask;
    Field<uint64_t> cmask_slice_and_fmask;
    Field<uint32_t> fmask_slice;
    Field<uint32_t> nop_header;
    Field<uint32_t> nop_payload;
};
static_assert(offsetof(PreparedRecordPacket, header) == 0x00);
static_assert(offsetof(PreparedRecordPacket, index) == 0x04);
static_assert(offsetof(PreparedRecordPacket, base_and_pitch) == 0x08);
static_assert(offsetof(PreparedRecordPacket, slice_and_view) == 0x10);
static_assert(offsetof(PreparedRecordPacket, info_and_attrib) == 0x18);
static_assert(offsetof(PreparedRecordPacket, reserved_and_cmask) == 0x20);
static_assert(offsetof(PreparedRecordPacket, cmask_slice_and_fmask) == 0x28);
static_assert(offsetof(PreparedRecordPacket, fmask_slice) == 0x30);
static_assert(offsetof(PreparedRecordPacket, nop_header) == 0x34);
static_assert(offsetof(PreparedRecordPacket, nop_payload) == 0x38);
static_assert(sizeof(PreparedRecordPacket) == 0x3c);

struct PreparedSelectorPacket {
    Field<uint32_t> header;
    Field<uint32_t> index;
    Field<uint64_t> z_and_stencil_info;
    Field<uint64_t> z_and_stencil_read_base;
    Field<uint64_t> z_and_stencil_write_base;
    Field<uint64_t> depth_size_and_slice;
    PreparationWordPacket depth_info;
    PreparationWordPacket depth_view;
    PreparationWordPacket htile_data_base;
    PreparationWordPacket htile_surface;
    Field<uint32_t> nop_header;
    Field<uint32_t> nop_payload;
};
static_assert(offsetof(PreparedSelectorPacket, header) == 0x00);
static_assert(offsetof(PreparedSelectorPacket, index) == 0x04);
static_assert(offsetof(PreparedSelectorPacket, z_and_stencil_info) == 0x08);
static_assert(offsetof(PreparedSelectorPacket, z_and_stencil_read_base) == 0x10);
static_assert(offsetof(PreparedSelectorPacket, z_and_stencil_write_base) == 0x18);
static_assert(offsetof(PreparedSelectorPacket, depth_size_and_slice) == 0x20);
static_assert(offsetof(PreparedSelectorPacket, depth_info) == 0x28);
static_assert(offsetof(PreparedSelectorPacket, depth_view) == 0x34);
static_assert(offsetof(PreparedSelectorPacket, htile_data_base) == 0x40);
static_assert(offsetof(PreparedSelectorPacket, htile_surface) == 0x4c);
static_assert(offsetof(PreparedSelectorPacket, nop_header) == 0x58);
static_assert(offsetof(PreparedSelectorPacket, nop_payload) == 0x5c);
static_assert(sizeof(PreparedSelectorPacket) == 0x60);

struct PreparedSelectorClearPacket {
    PreparationWordPacket first;
    PreparationWordPacket second;
};
static_assert(offsetof(PreparedSelectorClearPacket, first) == 0x00);
static_assert(offsetof(PreparedSelectorClearPacket, second) == 0x0c);
static_assert(sizeof(PreparedSelectorClearPacket) == 0x18);

constexpr uint32_t set_context_reg_one_value_header = 0xc0016900;
constexpr uint32_t nop_one_word_header = 0xc0001000;
}  // namespace render
