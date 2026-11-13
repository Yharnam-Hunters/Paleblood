// SPDX-License-Identifier: GPL-2.0-or-later
// Write the supplied MATCH_INDEX DWORD to VGT_DISPATCH_DRAW_INDEX.
// The packet may overlap command metadata; publish from the captured cursor.
#include "present_register_packets.h"

extern "C" void bb_render_set_dispatch_draw_index(render::PacketBuffer *commands,
                                                    uint32_t match_index)
{
    constexpr uint32_t vgt_dispatch_draw_index_index = 0x2dd;
    uintptr_t cursor;
    if (!render::reserve_words(commands, render::preparation_packet::words, cursor)) return;
    render::write_present_register_packet(commands, cursor, vgt_dispatch_draw_index_index, match_index);
}
