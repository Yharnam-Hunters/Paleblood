// SPDX-License-Identifier: GPL-2.0-or-later
// Single-value context-register packets for dirty depth and stencil clear values.
// Capacity admission uses AL; failure returns the complete callback result.
#pragma once

#include "command_capacity_result.h"

namespace render {
struct PreparationCommandPacket {
    Field<uint32_t> header;
    Field<uint32_t> register_index;
    Field<uint32_t> payload;
};
static_assert(offsetof(PreparationCommandPacket, header) == 0x00);
static_assert(offsetof(PreparationCommandPacket, register_index) == 0x04);
static_assert(offsetof(PreparationCommandPacket, payload) == 0x08);
static_assert(sizeof(PreparationCommandPacket) == 0x0c);

namespace preparation_packet {
constexpr uint32_t words = 3;
constexpr uint32_t set_context_reg_one_value_header = 0xc0016900;
constexpr uint32_t depth_clear_index = 0xb;
constexpr uint32_t stencil_clear_index = 0xa;
}

}  // namespace render
