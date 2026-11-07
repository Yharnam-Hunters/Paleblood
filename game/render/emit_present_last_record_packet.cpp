// SPDX-License-Identifier: GPL-2.0-or-later
// Write the selected DB_RENDER_CONTROL bits for the last-record path after capacity admission;
// publish the captured cursor.
// Capacity admission truncates the wrapped DWORD distance; callback success reloads the cursor
// without retrying.
#include "present_last_record_packet.h"
#include "render_observe.h"

namespace {
constexpr uint32_t packet_words = 3;
constexpr uint32_t set_context_reg_one_value_header = 0xc0016900;
constexpr uint32_t db_render_control_value_mask = 0xfff;
constexpr uintptr_t packet_bytes = sizeof(render::LastRecordPacket);
}

extern "C" uint64_t bb_render_emit_present_last_record_packet(render::PacketBuffer *commands,
                                                              uint32_t value)
{
    uintptr_t cursor;
    uint64_t rejected_result;
    if (!render::reserve_words_with_result(commands, packet_words, cursor, rejected_result))
        return rejected_result;

    const uint32_t packet_value = value & db_render_control_value_mask;
    render::observe_context_register(commands, cursor, 0, packet_value);
    auto *packet = reinterpret_cast<render::LastRecordPacket *>(cursor);
    // Keep these separate and ordered: command storage can alias buffer fields.
    packet->header.set(set_context_reg_one_value_header);
    packet->register_index.set(0);
    packet->value.set(packet_value);
    const uintptr_t next_cursor = cursor + packet_bytes;
    render::set_packet_cursor(commands, next_cursor);
    return next_cursor;
}
