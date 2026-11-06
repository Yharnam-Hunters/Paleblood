// SPDX-License-Identifier: GPL-2.0-or-later
// Finalize the record consumer's two dirty passes, then clear its dirty flags.
// Use cached event-control bits while retaining live command fields and the delayed mode read.
#include "secondary_calls.h"
#include "finish_record_shader.h"

namespace {
constexpr uint32_t first_dirty_pass = 0x80;
constexpr uint32_t second_dirty_pass = 0x10;
constexpr uint32_t counted_event_mask = 0x102;
constexpr uint32_t broad_event_mask = 0x60;
constexpr uint32_t active_preparation = 1;
constexpr uint32_t counted_event = 5;
constexpr uint32_t broad_event = 0xf;
constexpr uint32_t ordinary_event = 1;
constexpr uint32_t unchanged_mode = UINT32_MAX;

void finish_dirty_pass(render::ShaderCleanupContext *commands, bool counted, bool broad)
{
    const uint32_t mode = commands->preparation.mode.get() | active_preparation;
    commands->preparation.mode.set(mode);
    uint32_t event = commands->preparation.event.get();
    if (counted) {
        event |= counted_event;
        commands->preparation.event.set(event);
        commands->preparation.event_count.set(0);
    } else {
        if (broad) {
            event |= broad_event;
        } else {
            event |= ordinary_event;
        }
        commands->preparation.event.set(event);
    }
}
}

extern "C" void bb_render_finish_record_shader(render::ShaderCleanupState *state,
                                                render::PacketBuffer *commands)
{
    using namespace render;
    auto *context = reinterpret_cast<ShaderCleanupContext *>(commands);
    const uint32_t dirty = state->dirty_flags.get();
    const bool counted = (dirty & counted_event_mask) != 0;
    const bool broad = (dirty & broad_event_mask) != 0;
    if ((dirty & first_dirty_pass) != 0) finish_dirty_pass(context, counted, broad);
    if ((dirty & second_dirty_pass) != 0) finish_dirty_pass(context, counted, broad);
    state->dirty_flags.set(0);
    // Read after the command writes, since the state may alias the preparation mode.
    const uint32_t mode = state->command_mode.get();
    if (mode != unchanged_mode) select_shader_command_mode(commands, mode);
}
