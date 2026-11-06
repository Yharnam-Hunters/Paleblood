// SPDX-License-Identifier: GPL-2.0-or-later
// Write PA_CL_VTE_CNTL viewport-transform enable/format bits after capacity admission.
#include "present_command_configuration.h"
#include "present_register_packets.h"

extern "C" void bb_render_emit_pa_cl_vte_cntl(
    render::PacketBuffer *commands, uint32_t value)
{
    constexpr uint32_t required_words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    // The captured value is masked only after the callback's admission result.
    constexpr uint32_t viewport_transform_control_index = 0x206u;
    constexpr uint32_t viewport_transform_control_mask = 0x00000f3f;
    const uint32_t masked_value = value & viewport_transform_control_mask;
    render::write_present_register_packet(commands, cursor, viewport_transform_control_index, masked_value);
}
