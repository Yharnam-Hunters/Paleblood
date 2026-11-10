// SPDX-License-Identifier: GPL-2.0-or-later
// Finish pending command lists, retain live block/counter reads, then conditionally
// submit the pending cursor snapshot when its callback and capacity allow it.
#include <bit>

#include "packet_buffer_bounds.h"
#include "present_command_finish_calls.h"

namespace {
using namespace render;

FinishBucketSlot *bucket_slot(CommandFinishStateView *state, uint64_t index)
{
    const uintptr_t origin = reinterpret_cast<uintptr_t>(state) +
                             offsetof(CommandFinishStateView, bucket_origin);
    return reinterpret_cast<FinishBucketSlot *>(origin + index * sizeof(FinishBucketSlot));
}

uintptr_t block_word(uintptr_t block, uint64_t index)
{
    const uintptr_t address = block + index * sizeof(FinishBlockSlot);
    return reinterpret_cast<const FinishBlockSlot *>(address)->value.get();
}

void drain_nodes(CommandFinishStateView *state, FinishDrainNode *node,
                 PresentCommandFinishFrame &frame)
{
    while (node != nullptr) {
        FinishDrainNode *next = node->next.get();
        void *manager = state->node_manager;
        release_present_finish_node(manager, &frame.command_argument, node);
        node = next;
    }
}

void process_blocks(CommandFinishStateView *state, CommandFinishStateView *commands)
{
    uintptr_t block = state->block_head;
    if (block == 0) return;
    const uint32_t link_index = state->block_link_index;
    do {
        uint64_t item_index = 0;
        for (;;) {
            // The link is reread on every condition; callbacks can replace it and
            // the count of the last block. Neither value is cached for this loop.
            const uintptr_t link = block_word(block, link_index);
            const uint64_t count = link != 0 ? link_index : state->last_block_count;
            if (item_index >= count) break;
            void *processor = state->item_processor;
            const uintptr_t item = block_word(block, item_index);
            process_present_finish_item(processor, commands, item);
            state->last_reset_cursor = packet_cursor(&state->buffer.commands);
            ++item_index;
        }
        block = block_word(block, link_index);
    } while (block != 0);
}

void release_one_block(FinishBlockOwner *owner, uintptr_t block)
{
    FinishBlockOwnerVtable *vtable = owner->vtable.get();
    const auto release = vtable->release.get();
    release(owner, reinterpret_cast<void *>(block));
}

void release_blocks(CommandFinishStateView *state)
{
    FinishBlockOwner *owner = state->block_owner;
    if (owner == nullptr) return;
    const uint32_t link_index = state->block_link_index;
    uintptr_t block = state->block_head;
    if (block == 0) return;
    uintptr_t next = block_word(block, link_index);
    release_one_block(owner, block);
    block = next;
    while (block != 0) {
        owner = state->block_owner;
        next = block_word(block, link_index);
        release_one_block(owner, block);
        block = next;
    }
}

void finish_snapshot(CommandFinishStateView *state, CommandFinishStateView *commands,
                     PresentCommandFinishFrame &frame)
{
    const uint8_t flags = state->submission_flags;
    frame.snapshot.unknown_0x28 = 0;
    CommandSubmissionOwner *owner = state->owner;
    CounterWaitState *counters = owner->counters;
    uint32_t producer;
    uint32_t actual;
    std::memcpy(&producer, counters->producer_count.get(), sizeof(producer));
    std::memcpy(&actual, counters->observed_count.get(), sizeof(actual));
    const uint32_t expected = producer - 1;
    CommandFinishStateView *optional = nullptr;
    if (actual == expected) {
        copy_finish_snapshot_reverse(frame.snapshot, state->buffer);
    } else {
        constexpr uint32_t minimum_words = 7;
        constexpr uint32_t word_bytes = sizeof(uint32_t);
        const uintptr_t limit = packet_limit(&state->buffer.commands);
        const uintptr_t cursor = packet_cursor(&state->buffer.commands);
        const uint32_t available = static_cast<uint32_t>((limit - cursor) / word_bytes);
        if (available < minimum_words) {
            const uintptr_t argument = packet_callback_argument(&state->buffer.commands);
            const auto callback = packet_callback<PacketSpaceCallback>(&state->buffer.commands);
            // This request is zero, not the seven-word capacity threshold. Its AL
            // result is ignored and this path performs no delegate or cursor reset.
            callback(&state->buffer.commands, 0, argument);
            return;
        }
        copy_finish_snapshot_reverse(frame.snapshot, state->buffer);
        set_packet_begin(&state->buffer.commands, cursor);
        optional = commands;
    }
    submit_present_finish_snapshot(commands, optional, &frame.snapshot, flags);
    const uintptr_t cursor = packet_cursor(&state->buffer.commands);
    set_packet_begin(&state->buffer.commands, cursor);
    state->last_reset_cursor = cursor;
}
}  // namespace

extern "C" void bb_render_finish_present_commands(render::CommandFinishStateView *commands)
{
    using namespace render;
    PresentCommandFinishFrame frame;
    CommandFinishStateView *state = commands;
    state->last_reset_cursor = packet_cursor(&state->buffer.commands);
    if (state->block_head == 0) return;
    const uintptr_t callback_bits = reinterpret_cast<uintptr_t>(
        packet_callback<PacketSpaceCallback>(&state->buffer.commands));
    const uintptr_t expected_callback = present_finish_expected_callback.get();
    if (callback_bits != expected_callback) return;

    frame.command_argument = commands;
    uint32_t remaining = state->bucket_mask;
    if (remaining != 0) {
        do {
            const uint32_t index = std::countr_zero(remaining);
            frame.saved_bucket_index.set(index);
            const uint32_t bit = uint32_t(1) << index;
            FinishDrainNode *head = bucket_slot(state, index)->head.get();
            drain_nodes(state, head, frame);
            // A callback receives adjacent exposed frame storage. Reload the saved
            // index just as the native does, then clear this captured bucket's head.
            bucket_slot(state, frame.saved_bucket_index.get())->head.set(nullptr);
            remaining -= bit;
        } while (remaining != 0);
        state->bucket_mask = 0;
    }
    FinishDrainNode *single = state->single_head;
    drain_nodes(state, single, frame);
    state->single_head = nullptr;
    prepare_present_finish_section(&state->shader_section, commands);
    process_blocks(state, commands);
    release_blocks(state);
    state->last_block_count = 0;
    state->block_tail = 0;
    state->block_head = 0;
    finish_snapshot(state, commands, frame);
}
