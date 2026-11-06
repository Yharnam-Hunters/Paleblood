// SPDX-License-Identifier: GPL-2.0-or-later
// Shader-register packet layouts and ordered descriptor writes used by the shader emitters.
// A descriptor prefix carries its control word in a NOP before SET_SH_REG.
#pragma once

#include "command_reservation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace render {

// Byte fields permit unaligned accesses and command output that overlaps the cursor
// or source descriptor.

constexpr uint32_t shader_packet_word_bytes = 4;
constexpr uint32_t shader_packet_flag_mask = 2;
constexpr uint32_t shader_register_origin = 0x2c00;
constexpr uint32_t shader_word_bits = 32;
RT_GLOBAL(0x04cd15c0, shader_stage_register_bases, const uint16_t);

inline uint32_t shader_stage_base(uint32_t stage)
{
    // The caller supplies the stage index; preserve the table read without adding a range check.
    const uintptr_t address = reinterpret_cast<uintptr_t>(shader_stage_register_bases.address()) +
                              uint64_t(stage) * sizeof(uint16_t);
    uint16_t value;
    std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
    return value;
}

struct ShaderPacketWords {
    Field<uint32_t> header;
    Field<uint32_t> register_index;
    Field<uint32_t> value_low;
    Field<uint32_t> value_high;
};
static_assert(offsetof(ShaderPacketWords, header) == 0x00);
static_assert(offsetof(ShaderPacketWords, register_index) == 0x04);
static_assert(offsetof(ShaderPacketWords, value_low) == 0x08);
static_assert(offsetof(ShaderPacketWords, value_high) == 0x0c);

struct ShaderDescriptorPacket {
    Field<uint32_t> preamble;
    Field<uint32_t> descriptor_control;
    Field<uint32_t> header;
    Field<uint32_t> register_index;
    static constexpr auto payload_word_count = 4;
    Field<uint64_t> payload[payload_word_count];
};
static_assert(offsetof(ShaderDescriptorPacket, preamble) == 0x00);
static_assert(offsetof(ShaderDescriptorPacket, descriptor_control) == 0x04);
static_assert(offsetof(ShaderDescriptorPacket, header) == 0x08);
static_assert(offsetof(ShaderDescriptorPacket, register_index) == 0x0c);
static_assert(offsetof(ShaderDescriptorPacket, payload) == 0x10);

struct ShaderDescriptorWords {
    static constexpr auto word_count = 4;
    Field<uint64_t> words[word_count];
};

inline void shader_write_descriptor_header(PacketBuffer *commands, uintptr_t cursor,
                                            uint32_t flags, uint32_t stage_base, uint32_t slot,
                                            uint32_t control, uint32_t header)
{
    constexpr uint32_t nop_one_word_header = 0xc0001000;
    constexpr uint32_t preamble_bytes = 8;
    auto *out = reinterpret_cast<ShaderDescriptorPacket *>(cursor);
    out->preamble.set(nop_one_word_header);
    out->descriptor_control.set(control);
    set_packet_cursor(commands, cursor + preamble_bytes);
    out->header.set(header | (flags & shader_packet_flag_mask));
    out->register_index.set(stage_base + slot - shader_register_origin);
}

}  // namespace render
