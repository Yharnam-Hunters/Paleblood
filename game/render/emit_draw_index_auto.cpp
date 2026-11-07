// SPDX-License-Identifier: GPL-2.0-or-later
// Emit DRAW_INDEX_AUTO, then a THREAD_TRACE_MARKER event when the original query admits it,
// otherwise a NOP.
// Packet stores can overlap flags or cursor; reload them after the query before
// choosing and publishing the trailing event packet.
#include "command_flags.h"
#include "present_command_controls.h"
#include "record_step_user_pa_enabled.h"
#include "render_observe.h"

extern "C" void bb_render_emit_draw_index_auto(render::PacketBuffer *commands,
                                                   uint32_t vertex_count)
{
    constexpr uint32_t required_words = 5;
    constexpr uint32_t draw_index_auto_header = 0xc0012d00;
    constexpr uint32_t packet_predicate_bit = 1;
    constexpr uint32_t auto_index_source_select = 2;
    constexpr uint32_t event_write_header = 0xc0004600;
    constexpr uint32_t packet_shader_type_bit = 2;
    constexpr uint32_t thread_trace_marker_event = 0x35;
    constexpr uint32_t nop_one_word_header = 0xc0001000;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    rt_observe_draw_auto(render::observed_buffer(commands), render::observed_offset(commands, cursor), vertex_count);
    const uint32_t flags = packet_flags(commands);
    auto *packet = reinterpret_cast<render::RecordControlTriple *>(cursor);
    packet->header.set(draw_index_auto_header | (flags & packet_predicate_bit));
    packet->first.set(vertex_count);
    packet->second.set(auto_index_source_select);
    render::advance_record_control_cursor(commands, sizeof(render::RecordControlTriple));

    if (render::record_step_user_pa_enabled() != 0) {
        const uint32_t live_flags = packet_flags(commands);
        const uintptr_t next = packet_cursor(commands);
        render::write_record_control_pair(next, event_write_header | (live_flags & packet_shader_type_bit),
                                          thread_trace_marker_event);
        render::advance_record_control_cursor(commands, sizeof(render::RecordControlPair));
    } else {
        const uintptr_t next = packet_cursor(commands);
        render::write_record_control_pair(next, nop_one_word_header, 0);
        // This route publishes the captured next pointer; the user-PA route above
        // performs a live ADD instead. The distinction is observable under overlap.
        set_packet_cursor(commands, next + sizeof(render::RecordControlPair));
    }
}
