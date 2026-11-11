// SPDX-License-Identifier: GPL-2.0-or-later
// Indirect draw packets share register selection and a query-selected trailing packet.
#pragma once
#include "command_flags.h"
#include "present_command_controls.h"
#include "record_step_user_pa_enabled.h"

namespace render {
// The full DWORD bank selects mapped original storage. No table extent or bounds
// check is established. The original reads it even when both byte selectors are zero.
RT_GLOBAL(0x04b2bf30, indirect_register_base_words, const volatile uint32_t);

struct IndirectDrawPacket {
    Field<uint32_t> header;
    Field<uint32_t> data_offset;
    Field<uint32_t> first_register_index;
    Field<uint32_t> second_register_index;
    Field<uint32_t> initiator;
};
static_assert(offsetof(IndirectDrawPacket, header) == 0x00);
static_assert(offsetof(IndirectDrawPacket, data_offset) == 0x04);
static_assert(offsetof(IndirectDrawPacket, first_register_index) == 0x08);
static_assert(offsetof(IndirectDrawPacket, second_register_index) == 0x0c);
static_assert(offsetof(IndirectDrawPacket, initiator) == 0x10);
static_assert(sizeof(IndirectDrawPacket) == 0x14);

static inline uint32_t indirect_register_base(uint32_t bank)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(indirect_register_base_words.address()) +
                              static_cast<uintptr_t>(bank) * sizeof(uint32_t);
    return *reinterpret_cast<const volatile uint32_t *>(address);
}

static inline uint32_t indirect_register_index(uint32_t base, uint32_t selector)
{
    constexpr uint32_t register_origin = 0x2c00;
    const uint8_t byte_selector = static_cast<uint8_t>(selector);
    return byte_selector == 0 ? 0 : static_cast<uint16_t>(base + byte_selector - register_origin);
}

template <uint32_t DrawHeader, uint32_t Initiator>
static inline void emit_indirect_draw(PacketBuffer *commands, uint32_t data_offset,
                                      uint32_t bank, uint32_t first_selector, uint32_t second_selector)
{
    constexpr uint32_t required_words = 7;
    constexpr uint32_t predicate_bit = 1;
    constexpr uint32_t shader_type_bit = 2;
    constexpr uint32_t event_write_header = 0xc0004600;
    constexpr uint32_t thread_trace_marker = 0x35;
    constexpr uint32_t nop_header = 0xc0001000;
    uintptr_t cursor;
    if (!reserve_words(commands, required_words, cursor)) return;

    const uint32_t header = DrawHeader | (packet_flags(commands) & predicate_bit);
    const uint32_t base = indirect_register_base(bank);
    const uint32_t first_index = indirect_register_index(base, first_selector);
    const uint32_t second_index = indirect_register_index(base, second_selector);
    auto *packet = reinterpret_cast<IndirectDrawPacket *>(cursor);
    packet->header.set(header);
    packet->data_offset.set(data_offset);
    packet->first_register_index.set(first_index);
    packet->second_register_index.set(second_index);
    packet->initiator.set(Initiator);
    advance_record_control_cursor(commands, sizeof(IndirectDrawPacket));

    if (record_step_user_pa_enabled() != 0) {
        const uint32_t live_flags = packet_flags(commands);
        const uintptr_t next = packet_cursor(commands);
        write_record_control_pair(next, event_write_header | (live_flags & shader_type_bit),
                                  thread_trace_marker);
        advance_record_control_cursor(commands, sizeof(RecordControlPair));
    } else {
        const uintptr_t next = packet_cursor(commands);
        write_record_control_pair(next, nop_header, 0);
        // The event route adds to the stored cursor after its packet writes.
        // This route instead publishes the captured pointer plus eight bytes.
        set_packet_cursor(commands, next + sizeof(RecordControlPair));
    }
}
}  // namespace render
