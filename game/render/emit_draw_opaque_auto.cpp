// SPDX-License-Identifier: GPL-2.0-or-later
// Emit the opaque DRAW_INDEX_AUTO profile and its query-selected trace marker.
// Its initiator differs from an ordinary auto draw, which the existing draw observer
// cannot represent. Keep this writer's packet effects separate from draw execution.
#include "command_flags.h"
#include "present_command_controls.h"
#include "record_step_user_pa_enabled.h"

extern "C" void bb_render_emit_draw_opaque_auto(render::PacketBuffer *commands)
{
    constexpr uint32_t required_words = 5;
    constexpr uint32_t draw_index_auto_header = 0xc0012d00;
    constexpr uint32_t packet_predicate_bit = 1;
    constexpr uint32_t opaque_draw_count_word = 16;
    constexpr uint32_t auto_index_source_select = 2;
    constexpr uint32_t use_opaque_bit = 0x40;
    constexpr uint32_t event_write_header = 0xc0004600;
    constexpr uint32_t packet_shader_type_bit = 2;
    constexpr uint32_t thread_trace_marker_event = 0x35;
    constexpr uint32_t nop_one_word_header = 0xc0001000;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;

    const uint32_t draw_flags = packet_flags(commands);
    auto *packet = reinterpret_cast<render::RecordControlTriple *>(cursor);
    packet->header.set(draw_index_auto_header | (draw_flags & packet_predicate_bit));
    packet->first.set(opaque_draw_count_word);
    packet->second.set(auto_index_source_select | use_opaque_bit);
    render::advance_record_control_cursor(commands, sizeof(render::RecordControlTriple));

    if (render::record_step_user_pa_enabled() != 0) {
        // DRAW stores can overlap flags. The trailing event consumes their live value.
        const uint32_t live_flags = packet_flags(commands);
        const uintptr_t next = packet_cursor(commands);
        render::write_record_control_pair(next, event_write_header | (live_flags & packet_shader_type_bit),
                                          thread_trace_marker_event);
        render::advance_record_control_cursor(commands, sizeof(render::RecordControlPair));
    } else {
        const uintptr_t next = packet_cursor(commands);
        render::write_record_control_pair(next, nop_one_word_header, 0);
        // Unlike the event route's live ADD, this route publishes the captured pointer.
        set_packet_cursor(commands, next + sizeof(render::RecordControlPair));
    }
}
