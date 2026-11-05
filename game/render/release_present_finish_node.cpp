// SPDX-License-Identifier: GPL-2.0-or-later
// The manager helper decides whether the expected head matched; do not substitute
// a host atomic operation or cache the command pointer across its retries.
#include "present_finish_node.h"

namespace {
constexpr uint64_t released_node_word = 2;
constexpr uint32_t node_packet_selector = 0x28;
constexpr uint32_t node_packet_count = 4;
constexpr uint32_t event_eop_data_selection = 2;
}  // namespace

extern "C" uintptr_t bb_render_release_present_finish_node(
    render::FinishNodeManager *manager, render::CommandFinishStateView **command_argument,
    render::FinishReleaseNode *node)
{
    for (;;) {
        const uintptr_t expected = manager->head.value.get();
        node->next.set(expected);
        if (render::exchange_present_finish_node_head(&manager->head, node, expected) == expected)
            break;
    }

    node->word_00.set(released_node_word);
    render::CommandFinishStateView *commands = *command_argument;
    return render::emit_released_present_finish_node(
        &commands->buffer.commands, node_packet_selector, 0, node, 1, node_packet_count,
        0, event_eop_data_selection);
}
