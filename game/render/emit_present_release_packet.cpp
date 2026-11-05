// SPDX-License-Identifier: GPL-2.0-or-later
// Emit EVENT_WRITE_EOP with the supplied event/control fields, address and data after capacity
// admission.
// The two trailing selectors are ordinary by-value DWORD arguments.
#include "present_release_packet.h"

namespace {
constexpr uint32_t release_packet_words = 6;
constexpr uint32_t event_write_eop_header = 0xc0044700;
constexpr uint32_t event_control_six_bit_mask = 0x3f;
constexpr uint32_t stack_selector_shift = 12;
constexpr uint32_t stack_mode_mask = 0x3;
constexpr uint32_t stack_mode_shift = 25;
constexpr uint32_t control_flag_mask = 0x10;
constexpr uint32_t control_flag_shift = 23;
constexpr uint32_t release_control_base = 0x500;
constexpr uint32_t address_low_mask = 0xfffffff8;
constexpr uint32_t address_high_mask = 0xffff;
constexpr uint32_t address_flag_mask = 0x1;
constexpr uint32_t address_flag_shift = 16;
constexpr uint32_t address_mode_shift = 29;
constexpr uint32_t high_word_shift = 32;
}  // namespace

extern "C" uintptr_t bb_render_emit_present_release_packet(
    render::PacketBuffer *buffer, uint32_t selector, uint32_t flags,
    uint64_t address, uint32_t mode, uint64_t data,
    uint32_t stack_selector, uint32_t stack_mode)
{
    using namespace render;
    uintptr_t write_pointer;
    uint64_t rejected_result;
    if (!reserve_words_with_result(buffer, release_packet_words, write_pointer, rejected_result))
        return rejected_result;

    const uint32_t control = (selector & event_control_six_bit_mask) |
        ((stack_selector & event_control_six_bit_mask) << stack_selector_shift) |
        ((stack_mode & stack_mode_mask) << stack_mode_shift) |
        ((flags & control_flag_mask) << control_flag_shift) | release_control_base;
    const uint32_t address_low = static_cast<uint32_t>(address) & address_low_mask;
    const uint32_t address_high =
        (static_cast<uint32_t>(address >> high_word_shift) & address_high_mask) |
        ((flags & address_flag_mask) << address_flag_shift) |
        (mode << address_mode_shift);
    const uint32_t data_low = static_cast<uint32_t>(data);
    const uint32_t data_high = static_cast<uint32_t>(data >> high_word_shift);

    auto *packet = reinterpret_cast<PresentReleasePacket *>(write_pointer);
    packet->header.set(event_write_eop_header);
    packet->control.set(control);
    packet->address_low.set(address_low);
    packet->address_high.set(address_high);
    packet->data_low.set(data_low);
    packet->data_high.set(data_high);
    // Packet stores can alias the buffer itself: ADD reads the live cursor.
    set_packet_cursor(buffer, packet_cursor(buffer) + sizeof(PresentReleasePacket));
    return write_pointer;
}
