// SPDX-License-Identifier: GPL-2.0-or-later
// Emit only the input's captured low byte as a DWORD payload. Capacity failure
// returns the full callback result; successful admission refreshes cursor alone.
#include "present_preparation_packets.h"

extern "C" uint64_t bb_render_emit_present_preparation_byte(
    render::GpuCommandState *commands, uint32_t value)
{
    using namespace render;
    auto *buffer = reinterpret_cast<PreparationPacketBuffer *>(commands);
    const uintptr_t limit = buffer->limit.get();
    uintptr_t cursor = buffer->cursor.get();
    const uint64_t remaining_words = (limit - cursor) >> preparation_packet::word_shift;
    if (static_cast<uint32_t>(remaining_words) <= preparation_packet::last_insufficient_word) {
        const uintptr_t argument = buffer->capacity_argument.get();
        const auto capacity = buffer->capacity.get();
        const uint64_t result = capacity(commands, preparation_packet::words, argument);
        if (static_cast<uint8_t>(result) == 0) return result;
        cursor = buffer->cursor.get();
    }

    const uint32_t byte_value = static_cast<uint8_t>(value);
    auto *packet = reinterpret_cast<PreparationCommandPacket *>(cursor);
    packet->header.set(preparation_packet::header);
    packet->selector.set(preparation_packet::byte_selector);
    packet->payload.set(byte_value);
    const uintptr_t next_cursor = cursor + sizeof(PreparationCommandPacket);
    buffer->cursor.set(next_cursor);
    return next_cursor;
}
