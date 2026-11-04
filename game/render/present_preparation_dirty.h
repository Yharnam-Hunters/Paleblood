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
    Field<float> depth_clear;
    Field<uint16_t> stencil_clear;
    uint8_t unknown_0x00fe[0x42];
    Field<float> cached_depth_clear;
    Field<uint16_t> cached_stencil_clear;
    uint8_t unknown_0x0146[0x42];
    Field<uint32_t> dirty_188;
};
static_assert(offsetof(PreparationDirtyState, depth_clear) == 0xf8);
static_assert(offsetof(PreparationDirtyState, stencil_clear) == 0xfc);
static_assert(offsetof(PreparationDirtyState, cached_depth_clear) == 0x140);
static_assert(offsetof(PreparationDirtyState, cached_stencil_clear) == 0x144);
static_assert(offsetof(PreparationDirtyState, dirty_188) == 0x188);
static_assert(sizeof(PreparationDirtyState) == 0x18c);

}  // namespace render
