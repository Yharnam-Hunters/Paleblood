// SPDX-License-Identifier: GPL-2.0-or-later
// Clear the conditional command flag before resetting the state. Sentinel stores
// deliberately surround each distinct original fill call, preserving partial
// publication and aliases rather than replacing this with one bulk reset.
#include "present_context_shader_reset.h"

namespace {
constexpr uint8_t retained_command_flags = 0xfd;
constexpr uint32_t reset_word = 0xffffffff;
constexpr uint32_t reset_byte_mask = 0xff;
constexpr uint64_t reset_qword = 0xffffffffffffffff;
}  // namespace

extern "C" void bb_render_reset_present_context_shader_state(
    render::ContextShaderResetState *state, render::PresentGpuContext *context)
{
    using namespace render;
    auto *commands = reinterpret_cast<ContextShaderResetCommands *>(context);
    void *section = commands->section_10;
    if (state->word_04.get() != 0) {
        commands->flags_38.set(commands->flags_38.get() & retained_command_flags);
        state->word_04.set(0);
    }
    context_shader_reset_calls::reset_section(section, context);
    state->word_00.set(reset_word);
    state->word_04.set(reset_word);
    state->word_08.set(reset_byte_mask);
    state->word_10.set(0);

    for (ContextShaderResetGroup &group : state->groups) {
        void *payload = group.payload;
        for (uint32_t index = ContextShaderResetGroup::words_extent_0 - 1;
             index-- != 0;) {
            group.words[index].set(reset_qword);
        }
        context_shader_reset_calls::c_memset(payload, 0, sizeof(group.payload));
        group.words[ContextShaderResetGroup::words_extent_0 - 1].set(reset_qword);
    }

    state->word_25a0.set(reset_word);
    state->word_25a8.set(reset_qword);
    void *first_block = state->block_25b0;
    void *second_block = state->block_26c0;
    context_shader_reset_calls::c_memset(first_block, 0, sizeof(state->block_25b0));
    context_shader_reset_calls::c_memset(second_block, 0, sizeof(state->block_26c0));
    state->word_28d8.set(0);
    state->word_28d0.set(0);
    context_shader_reset_calls::c_memset(
        state->block_a4a0, 0, sizeof(state->block_a4a0));
}
