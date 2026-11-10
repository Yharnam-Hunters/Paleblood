// SPDX-License-Identifier: GPL-2.0-or-later
// Grow the list of previous presentation buffers and obtain writable command
// space. Save the previous region before installing the acquired buffer and
// submitting the saved snapshot.
#include "packet_buffer_bounds.h"
#include "present_managed_capacity.h"

namespace {
using namespace render;
constexpr uintptr_t block_slot_bytes = sizeof(uint64_t);
constexpr uint64_t capacity_word_bytes = sizeof(uint32_t);
constexpr uint32_t allocation_alignment = sizeof(uint64_t);

FinishBlockSlot *managed_capacity_block_slot(uintptr_t block, uint32_t index)
{
    const uintptr_t address = block + static_cast<uintptr_t>(index) * block_slot_bytes;
    return reinterpret_cast<FinishBlockSlot *>(address);
}

}  // namespace

extern "C" uint8_t bb_render_managed_present_capacity(render::CommandFinishStateView *buffer,
                                                      uint32_t, render::CommandFinishStateView *state)
{
    using namespace render;
    uintptr_t acquired_buffer = 0;
    CommandBufferSnapshot snapshot;

    PresentBufferPool *processor = state->item_processor;
    const uintptr_t begin = packet_begin(&buffer->buffer.commands);
    const uint64_t saved_record = capture_present_buffer_pool_record(processor, begin);
    const uint64_t slots = processor->slots.get();
    uint32_t pop_result = pop_present_buffer_pool(
        processor, &acquired_buffer);
    while (pop_result != 0) {
        managed_capacity_calls::managed_capacity_retry_step();
        pop_result = pop_present_buffer_pool(
            processor, &acquired_buffer);
    }

    const uint64_t capacity_words = (slots * block_slot_bytes - block_slot_bytes) /
                                    capacity_word_bytes;
    // Capture the acquired buffer before invoking the allocator.
    const uintptr_t acquired = acquired_buffer;
    const uint32_t link_index = state->block_link_index;
    const uint64_t allocation_bytes = static_cast<uint64_t>(link_index) * block_slot_bytes +
                                      block_slot_bytes;
    const bool had_head = state->block_head != 0;
    uint32_t count = 0;
    bool allocate_block = !had_head;
    if (had_head) {
        count = state->last_block_count;
        allocate_block = count == link_index;
    }

    uintptr_t block;
    if (allocate_block) {
        FinishBlockOwner *owner = state->block_owner;
        auto *owner_view = reinterpret_cast<ManagedCapacityOwnerView *>(owner);
        ManagedCapacityOwnerVtable *vtable = owner_view->vtable.get();
        const auto allocate = vtable->allocate.get();
        block = allocate(owner, allocation_bytes, allocation_alignment);
        if (block == 0) return 0;
        managed_capacity_block_slot(block, link_index)->value.set(0);
        if (had_head) {
            // Native reloads the tail after clearing the new block's link slot.
            const uintptr_t tail = state->block_tail;
            managed_capacity_block_slot(tail, link_index)->value.set(block);
        } else {
            state->block_head = block;
        }
        state->block_tail = block;
        state->last_block_count = 0;
        count = 0;
    } else {
        block = state->block_tail;
    }

    const uintptr_t slot_offset = static_cast<uintptr_t>(count) * block_slot_bytes;
    state->last_block_count = count + 1u;
    const uintptr_t slot_address = block + slot_offset;
    // The count publication precedes this admission; there is no rollback on zero.
    if (slot_address == 0) return 0;
    reinterpret_cast<FinishBlockSlot *>(slot_address)->value.set(saved_record);

    copy_finish_snapshot_reverse(snapshot, buffer->buffer);
    set_packet_begin(&buffer->buffer.commands, acquired);
    set_packet_cursor(&buffer->buffer.commands, acquired);
    set_packet_limit(&buffer->buffer.commands, acquired + capacity_words * capacity_word_bytes);
    const uint32_t flags = state->submission_flags;
    submit_present_finish_snapshot(state, buffer,
                                   &snapshot, flags);
    const uintptr_t fresh_cursor = packet_cursor(&buffer->buffer.commands);
    set_packet_begin(&buffer->buffer.commands, fresh_cursor);
    state->last_reset_cursor = fresh_cursor;
    return 1;
}
