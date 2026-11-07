// SPDX-License-Identifier: GPL-2.0-or-later
// Write the captured scalar bits to DB_DEPTH_CLEAR after capacity admission; callback changes do
// not replace the captured value.
// Dirty-state callers 0x02ad2c30 and 0x02ad2790 use this writer.
#include "present_dirty_preparation_packets.h"
#include "render_observe.h"
#include <bit>

extern "C" uint64_t bb_render_emit_present_preparation_scalar(
    render::PacketBuffer *commands, float scalar)
{
    const uint32_t scalar_bits = std::bit_cast<uint32_t>(scalar);
    using namespace render;
    uintptr_t cursor;
    uint64_t rejected_result;
    if (!reserve_words_with_result(commands, preparation_packet::words, cursor, rejected_result))
        return rejected_result;

    observe_context_register(commands, cursor, preparation_packet::depth_clear_index, scalar_bits);
    auto *packet = reinterpret_cast<PreparationCommandPacket *>(cursor);
    // Packet storage may alias the buffer. Keep all three DWORD writes ordered,
    // then publish the captured cursor plus the packet size without rereading it.
    packet->header.set(preparation_packet::set_context_reg_one_value_header);
    packet->register_index.set(preparation_packet::depth_clear_index);
    packet->payload.set(scalar_bits);
    const uintptr_t next_cursor = cursor + sizeof(PreparationCommandPacket);
    set_packet_cursor(commands, next_cursor);
    return next_cursor;
}
