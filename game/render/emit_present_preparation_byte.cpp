// SPDX-License-Identifier: GPL-2.0-or-later
// Write the captured low-byte value to DB_STENCIL_CLEAR after capacity admission.
// Dirty-state callers 0x02ad2c30 and 0x02ad2790 use this writer.
#include "present_dirty_preparation_packets.h"

extern "C" uint64_t bb_render_emit_present_preparation_byte(
    render::PacketBuffer *commands, uint32_t value)
{
    using namespace render;
    uintptr_t cursor;
    uint64_t rejected_result;
    if (!reserve_words_with_result(commands, preparation_packet::words, cursor, rejected_result))
        return rejected_result;

    const uint32_t byte_value = static_cast<uint8_t>(value);
    auto *packet = reinterpret_cast<PreparationCommandPacket *>(cursor);
    packet->header.set(preparation_packet::set_context_reg_one_value_header);
    packet->register_index.set(preparation_packet::stencil_clear_index);
    packet->payload.set(byte_value);
    const uintptr_t next_cursor = cursor + sizeof(PreparationCommandPacket);
    set_packet_cursor(commands, next_cursor);
    return next_cursor;
}
