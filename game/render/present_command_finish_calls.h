// SPDX-License-Identifier: GPL-2.0-or-later
// Operations used to drain pending presentation command lists and release blocks.
#pragma once
#include "present_command_finish.h"

namespace render {
struct FinishDrainNode {
    uint8_t unknown_0x00[0x28];
    Field<FinishDrainNode *> next;
};
static_assert(offsetof(FinishDrainNode, next) == 0x28);
static_assert(sizeof(FinishDrainNode) == 0x30);
using FinishBlockRelease = void (*)(FinishBlockOwner *, void *block);
struct FinishBlockOwnerVtable {
    uint8_t unknown_0x00[0x70];
    Field<FinishBlockRelease> release;
};
static_assert(offsetof(FinishBlockOwnerVtable, release) == 0x70);
struct FinishBlockOwner {
    Field<FinishBlockOwnerVtable *> vtable;
};
static_assert(offsetof(FinishBlockOwner, vtable) == 0x00);
struct FinishBucketSlot { Field<FinishDrainNode *> head; };
static_assert(sizeof(FinishBucketSlot) == 0x08);
struct alignas(8) PresentCommandFinishFrame {
    Field<uint64_t> saved_bucket_index;
    CommandFinishStateView *command_argument;
    CommandBufferSnapshot snapshot;
};
static_assert(offsetof(PresentCommandFinishFrame, saved_bucket_index) == 0x00);
static_assert(offsetof(PresentCommandFinishFrame, command_argument) == 0x08);
static_assert(offsetof(PresentCommandFinishFrame, snapshot) == 0x10);
static_assert(sizeof(PresentCommandFinishFrame) == 0x50);
RT_ORIGINAL(0x02aaa5a0, release_present_finish_node,
            uintptr_t(void *manager, CommandFinishStateView **command_argument, FinishDrainNode *node));
RT_ORIGINAL(0x02acd3f0, prepare_present_finish_section,
            void(CommandSubmissionShaderState *, CommandFinishStateView *));
RT_ORIGINAL(0x02ad3c80, process_present_finish_item,
            uintptr_t(void *processor, CommandFinishStateView *, uintptr_t item));
RT_GLOBAL(0x057e43c8, present_finish_expected_callback, const uintptr_t);
}  // namespace render
