// SPDX-License-Identifier: GPL-2.0-or-later
// Emit SET_BASE for base index one, with an eight-byte-aligned 40-bit address.
#include "command_flags.h"
#include "present_command_controls.h"

namespace render {
struct SetBasePacket {
    Field<uint32_t> header;
    Field<uint32_t> base_index;
    Field<uint32_t> address_low;
    Field<uint32_t> address_high;
};
static_assert(offsetof(SetBasePacket, header) == 0x00);
static_assert(offsetof(SetBasePacket, base_index) == 0x04);
static_assert(offsetof(SetBasePacket, address_low) == 0x08);
static_assert(offsetof(SetBasePacket, address_high) == 0x0c);
static_assert(sizeof(SetBasePacket) == 0x10);
}  // namespace render

extern "C" void bb_render_emit_set_base(render::PacketBuffer *commands, uint64_t address)
{
    constexpr uint32_t required_words = 4;
    constexpr uint32_t set_base_header = 0xc0021100;
    constexpr uint32_t packet_shader_type_bit = 2;
    constexpr uint32_t base_index = 1;
    constexpr uint32_t aligned_low_mask = 0xfffffff8;
    constexpr uint32_t high_mask = 0xff;
    constexpr uint32_t high_shift = 32;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;

    const uint32_t header = set_base_header | (render::packet_flags(commands) & packet_shader_type_bit);
    const uint32_t address_low = static_cast<uint32_t>(address) & aligned_low_mask;
    const uint32_t address_high = static_cast<uint32_t>(address >> high_shift) & high_mask;
    auto *packet = reinterpret_cast<render::SetBasePacket *>(cursor);
    packet->header.set(header);
    packet->base_index.set(base_index);
    packet->address_low.set(address_low);
    packet->address_high.set(address_high);
    // Packet stores may overwrite the cursor itself; the original adds to its live value.
    render::advance_record_control_cursor(commands, sizeof(render::SetBasePacket));
}
