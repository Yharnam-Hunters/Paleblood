// SPDX-License-Identifier: GPL-2.0-or-later
// The initial capacity check uses only the low DWORD after a full wrapped
// difference. Successful callback admission refreshes cursor alone, without retry.
#include "present_last_record_packet.h"

namespace {
constexpr uint32_t command_word_shift = 2;
constexpr uint32_t packet_words = 3;
constexpr uint32_t last_insufficient_word = packet_words - 1;
constexpr uint32_t packet_header = 0xc0016900;
constexpr uint32_t packet_value_mask = 0xfff;
constexpr uintptr_t packet_bytes = sizeof(render::LastRecordPacket);
}

extern "C" uint64_t bb_render_emit_present_last_record_packet(render::GpuCommandState *commands,
                                                              uint32_t value)
{
    auto *buffer = reinterpret_cast<render::LastRecordPacketBuffer *>(commands);
    const uintptr_t limit = buffer->limit.get();
    uintptr_t cursor = buffer->cursor.get();
    const uint64_t remaining_words = (limit - cursor) >> command_word_shift;
    if (static_cast<uint32_t>(remaining_words) <= last_insufficient_word) {
        const uintptr_t argument = buffer->capacity_argument.get();
        const auto capacity = buffer->capacity.get();
        const uint64_t result = capacity(commands, packet_words, argument);
        if (static_cast<uint8_t>(result) == 0) return result;
        cursor = buffer->cursor.get();
    }

    const uint32_t packet_value = value & packet_value_mask;
    auto *packet = reinterpret_cast<render::LastRecordPacket *>(cursor);
    // Keep these separate and ordered: command storage can alias buffer fields.
    packet->header.set(packet_header);
    packet->zero_word.set(0);
    packet->value.set(packet_value);
    const uintptr_t next_cursor = cursor + packet_bytes;
    buffer->cursor.set(next_cursor);
    return next_cursor;
}
