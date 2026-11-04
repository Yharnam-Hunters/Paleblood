// SPDX-License-Identifier: GPL-2.0-or-later
// Two dirty preparation fields are emitted through original command helpers.
// The caller caches values only after each writer has returned.
#pragma once

#include "command_reservation.h"
#include "present_preparation_calls.h"

namespace render {
struct PreparationDirtyContext {
    uint8_t unknown_0x0000[0x10];
    PacketBuffer commands;
};
static_assert(offsetof(PreparationDirtyContext, commands) == 0x10);

struct PreparationDirtyState {
    uint8_t unknown_0x0000[0xf8];
    Field<float> scalar_f8;
    Field<uint16_t> word_fc;
    uint8_t unknown_0x00fe[0x42];
    Field<float> cached_scalar_140;
    Field<uint16_t> cached_word_144;
    uint8_t unknown_0x0146[0x42];
    Field<uint32_t> dirty_188;
};
static_assert(offsetof(PreparationDirtyState, scalar_f8) == 0xf8);
static_assert(offsetof(PreparationDirtyState, word_fc) == 0xfc);
static_assert(offsetof(PreparationDirtyState, cached_scalar_140) == 0x140);
static_assert(offsetof(PreparationDirtyState, cached_word_144) == 0x144);
static_assert(offsetof(PreparationDirtyState, dirty_188) == 0x188);
static_assert(sizeof(PreparationDirtyState) == 0x18c);

}  // namespace render
