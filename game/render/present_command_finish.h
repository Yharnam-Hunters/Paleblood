// SPDX-License-Identifier: GPL-2.0-or-later
// Pending presentation command lists, saved regions and owner/counter links
// used while finishing a presentation context.
#pragma once
#include "packet_buffer_bounds.h"

#include "command_reservation.h"
#include "command_counter_state.h"
#include "command_buffer_snapshot.h"

#include "runtime/original.h"

namespace render {
struct FinishDrainNode;
struct FinishBlockOwner;
struct FinishCommandSection {
    uint8_t unknown_0x0000[0xb360];
    Field<uint64_t> reset_word;
};
static_assert(offsetof(FinishCommandSection, reset_word) == 0xb360);
static_assert(sizeof(FinishCommandSection) == 0xb368);

struct FinishSubmissionOwner {
    uint8_t unknown_0x0000[0xb728];
    Field<CounterWaitState *> counters;
};
static_assert(offsetof(FinishSubmissionOwner, counters) == 0xb728);

inline void copy_finish_snapshot_reverse(CommandBufferSnapshot &out,
                                          const CommandBufferSnapshot &in)
{
    auto *destination = reinterpret_cast<uint8_t *>(&out);
    const auto *source = reinterpret_cast<const uint8_t *>(&in);
    for (std::size_t offset = sizeof(CommandBufferSnapshot);
         offset > offsetof(PacketBuffer, cursor);) {
        offset -= sizeof(uint64_t);
        uint64_t value;
        std::memcpy(&value, source + offset, sizeof(value));
        std::memcpy(destination + offset, &value, sizeof(value));
    }
    const uintptr_t begin = packet_begin(&in.commands);
    const uintptr_t limit = packet_limit(&in.commands);
    set_packet_limit(&out.commands, limit);
    set_packet_begin(&out.commands, begin);
}

struct CommandFinishStateView {
    CommandBufferSnapshot buffer;
    FinishCommandSection shader_section;
    uint8_t unknown_0xb3a8[0x28];
    Field<void *> node_manager;
    Field<FinishDrainNode *> bucket_origin;
    uint8_t unknown_0xb3e0[0x78];
    Field<FinishDrainNode *> single_head;
    Field<uint32_t> bucket_mask;
    uint8_t unknown_0xb464[0x2c];
    Field<FinishSubmissionOwner *> owner;
    uint8_t unknown_0xb498[0x20];
    Field<uintptr_t> last_reset_cursor;
    uint8_t unknown_0xb4c0[0x08];
    Field<void *> item_processor;
    Field<FinishBlockOwner *> block_owner;
    Field<uintptr_t> block_head;
    Field<uint64_t> block_tail;
    Field<uint32_t> block_link_index;
    Field<uint32_t> last_block_count;
    Field<uint8_t> submission_flags;
};
static_assert(offsetof(CommandFinishStateView, buffer) == 0x00);
static_assert(offsetof(CommandFinishStateView, shader_section) == 0x40);
static_assert(offsetof(CommandFinishStateView, node_manager) == 0xb3d0);
static_assert(offsetof(CommandFinishStateView, bucket_origin) == 0xb3d8);
static_assert(offsetof(CommandFinishStateView, single_head) == 0xb458);
static_assert(offsetof(CommandFinishStateView, bucket_mask) == 0xb460);
static_assert(offsetof(CommandFinishStateView, owner) == 0xb490);
static_assert(offsetof(CommandFinishStateView, last_reset_cursor) == 0xb4b8);
static_assert(offsetof(CommandFinishStateView, item_processor) == 0xb4c8);
static_assert(offsetof(CommandFinishStateView, block_owner) == 0xb4d0);
static_assert(offsetof(CommandFinishStateView, block_head) == 0xb4d8);
static_assert(offsetof(CommandFinishStateView, block_tail) == 0xb4e0);
static_assert(offsetof(CommandFinishStateView, block_link_index) == 0xb4e8);
static_assert(offsetof(CommandFinishStateView, last_block_count) == 0xb4ec);
static_assert(offsetof(CommandFinishStateView, submission_flags) == 0xb4f0);
static_assert(sizeof(CommandFinishStateView) == 0xb4f8);

struct FinishBlockSlot { Field<uintptr_t> value; };
static_assert(sizeof(FinishBlockSlot) == 0x08);

RT_ORIGINAL(0x02ab0b00, submit_present_finish_snapshot,
            void(CommandFinishStateView *, CommandFinishStateView *optional,
                 CommandBufferSnapshot *, uint32_t flags));
}  // namespace render
