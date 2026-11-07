// SPDX-License-Identifier: GPL-2.0-or-later
// Primary and optional shader state consumed by configure_record_shader. Sparse masks
// select cached bindings before the VS/PS packet writers and PS link-word emission.
#pragma once

#include "command_flags.h"
#include "shader_link.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "shader_packet_layout.h"

namespace render {
constexpr std::size_t primary_shader_bank_count = 3;
constexpr std::size_t primary_shader_slot_count = 64;
constexpr std::size_t primary_shader_special_flag_index = 58;
constexpr std::size_t primary_shader_first_selector_index = 59;
constexpr std::size_t primary_shader_second_selector_index = 60;
constexpr std::size_t primary_shader_first_one_hot_slot = 58;
constexpr std::size_t primary_shader_second_one_hot_slot = 59;
constexpr std::size_t primary_shader_special_slot = 31;
constexpr std::size_t primary_shader_mask_flag_byte = 3;
constexpr uint8_t primary_shader_special_mask = 0x80;
constexpr uint64_t primary_shader_dirty_bit = 0x80000000;
constexpr uint64_t primary_shader_invalid = UINT64_MAX;

// The optional shader program supplies at most shader_link_word_capacity link words.
// Larger counts overrun the original local buffer; callers must satisfy this bound.
inline uint8_t &packet_flag_byte(PacketBuffer *commands)
{
    return reinterpret_cast<CommandPacketFlags *>(commands)->flags.bytes[0];
}

struct ShaderPrimaryPair {
    Field<uint64_t> low;
    Field<uint64_t> high;
};
struct ShaderPrimaryPairValue { uint64_t low, high; };
static_assert(sizeof(ShaderPrimaryPair) == 0x10);
static_assert(offsetof(ShaderPrimaryPair, high) == 0x08);
static_assert(sizeof(ShaderPrimaryPairValue) == sizeof(ShaderPrimaryPair));

struct ShaderPrimaryIndexMap { uint8_t indices[primary_shader_slot_count]; };
struct ShaderPrimarySlotBank { Field<uint64_t> slots[primary_shader_slot_count]; };

// d2/d3/d4 overlap the last map, so they are accessed through that same byte storage.
struct ShaderPrimaryDescriptorView {
    Field<uint64_t> masks[primary_shader_bank_count];
    ShaderPrimaryIndexMap maps[primary_shader_bank_count];
    Field<uintptr_t> table_d8;  // QWORD binding table indexed by the three sparse maps.
    uint8_t unknown_0xe0[0x10];
    ShaderPrimaryPair pair_f0;  // Pair published into the selected stage cache when both descriptor gates admit.
    Field<uintptr_t> program_100;  // Shader program; its +8 member is passed to GNM.
};
static_assert(offsetof(ShaderPrimaryDescriptorView, maps) == 0x18);
static_assert(offsetof(ShaderPrimaryDescriptorView, table_d8) == 0xd8);
static_assert(offsetof(ShaderPrimaryDescriptorView, pair_f0) == 0xf0);
static_assert(offsetof(ShaderPrimaryDescriptorView, program_100) == 0x100);
static_assert(sizeof(ShaderPrimaryDescriptorView) == 0x108);

// The one-byte source anchors describe addresses, not capacities of trailing arrays.
struct ShaderPrimaryProgramView {
    uint8_t unknown_0x00[0x3];
    uint8_t index_03;  // Starting link-pair index, used by both primary and optional programs.
    uint8_t unknown_0x04[0x4];
    uint8_t command_08;
    uint8_t unknown_0x09[0x1b];
    uint8_t index_24;  // Additional primary-program link-pair offset.
    uint8_t count_25;  // Number of primary link pairs searched for each optional key.
    uint8_t unknown_0x26[0x2];
    uint8_t source_28;  // Beginning of the primary link-pair storage; its extent is supplied by count_25.
    uint8_t unknown_0x29[0xf];
    uint8_t count_38;  // PS link-word count.
    uint8_t unknown_0x39[0x3];
    uint8_t source_3c;  // First PS link-pair byte.
};
static_assert(offsetof(ShaderPrimaryProgramView, index_03) == 0x03);
static_assert(offsetof(ShaderPrimaryProgramView, command_08) == 0x08);
static_assert(offsetof(ShaderPrimaryProgramView, index_24) == 0x24);
static_assert(offsetof(ShaderPrimaryProgramView, count_25) == 0x25);
static_assert(offsetof(ShaderPrimaryProgramView, source_28) == 0x28);
static_assert(offsetof(ShaderPrimaryProgramView, count_38) == 0x38);
static_assert(offsetof(ShaderPrimaryProgramView, source_3c) == 0x3c);

struct ShaderPrimaryStageView {
    uint8_t unknown_0x00[0x4];
    Field<uint32_t> flag_04;  // Requests clearing the command-buffer shader flag before updating bindings.
    uint8_t unknown_0x08[0x20];
    ShaderPrimaryPair dirty_28;  // Dirty masks; the high lane is invalidated and the low lane tracks the special slot.
    Field<uint64_t> invalid_38;  // Cache sentinel reset to all ones after both shader passes.
    uint8_t unknown_0x40[0x20];
    Field<uint64_t> masks[primary_shader_bank_count];
    ShaderPrimarySlotBank banks[primary_shader_bank_count];
};
static_assert(offsetof(ShaderPrimaryStageView, flag_04) == 0x04);
static_assert(offsetof(ShaderPrimaryStageView, dirty_28) == 0x28);
static_assert(offsetof(ShaderPrimaryStageView, invalid_38) == 0x38);
static_assert(offsetof(ShaderPrimaryStageView, masks) == 0x60);
static_assert(offsetof(ShaderPrimaryStageView, banks) == 0x78);
static_assert(sizeof(ShaderPrimarySlotBank) == 0x200);
static_assert(sizeof(ShaderPrimaryStageView) == 0x678);

struct ShaderPrimaryStateView {
    ShaderPrimaryStageView primary;
    uint8_t unknown_0x0678[0x1288];
    ShaderPrimaryStageView optional;
    uint8_t unknown_0x1f78[0x630];
    Field<uint64_t> invalid_25a8;  // Shared shader-cache sentinel reset after both shader passes.
    uint8_t unknown_0x25b0[0x7620];
    ShaderPrimaryPair primary_pair_9bd0;
    uint8_t unknown_0x9be0[0x4f0];
    ShaderPrimaryPair optional_pair_a0d0;
};
static_assert(offsetof(ShaderPrimaryStateView, optional) == 0x1900);
static_assert(offsetof(ShaderPrimaryStateView, invalid_25a8) == 0x25a8);
static_assert(offsetof(ShaderPrimaryStateView, primary_pair_9bd0) == 0x9bd0);
static_assert(offsetof(ShaderPrimaryStateView, optional_pair_a0d0) == 0xa0d0);
static_assert(sizeof(ShaderPrimaryStateView) == 0xa0e0);
static_assert(alignof(ShaderPrimaryStateView) == 1);

RT_ORIGINAL(0x01475480, set_vs_shader,
            void(PacketBuffer *, const void *, uint32_t));
RT_ORIGINAL(0x014753c0, set_ps_shader,
            void(PacketBuffer *, const void *));
RT_ORIGINAL(0x014873a0, build_shader_link_words,
            void(void *, const void *, uint32_t, const void *, uint32_t));
RT_ORIGINAL(0x01475000, emit_shader_link_words,
            void(PacketBuffer *, const void *, uint32_t));

}  // namespace render
