// SPDX-License-Identifier: GPL-2.0-or-later
// Update dirty depth and stencil clear state for the last-record path. Cache each
// field after its writer returns. The result keeps the second writer's upper bits
// and replaces its low word with the current stencil value.
#include "present_preparation_dirty.h"

namespace {
constexpr uint32_t depth_dirty = 0x4;
constexpr uint32_t stencil_dirty = 0x8;
constexpr uint32_t clear_depth_dirty = UINT32_MAX ^ depth_dirty;
constexpr uint8_t clear_stencil_dirty = uint8_t(UINT8_MAX ^ stencil_dirty);
constexpr uint64_t upper_return_bits = UINT64_MAX ^ uint64_t(UINT16_MAX);
}

extern "C" uint64_t bb_render_reset_present_preparation_dirty(
    render::PreparationDirtyState *state, render::PreparationDirtyContext *context)
{
    using namespace render;
    uint32_t flags = state->dirty_188.get();
    uint64_t result = flags;
    if ((uint8_t(flags) & depth_dirty) != 0) {
        render_emit_present_preparation_scalar(&context->commands, state->depth_clear.get());
        // The writer can change the source or flags. Cache the new value before
        // clearing its dirty bit, while preserving every other changed flag.
        state->cached_depth_clear.set(state->depth_clear.get());
        flags = state->dirty_188.get() & clear_depth_dirty;
        state->dirty_188.set(flags);
        result = flags;
    }
    if ((uint8_t(flags) & stencil_dirty) != 0) {
        // Pass the low byte, then cache both current bytes after the writer returns.
        const uint32_t byte_value = state->stencil_clear.bytes[0];
        result = render_emit_present_preparation_byte(&context->commands, byte_value);
        const uint16_t word = state->stencil_clear.get();
        result = (result & upper_return_bits) | uint64_t(word);
        state->cached_stencil_clear.set(word);
        // This second clear touches only the low flags byte, preserving its
        // current upper bytes and every other bit changed by the lower helper.
        state->dirty_188.bytes[0] &= clear_stencil_dirty;
    }
    return result;
}
