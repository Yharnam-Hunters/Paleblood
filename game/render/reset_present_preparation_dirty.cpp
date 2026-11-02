// SPDX-License-Identifier: GPL-2.0-or-later
// Emit the two dirty fields and cache their post-call values. The no-word route
// returns zero-extended flags; the word route retains the lower original's upper
// 48 RAX bits and replaces only AX. The known last-record caller ignores it.
#include "present_preparation_dirty.h"

namespace {
constexpr uint32_t scalar_dirty = 0x4;
constexpr uint32_t word_dirty = 0x8;
constexpr uint32_t clear_scalar_dirty = UINT32_MAX ^ scalar_dirty;
constexpr uint8_t clear_word_dirty = uint8_t(UINT8_MAX ^ word_dirty);
constexpr uint64_t upper_return_bits = UINT64_MAX ^ uint64_t(UINT16_MAX);
}

extern "C" uint64_t bb_render_reset_present_preparation_dirty(
    render::PreparationDirtyState *state, render::PreparationDirtyContext *context)
{
    using namespace render;
    uint32_t flags = state->dirty_188.get();
    uint64_t result = flags;
    if ((uint8_t(flags) & scalar_dirty) != 0) {
        preparation_dirty_calls::emit_scalar(&context->commands, state->scalar_f8.get());
        // The call may change either source or flags. Native code rereads both,
        // publishes the scalar cache first, then clears its bit with a DWORD update.
        state->cached_scalar_140.set(state->scalar_f8.get());
        flags = state->dirty_188.get() & clear_scalar_dirty;
        state->dirty_188.set(flags);
        result = flags;
    }
    if ((uint8_t(flags) & word_dirty) != 0) {
        // Input is one BYTE, but the post-call cache/AX reload is a full WORD.
        const uint32_t byte_value = state->word_fc.bytes[0];
        result = preparation_dirty_calls::emit_byte(&context->commands, byte_value);
        const uint16_t word = state->word_fc.get();
        result = (result & upper_return_bits) | uint64_t(word);
        state->cached_word_144.set(word);
        // This second clear touches only the low flags byte, preserving its
        // current upper bytes and every other bit changed by the lower helper.
        state->dirty_188.bytes[0] &= clear_word_dirty;
    }
    return result;
}
