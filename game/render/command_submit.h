// SPDX-License-Identifier: GPL-2.0-or-later
// Original command-buffer views shared by the submission delegates. GPU command contents
// remain opaque; these fields describe only the observed cursors, callbacks and counters.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "command_counter_wait.h"
#include "command_buffer_snapshot.h"
#include "runtime/original.h"

namespace render {

struct CommandSubmissionContext;
struct FinishBlockOwner;
struct FinishDrainNode;
struct PresentBufferPool;

// The finish pass forwards this embedded shader state without inspecting its contents.
struct CommandSubmissionShaderState {
    uint8_t unknown_0x0000[0xb368];
};
static_assert(sizeof(CommandSubmissionShaderState) == 0xb368);
constexpr uint32_t command_word_bytes = 4;


struct CommandSubmissionOwner {
    uint8_t unknown_0x0000[0xb728];
    CounterWaitState *counters;
};
static_assert(offsetof(CommandSubmissionOwner, counters) == 0xb728);

struct CommandSubmissionContext {
    CommandBufferSnapshot buffer;
    CommandSubmissionShaderState shader_section;
    uint8_t unknown_0xb3a8[0x28];
    void *node_manager;
    FinishDrainNode *bucket_origin;
    uint8_t unknown_0xb3e0[0x78];
    FinishDrainNode *single_head;
    uint32_t bucket_mask;
    uint8_t unknown_0xb464[0x2c];
    CommandSubmissionOwner *owner;
    uint8_t unknown_0xb498[0x20];
    uintptr_t last_reset_cursor;
    uint8_t unknown_0xb4c0[0x8];
    PresentBufferPool *item_processor;
    FinishBlockOwner *block_owner;
    uintptr_t block_head;
    uint64_t block_tail;
    uint32_t block_link_index;
    uint32_t last_block_count;
    uint8_t submission_flags;
};
static_assert(offsetof(CommandSubmissionContext, buffer) == 0x0);
static_assert(offsetof(CommandSubmissionContext, owner) == 0xb490);
static_assert(offsetof(CommandSubmissionContext, last_reset_cursor) == 0xb4b8);
static_assert(offsetof(CommandSubmissionContext, submission_flags) == 0xb4f0);

static_assert(offsetof(CommandSubmissionContext, shader_section) == 0x40);
static_assert(offsetof(CommandSubmissionContext, node_manager) == 0xb3d0);
static_assert(offsetof(CommandSubmissionContext, bucket_origin) == 0xb3d8);
static_assert(offsetof(CommandSubmissionContext, single_head) == 0xb458);
static_assert(offsetof(CommandSubmissionContext, bucket_mask) == 0xb460);
static_assert(offsetof(CommandSubmissionContext, item_processor) == 0xb4c8);
static_assert(offsetof(CommandSubmissionContext, block_owner) == 0xb4d0);
static_assert(offsetof(CommandSubmissionContext, block_head) == 0xb4d8);
static_assert(offsetof(CommandSubmissionContext, block_tail) == 0xb4e0);
static_assert(offsetof(CommandSubmissionContext, block_link_index) == 0xb4e8);
static_assert(offsetof(CommandSubmissionContext, last_block_count) == 0xb4ec);
static_assert(sizeof(CommandSubmissionContext) == 0xb4f8);

// A trivial aggregate of this size is passed in memory by the guest's AMD64 ABI, after the
// three register parameters. Padding has no reconstructed value; only named fields are initialized.
struct FlipSubmission {
    uint32_t video_out;
    uint32_t buffer_index;
    uint32_t mode;
    uint64_t flip_arg;
    uint8_t flags;
};
static_assert(std::is_trivially_copyable_v<FlipSubmission>);
static_assert(alignof(FlipSubmission) == 0x8 && sizeof(FlipSubmission) == 0x20);
static_assert(offsetof(FlipSubmission, video_out) == 0x0);
static_assert(offsetof(FlipSubmission, buffer_index) == 0x4);
static_assert(offsetof(FlipSubmission, mode) == 0x8);
static_assert(offsetof(FlipSubmission, flip_arg) == 0x10);
static_assert(offsetof(FlipSubmission, flags) == 0x18);

RT_ORIGINAL(0x01471270, initialize_command_buffer,
            void(CommandBufferSnapshot *, uintptr_t begin, uint32_t size,
                 PacketSpaceCallback callback, uintptr_t callback_argument));

RT_ORIGINAL(0x02ab0ce0, submit_command_batch,
            void(CommandSubmissionContext *, CommandSubmissionContext *,
                 const CommandBufferSnapshot *, FlipSubmission));

RT_ORIGINAL(0x02ab0e30, submit_snapshot,
            void(const FlipSubmission *, const CommandBufferSnapshot *));

}  // namespace render
