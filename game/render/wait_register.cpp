// SPDX-License-Identifier: GPL-2.0-or-later
// Emit a register-space WAIT_REG_MEM packet with the original live publication.
#include "command_reservation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr uint32_t wait_reg_mem_header = 0xc0053c00;
constexpr uint32_t comparison_mask = 0x7;
constexpr uint32_t poll_interval = 10;

struct WaitRegisterPacket {
    uint32_t header;
    uint32_t control;
    uint32_t register_address;
    uint32_t address_high;
    uint32_t reference;
    uint32_t mask;
    uint32_t poll_interval;
};
static_assert(offsetof(WaitRegisterPacket, header) == 0x0);
static_assert(offsetof(WaitRegisterPacket, control) == 0x4);
static_assert(offsetof(WaitRegisterPacket, register_address) == 0x8);
static_assert(offsetof(WaitRegisterPacket, address_high) == 0xc);
static_assert(offsetof(WaitRegisterPacket, reference) == 0x10);
static_assert(offsetof(WaitRegisterPacket, mask) == 0x14);
static_assert(offsetof(WaitRegisterPacket, poll_interval) == 0x18);
static_assert(sizeof(WaitRegisterPacket) == 0x1c);
constexpr uint32_t packet_words = sizeof(WaitRegisterPacket) / render::packet_word_bytes;
}  // namespace

extern "C" void bb_render_wait_register(render::PacketBuffer *commands, uint32_t register_address,
                                       uint32_t mask, uint32_t comparison, uint32_t reference)
{
    uintptr_t cursor;
    if (!render::reserve_words(commands, packet_words, cursor)) return;

    // Only comparison bits reach control; register space and other control bits stay zero.
    const WaitRegisterPacket packet{wait_reg_mem_header, comparison & comparison_mask,
                                    static_cast<uint16_t>(register_address), 0,
                                    reference, mask, poll_interval};
    std::memcpy(reinterpret_cast<void *>(cursor), &packet, sizeof(packet));
    // Stores can overlap the prefix. Increment its live cursor after all seven words.
    render::set_packet_cursor(commands, render::packet_cursor(commands) + sizeof(packet));
}
