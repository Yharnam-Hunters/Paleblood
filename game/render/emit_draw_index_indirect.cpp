// SPDX-License-Identifier: GPL-2.0-or-later
// Emit DRAW_INDEX_INDIRECT with its two optional register indices and trailing event or NOP.
#include "indirect_draw_packet.h"

extern "C" void bb_render_emit_draw_index_indirect(render::PacketBuffer *commands, uint32_t data_offset,
                                                   uint32_t bank, uint32_t first_selector, uint32_t second_selector)
{
    constexpr uint32_t draw_header = 0xc0032500;
    constexpr uint32_t dma_index_source = 0;
    render::emit_indirect_draw<draw_header, dma_index_source>(commands, data_offset, bank,
                                                            first_selector, second_selector);
}
