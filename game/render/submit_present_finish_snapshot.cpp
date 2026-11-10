// SPDX-License-Identifier: GPL-2.0-or-later
// Emit required counter waits, then submit nonempty command regions in order.
// Without optional commands, initialize an empty second region. Flags are unused.
#include "present_finish_snapshot.h"
#include "command_submit.h"

extern "C" void bb_render_submit_present_finish_snapshot(
    render::CommandFinishStateView *state, render::CommandFinishStateView *optional_commands,
    render::CommandBufferSnapshot *snapshot, uint32_t)
{
    using namespace render;
    CommandBufferSnapshot empty;
    const CommandBufferSnapshot *first;
    const CommandBufferSnapshot *second;
    if (optional_commands != nullptr) {
        prepare_counter_wait(state->owner->counters,
                             &optional_commands->buffer.commands);
        first = &optional_commands->buffer;
        second = snapshot;
    } else {
        // The initializer preserves auxiliary bytes except this explicitly zeroed word.
        empty.unknown_0x28 = 0;
        initialize_command_buffer(&empty, 0, 0, nullptr, 0);
        first = snapshot;
        second = &empty;
    }
    const auto regions = collect_command_regions(*first, *second);
    if (regions.count != 0) {
        finish_snapshot_calls::sceGnmSubmitCommandBuffers(
            regions.count, regions.addresses, regions.sizes, nullptr, nullptr);
    }
}
