// SPDX-License-Identifier: GPL-2.0-or-later
// Emit the captured scalar as unchanged bits. On the native slow path it is
// spilled before the callback and reloaded after admission; no float math occurs.
#include "present_preparation_packets.h"
#include <bit>

extern "C" uint64_t bb_render_emit_present_preparation_scalar(
    render::GpuCommandState *commands, float scalar)
{
    const uint32_t scalar_bits = std::bit_cast<uint32_t>(scalar);
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

    auto *packet = reinterpret_cast<PreparationCommandPacket *>(cursor);
    // Packet storage may alias the buffer. Keep all three DWORD writes ordered,
    // then publish the captured cursor plus the packet size without rereading it.
    packet->header.set(preparation_packet::header);
    packet->selector.set(preparation_packet::scalar_selector);
    packet->payload.set(scalar_bits);
    const uintptr_t next_cursor = cursor + sizeof(PreparationCommandPacket);
    buffer->cursor.set(next_cursor);
    return next_cursor;
}
