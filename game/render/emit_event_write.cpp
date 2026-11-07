// SPDX-License-Identifier: GPL-2.0-or-later
// Emit EVENT_WRITE with a masked event type and the original table-selected event index.
#include "present_command_controls.h"

extern "C" void bb_render_emit_event_write(render::PacketBuffer *commands,
                                                uint32_t event_type)
{
    constexpr uint32_t required_words = 2;
    constexpr uint32_t event_write_header = 0xc0004600;
    constexpr uint32_t event_type_mask = 0x3f;
    constexpr uint32_t table_mask = 0x0f;
    constexpr uint32_t table_shift = 8;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;

    const uint32_t event_index_word = render::record_control_mode_words.get()[event_type];
    const uint32_t encoded = (event_type & event_type_mask) | ((event_index_word & table_mask) << table_shift);
    render::write_record_control_pair(cursor, event_write_header, encoded);
    render::advance_record_control_cursor(commands, sizeof(render::RecordControlPair));
}
