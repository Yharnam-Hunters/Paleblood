// SPDX-License-Identifier: GPL-2.0-or-later
// Write MSAA_ENABLE and VPORT_SCISSOR_ENABLE in PA_SC_MODE_CNTL_0.
#include "present_register_packets.h"

extern "C" void bb_render_set_pa_sc_mode_cntl_0(render::PacketBuffer *commands,
                                                   uint32_t msaa_enable, uint32_t viewport_scissor_enable)
{
    constexpr uint32_t scan_mode_control_index = 0x292;
    constexpr uint32_t first_mask = 1;
    constexpr uint32_t second_mask = 2;
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    const uint32_t packed = (msaa_enable & first_mask) | ((viewport_scissor_enable + viewport_scissor_enable) & second_mask);
    render::write_present_register_packet(commands, cursor, scan_mode_control_index, packed);
}
