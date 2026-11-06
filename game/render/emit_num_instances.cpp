// SPDX-License-Identifier: GPL-2.0-or-later
// Emit NUM_INSTANCES with the complete supplied instance count.
#include "present_command_controls.h"
#include "render_observe.h"

extern "C" void bb_render_emit_num_instances(render::PacketBuffer *commands,
                                              uint32_t instance_count)
{
    constexpr uint32_t required_words = 2;
    constexpr uint32_t num_instances_header = 0xc0002f00;
    uintptr_t cursor;
    if (!render::reserve_words(commands, required_words, cursor)) return;
    rt_observe_instances(render::observed_buffer(commands), render::observed_offset(commands, cursor), instance_count);
    render::write_record_control_pair(cursor, num_instances_header, instance_count);
    render::advance_record_control_cursor(commands, sizeof(render::RecordControlPair));
}
