// SPDX-License-Identifier: GPL-2.0-or-later
// Configure GDS_OA_CNTL and clear the adjacent counter and address registers.
// Publish the live cursor after packet writes, which may overlap the buffer prefix.
#include "command_reservation.h"

namespace {
struct GdsOaPacket {
    uint32_t header;
    uint32_t register_index;
    uint32_t control;
    uint32_t counter;
    uint32_t address;
};
static_assert(offsetof(GdsOaPacket, header) == 0x0);
static_assert(offsetof(GdsOaPacket, register_index) == 0x4);
static_assert(offsetof(GdsOaPacket, control) == 0x8);
static_assert(offsetof(GdsOaPacket, counter) == 0xc);
static_assert(offsetof(GdsOaPacket, address) == 0x10);
static_assert(sizeof(GdsOaPacket) == 0x14);

constexpr uint32_t set_uconfig_reg_three_values_header = 0xc0037900;
constexpr uint32_t gds_oa_cntl_index = 0x41d;
constexpr uint32_t packet_words = sizeof(GdsOaPacket) / render::packet_word_bytes;
}  // namespace

extern "C" void bb_render_set_gds_oa(render::PacketBuffer *commands, uint32_t control)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, packet_words, cursor)) return;

    const GdsOaPacket packet{set_uconfig_reg_three_values_header, gds_oa_cntl_index,
                             control, 0, 0};
    render::Field<GdsOaPacket>::write(reinterpret_cast<uint8_t *>(cursor), packet);
    render::set_packet_cursor(commands, render::packet_cursor(commands) + sizeof(GdsOaPacket));
}
