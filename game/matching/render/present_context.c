// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the presentation context's finish node release, the
// release packet delegate and two context state resets.
#include <stdint.h>

// The command packet buffer; only its leading pointers are known here.
typedef struct {
    uintptr_t begin;
    uintptr_t limit;
    uintptr_t cursor;
} PacketBuffer;

extern uintptr_t emit_present_release_packet(PacketBuffer *buffer, uint32_t selector,
                                             uint32_t flags, uint64_t address, uint32_t mode,
                                             uint64_t data, uint32_t stack_selector,
                                             uint32_t stack_mode);
extern uint64_t compare_exchange_present_pool_marker(uint64_t *word, uint64_t desired,
                                                     uint64_t expected);

typedef struct FinishReleaseNode {
    uint64_t release_state;
    uint8_t unknown_0x08[0x20];
    struct FinishReleaseNode *next;
} FinishReleaseNode;

typedef struct {
    uint8_t unknown_0x00[0x98];
    uint64_t head;
} FinishNodeManager;

// The command submission state starts with its packet buffer.
typedef struct {
    PacketBuffer commands;
} CommandFinishState;

enum {
    NODE_RELEASED = 2,
    NODE_PACKET_SELECTOR = 0x28,
    NODE_PACKET_COUNT = 4,
    EVENT_DATA_SELECTION = 2,
    RELEASE_DELEGATE_MODE = 2,
};

// Push the node onto the manager's lock-free list, mark it released and emit its release packet.
uintptr_t render_release_present_finish_node(FinishNodeManager *manager,
                                             CommandFinishState **command_argument,
                                             FinishReleaseNode *node)
{
    uint64_t expected;
    do {
        expected = manager->head;
        node->next = (FinishReleaseNode *)expected;
    } while (compare_exchange_present_pool_marker(&manager->head, (uintptr_t)node, expected)
             != expected);
    node->release_state = NODE_RELEASED;
    return emit_present_release_packet(&(*command_argument)->commands, NODE_PACKET_SELECTOR, 0,
                                       (uintptr_t)node, 1, NODE_PACKET_COUNT, 0,
                                       EVENT_DATA_SELECTION);
}

// Emit a release packet with no flags and the fixed mode.
uintptr_t render_emit_present_release_delegate(PacketBuffer *buffer, uint32_t selector,
                                               uint64_t address, uint64_t data,
                                               uint32_t stack_selector)
{
    return emit_present_release_packet(buffer, selector, 0, address, RELEASE_DELEGATE_MODE, data,
                                       stack_selector, 0);
}

typedef struct {
    uint64_t value;
    uint64_t packed;
} ContextPairState;

#define PAIR_STATE_SENTINEL 0xffffffff00000000ull

// Clear the pair state: zero value, and the packed word with an all-ones high half.
void render_reset_present_context_state_b518(ContextPairState *state)
{
    state->value = 0;
    state->packed = PAIR_STATE_SENTINEL;
}

typedef struct {
    uint32_t invalid_00;
    uint32_t invalid_04;
    float scalar_08;
    float scalar_0c;
    float scalar_10;
    uint32_t state_14;
    uint32_t invalid_18;
    uint8_t unknown_0x1c[4];
    uint64_t invalid_20;
    uint64_t packed_28;
} ContextScalarState;

#define INVALID_WORD 0xffffffffu
#define INVALID_DOUBLE_WORD 0xffffffffffffffffull
#define PACKED_STATE_SENTINEL 0x0000ffff0000ffffull

extern const float render_context_reset_scalar;

// Reset the scalar state: invalid markers, the shared default scalar three times and zero state.
void render_reset_present_context_state_b528(ContextScalarState *state)
{
    float scalar;
    state->invalid_00 = INVALID_WORD;
    state->invalid_04 = INVALID_WORD;
    scalar = render_context_reset_scalar;
    state->scalar_08 = scalar;
    state->scalar_0c = scalar;
    state->scalar_10 = scalar;
    state->state_14 = 0;
    state->invalid_18 = INVALID_WORD;
    state->invalid_20 = INVALID_DOUBLE_WORD;
    state->packed_28 = PACKED_STATE_SENTINEL;
}
