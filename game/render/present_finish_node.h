// SPDX-License-Identifier: GPL-2.0-or-later
// Return a drained node to its manager, then emit its release packet. Head
// updates use the manager's existing compare/exchange helper.
#pragma once

#include "byte_field.h"
#include "present_command_finish.h"

#include "runtime/original.h"

namespace render {
struct FinishReleaseNode;

struct FinishNodeHead {
    Field<uintptr_t> value;
};
struct FinishNodeManager {
    uint8_t unknown_0x00[0x98];
    FinishNodeHead head;
};
struct FinishReleaseNode {
    Field<uint64_t> word_00;
    uint8_t unknown_0x08[0x20];
    Field<uintptr_t> next;
};
static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
static_assert(sizeof(FinishNodeHead) == 0x8);
static_assert(offsetof(FinishNodeManager, head) == 0x98);
static_assert(offsetof(FinishReleaseNode, word_00) == 0);
static_assert(offsetof(FinishReleaseNode, next) == 0x28);
static_assert(sizeof(FinishReleaseNode) == 0x30);

RT_ORIGINAL(0x0247ebc0, exchange_present_finish_node_head,
            uintptr_t(FinishNodeHead *, FinishReleaseNode *, uintptr_t expected));
RT_ORIGINAL(0x014778f0, emit_released_present_finish_node,
            uintptr_t(PacketBuffer *, uint32_t, uint32_t, FinishReleaseNode *,
                      uint32_t, uint32_t, uint32_t, uint32_t));

}  // namespace render
