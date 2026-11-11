// SPDX-License-Identifier: GPL-2.0-or-later
// Emit DRAW_INDIRECT with its two optional register indices and trailing event or NOP.
#include "indirect_draw_packet.h"

extern "C" void bb_render_emit_draw_indirect(render::PacketBuffer *commands, uint32_t data_offset,
                                             uint32_t bank, uint32_t first_selector, uint32_t second_selector)
{
    constexpr uint32_t draw_header = 0xc0032400;
    constexpr uint32_t auto_index_source = 2;
    render::emit_indirect_draw<draw_header, auto_index_source>(commands, data_offset, bank,
                                                             first_selector, second_selector);
}
