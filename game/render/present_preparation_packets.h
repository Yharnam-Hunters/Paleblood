// SPDX-License-Identifier: GPL-2.0-or-later
// Color-buffer and depth/stencil register blocks used by the preparation consumer.
// Byte-backed fields preserve unaligned input and overlap with packet output or the live cursor.
#pragma once

#include "command_reservation.h"
#include "present_dirty_preparation_packets.h"

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x014738b0, render_set_render_target,
            void(PacketBuffer *, uint32_t slot, const void *descriptor));
RT_ORIGINAL(0x014739b0, render_set_depth_render_target,
            void(PacketBuffer *, const void *descriptor));

// Narrow views retain the exact validity and NOP-payload reads without
// widening them to the overlapping 64-bit register pairs copied by the writers.
struct ColorTargetValidityView {
    Field<uint32_t> base;
};
static_assert(offsetof(ColorTargetValidityView, base) == 0x00);
static_assert(sizeof(ColorTargetValidityView) == 0x04);

// Read the full color-buffer validity word, including noncanonical values.
// Byte-backed storage permits the original unaligned read.
inline uint32_t color_target_base(const void *descriptor)
{
    return static_cast<const ColorTargetValidityView *>(descriptor)->base.get();
}

struct DepthTargetValidityView {
    uint8_t unknown_0x00[0x08];
    Field<uint32_t> z_read_base;
    Field<uint32_t> stencil_read_base;
};
static_assert(offsetof(DepthTargetValidityView, z_read_base) == 0x08);
static_assert(offsetof(DepthTargetValidityView, stencil_read_base) == 0x0c);
static_assert(sizeof(DepthTargetValidityView) == 0x10);

// Keep the independent full-word checks for the stencil and depth read bases.
inline uint32_t depth_target_stencil_read_base(const void *descriptor)
{
    return static_cast<const DepthTargetValidityView *>(descriptor)->stencil_read_base.get();
}

inline uint32_t depth_target_z_read_base(const void *descriptor)
{
    return static_cast<const DepthTargetValidityView *>(descriptor)->z_read_base.get();
}

struct ColorTargetNopView {
    uint8_t unknown_0x00[0x18];
    Field<uint32_t> payload;
};
static_assert(offsetof(ColorTargetNopView, payload) == 0x18);
static_assert(sizeof(ColorTargetNopView) == 0x1c);

struct ColorTargetDescriptorView {
    Field<uint64_t> base_and_pitch;
    Field<uint64_t> slice_and_view;
    Field<uint64_t> info_and_attrib;
    Field<uint64_t> reserved_and_cmask;
    Field<uint64_t> cmask_slice_and_fmask;
    Field<uint32_t> fmask_slice;
};
static_assert(offsetof(ColorTargetDescriptorView, base_and_pitch) == 0x00);
static_assert(offsetof(ColorTargetDescriptorView, slice_and_view) == 0x08);
static_assert(offsetof(ColorTargetDescriptorView, info_and_attrib) == 0x10);
static_assert(offsetof(ColorTargetDescriptorView, reserved_and_cmask) == 0x18);
static_assert(offsetof(ColorTargetDescriptorView, cmask_slice_and_fmask) == 0x20);
static_assert(offsetof(ColorTargetDescriptorView, fmask_slice) == 0x28);
static_assert(sizeof(ColorTargetDescriptorView) == 0x2c);

struct DepthTargetDescriptorView {
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
static_assert(offsetof(DepthTargetDescriptorView, z_and_stencil_info) == 0x00);
static_assert(offsetof(DepthTargetDescriptorView, z_and_stencil_read_base) == 0x08);
static_assert(offsetof(DepthTargetDescriptorView, z_and_stencil_write_base) == 0x10);
static_assert(offsetof(DepthTargetDescriptorView, depth_size_and_slice) == 0x18);
static_assert(offsetof(DepthTargetDescriptorView, depth_view) == 0x20);
static_assert(offsetof(DepthTargetDescriptorView, htile_data_base) == 0x24);
static_assert(offsetof(DepthTargetDescriptorView, htile_surface) == 0x28);
static_assert(offsetof(DepthTargetDescriptorView, depth_info) == 0x2c);
static_assert(offsetof(DepthTargetDescriptorView, nop_payload) == 0x30);
static_assert(sizeof(DepthTargetDescriptorView) == 0x34);

struct ColorTargetPacket {
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
static_assert(offsetof(ColorTargetPacket, header) == 0x00);
static_assert(offsetof(ColorTargetPacket, index) == 0x04);
static_assert(offsetof(ColorTargetPacket, base_and_pitch) == 0x08);
static_assert(offsetof(ColorTargetPacket, slice_and_view) == 0x10);
static_assert(offsetof(ColorTargetPacket, info_and_attrib) == 0x18);
static_assert(offsetof(ColorTargetPacket, reserved_and_cmask) == 0x20);
static_assert(offsetof(ColorTargetPacket, cmask_slice_and_fmask) == 0x28);
static_assert(offsetof(ColorTargetPacket, fmask_slice) == 0x30);
static_assert(offsetof(ColorTargetPacket, nop_header) == 0x34);
static_assert(offsetof(ColorTargetPacket, nop_payload) == 0x38);
static_assert(sizeof(ColorTargetPacket) == 0x3c);

struct DepthTargetPacket {
    Field<uint32_t> header;
    Field<uint32_t> index;
    Field<uint64_t> z_and_stencil_info;
    Field<uint64_t> z_and_stencil_read_base;
    Field<uint64_t> z_and_stencil_write_base;
    Field<uint64_t> depth_size_and_slice;
    PreparationCommandPacket depth_info;
    PreparationCommandPacket depth_view;
    PreparationCommandPacket htile_data_base;
    PreparationCommandPacket htile_surface;
    Field<uint32_t> nop_header;
    Field<uint32_t> nop_payload;
};
static_assert(offsetof(DepthTargetPacket, header) == 0x00);
static_assert(offsetof(DepthTargetPacket, index) == 0x04);
static_assert(offsetof(DepthTargetPacket, z_and_stencil_info) == 0x08);
static_assert(offsetof(DepthTargetPacket, z_and_stencil_read_base) == 0x10);
static_assert(offsetof(DepthTargetPacket, z_and_stencil_write_base) == 0x18);
static_assert(offsetof(DepthTargetPacket, depth_size_and_slice) == 0x20);
static_assert(offsetof(DepthTargetPacket, depth_info) == 0x28);
static_assert(offsetof(DepthTargetPacket, depth_view) == 0x34);
static_assert(offsetof(DepthTargetPacket, htile_data_base) == 0x40);
static_assert(offsetof(DepthTargetPacket, htile_surface) == 0x4c);
static_assert(offsetof(DepthTargetPacket, nop_header) == 0x58);
static_assert(offsetof(DepthTargetPacket, nop_payload) == 0x5c);
static_assert(sizeof(DepthTargetPacket) == 0x60);

struct ClearDepthTargetPacket {
    PreparationCommandPacket z_info;
    PreparationCommandPacket stencil_info;
};
static_assert(offsetof(ClearDepthTargetPacket, z_info) == 0x00);
static_assert(offsetof(ClearDepthTargetPacket, stencil_info) == 0x0c);
static_assert(sizeof(ClearDepthTargetPacket) == 0x18);

constexpr uint32_t nop_one_word_header = 0xc0001000;
}  // namespace render
