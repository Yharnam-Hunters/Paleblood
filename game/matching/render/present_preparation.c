// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the presentation preparation state: restoring color and
// depth targets from a bank, and flushing the dirty depth and stencil clear values.
#include <stdint.h>

// The command packet buffer; only its leading pointers are known here.
typedef struct {
    uintptr_t begin;
    uintptr_t limit;
    uintptr_t cursor;
} PacketBuffer;

// A render target descriptor: a 16-byte header, then the payload the register writers take.
typedef struct {
    uint8_t unknown_0x00[0x10];
    uint8_t payload[1];
} TargetDescriptor;

enum { COLOR_TARGET_COUNT = 8, TARGET_BANK_COUNT = 2 };

typedef struct {
    TargetDescriptor *color_targets[COLOR_TARGET_COUNT];
    TargetDescriptor *depth_target;
} TargetBank;

typedef struct {
    uint8_t unknown_0x0000[0x60];
    TargetBank banks[TARGET_BANK_COUNT];
    uint8_t unknown_0x00f0[0x08];
    float depth_clear;
    uint16_t stencil_clear;
    uint8_t unknown_0x00fe[0x42];
    float cached_depth_clear;
    uint16_t cached_stencil_clear;
    uint8_t unknown_0x0146[0x42];
    uint32_t flags;
} PreparationState;

// The context that owns the command buffer.
typedef struct {
    uint8_t unknown_0x00[0x10];
    PacketBuffer commands;
} PreparationContext;

enum {
    FLAG_DEPTH_TARGET_BANK_SHIFT = 1,
    FLAG_DEPTH_DIRTY = 0x4,
    FLAG_STENCIL_DIRTY = 0x8,
};

#define NO_TARGET ((TargetDescriptor *)UINTPTR_MAX)

extern void render_set_render_target(PacketBuffer *commands, uint32_t slot, const void *payload);
extern void render_set_depth_render_target(PacketBuffer *commands, const void *payload);

// Write color target `slot` from the bank chosen by flag bit 0.
void render_restore_color_target_from_bank(PreparationState *state, PacketBuffer *commands,
                                           uint32_t slot)
{
    TargetDescriptor *target = state->banks[state->flags & 1].color_targets[slot];
    if (target == 0 || target == NO_TARGET)
        render_set_render_target(commands, slot, 0);
    else
        render_set_render_target(commands, slot, target->payload);
}

// Write the depth target from the bank chosen by flag bit 1.
void render_restore_depth_target_from_bank(PreparationState *state, PacketBuffer *commands)
{
    uint32_t bank = (state->flags >> FLAG_DEPTH_TARGET_BANK_SHIFT) & 1;
    TargetDescriptor *target = state->banks[bank].depth_target;
    if (target == 0 || target == NO_TARGET)
        render_set_depth_render_target(commands, 0);
    else
        render_set_depth_render_target(commands, target->payload);
}

// The repository's names for these two emitters; here they write the depth and stencil clear values.
extern void render_emit_present_preparation_scalar(PacketBuffer *commands, float depth);
extern void render_emit_present_preparation_byte(PacketBuffer *commands, uint8_t stencil);

// Write a dirty depth clear value and then a dirty stencil clear value, caching each written
// value and clearing its dirty flag after its writer returns. It matches as a void function with
// a float depth writer and a uint8_t stencil writer (the replacement's 64-bit result is the RAX
// the original happens to leave).
void render_reset_present_preparation_dirty(PreparationState *state, PreparationContext *context)
{
    if (state->flags & FLAG_DEPTH_DIRTY) {
        render_emit_present_preparation_scalar(&context->commands, state->depth_clear);
        state->cached_depth_clear = state->depth_clear;
        state->flags &= ~FLAG_DEPTH_DIRTY;
    }
    if (state->flags & FLAG_STENCIL_DIRTY) {
        render_emit_present_preparation_byte(&context->commands, (uint8_t)state->stencil_clear);
        state->cached_stencil_clear = state->stencil_clear;
        state->flags &= ~FLAG_STENCIL_DIRTY;
    }
}
