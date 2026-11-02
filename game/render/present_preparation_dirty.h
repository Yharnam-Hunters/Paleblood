// SPDX-License-Identifier: GPL-2.0-or-later
// Two dirty preparation fields are emitted through original command helpers.
// Lower command formats and their opaque return bits remain original behavior.
#pragma once

#include "present_finish.h"

namespace render {
struct PreparationDirtyCommandPrefix { uint8_t unknown_0x00; };
struct PreparationDirtyContext {
    uint8_t unknown_0x0000[0x10];
    PreparationDirtyCommandPrefix commands;
};
static_assert(offsetof(PreparationDirtyContext, commands) == 0x10);

struct PreparationDirtyState {
    uint8_t unknown_0x0000[0xf8];
    PresentFinishField<float> scalar_f8;
    PresentFinishField<uint16_t> word_fc;
    uint8_t unknown_0x00fe[0x42];
    PresentFinishField<float> cached_scalar_140;
    PresentFinishField<uint16_t> cached_word_144;
    uint8_t unknown_0x0146[0x42];
    PresentFinishField<uint32_t> dirty_188;
};
static_assert(offsetof(PreparationDirtyState, scalar_f8) == 0xf8);
static_assert(offsetof(PreparationDirtyState, word_fc) == 0xfc);
static_assert(offsetof(PreparationDirtyState, cached_scalar_140) == 0x140);
static_assert(offsetof(PreparationDirtyState, cached_word_144) == 0x144);
static_assert(offsetof(PreparationDirtyState, dirty_188) == 0x188);
static_assert(sizeof(PreparationDirtyState) == 0x18c);

namespace preparation_dirty_calls {
// Observed caller profiles only: scalar in XMM0, and a zero-extended BYTE in
// ESI. The second call's full RAX is required by this helper's AX-only update.
RT_ORIGINAL(0x01473590, emit_scalar, void(PreparationDirtyCommandPrefix *, float));
RT_ORIGINAL(0x014735f0, emit_byte, uint64_t(PreparationDirtyCommandPrefix *, uint32_t));
}  // namespace preparation_dirty_calls
}  // namespace render
