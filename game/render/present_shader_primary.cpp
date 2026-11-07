// SPDX-License-Identifier: GPL-2.0-or-later
// Populate sparse primary/optional bindings before forwarding the original command writers.
#include "present_shader_primary.h"

namespace render {
namespace {
void copy_sparse_shader_slots(ShaderPrimaryStageView &stage,
                              const ShaderPrimaryDescriptorView &descriptor)
{
    uint64_t mask = descriptor.masks[0].get();
    const uintptr_t table_address = descriptor.table_d8.get();
    const auto *table = reinterpret_cast<const Field<uint64_t> *>(table_address);
    for (std::size_t bank = 0; bank != primary_shader_bank_count; ++bank) {
        if (bank != 0) mask = descriptor.masks[bank].get();
        stage.masks[bank].set(mask);
        while (mask != 0) {
            const unsigned bit = std::countr_zero(mask);
            const uint8_t index = descriptor.maps[bank].indices[bit];
            const uint64_t value = table[index].get();
            stage.banks[bank].slots[bit].set(value);
            mask -= uint64_t{1} << bit;
        }
    }
}

uint64_t primary_shader_one_hot(uint8_t index)
{
    return index < primary_shader_slot_count ? uint64_t{1} << index : 0;
}

void update_shader_special_pair(ShaderPrimaryStageView &stage,
                                 const ShaderPrimaryDescriptorView &descriptor,
                                 ShaderPrimaryPair &destination)
{
    constexpr std::size_t last_bank = primary_shader_bank_count - 1;
    stage.dirty_28.high.set(primary_shader_invalid);
    const uint8_t first_selector = descriptor.maps[last_bank].indices[primary_shader_first_selector_index];
    stage.banks[last_bank].slots[primary_shader_first_one_hot_slot].set(
        primary_shader_one_hot(first_selector));
    const uint8_t second_selector = descriptor.maps[last_bank].indices[primary_shader_second_selector_index];
    stage.banks[last_bank].slots[primary_shader_second_one_hot_slot].set(
        primary_shader_one_hot(second_selector));
    if (descriptor.maps[last_bank].indices[primary_shader_special_flag_index] == 0 ||
        (stage.masks[last_bank].bytes[primary_shader_mask_flag_byte] & primary_shader_special_mask) == 0)
        return;
    const uint64_t low = descriptor.pair_f0.low.get();
    const uint64_t high = descriptor.pair_f0.high.get();
    destination.high.set(high);
    destination.low.set(low);
    const uint64_t additional = stage.banks[last_bank].slots[primary_shader_special_slot].get();
    // Capture the complete SIMD operand before either destination lane is changed.
    ShaderPrimaryPairValue pair;
    std::memcpy(&pair, &stage.dirty_28, sizeof(pair));
    pair.low |= primary_shader_dirty_bit;
    // The lane was set to all ones above; retain the original redundant merge.
    pair.high |= additional;
    std::memcpy(&stage.dirty_28, &pair, sizeof(pair));
}

const void *primary_shader_command_address(uintptr_t program)
{
    return reinterpret_cast<const void *>(program + offsetof(ShaderPrimaryProgramView, command_08));
}
}  // namespace
}  // namespace render

extern "C" void bb_render_configure_record_shader(render::ShaderPrimaryStateView *state,
                                                   render::PacketBuffer *commands,
                                                   const render::ShaderPrimaryDescriptorView *primary_descriptor,
                                                   const render::ShaderPrimaryDescriptorView *optional_descriptor)
{
    using namespace render;
    if (state->primary.flag_04.get() != 0) {
        packet_flag_byte(commands) &= static_cast<uint8_t>(~shader_packet_flag_mask);
        state->primary.flag_04.set(0);
    }
    copy_sparse_shader_slots(state->primary, *primary_descriptor);
    update_shader_special_pair(state->primary, *primary_descriptor, state->primary_pair_9bd0);
    set_vs_shader(commands, primary_shader_command_address(primary_descriptor->program_100.get()), 0);
    if (optional_descriptor == nullptr) {
        set_ps_shader(commands, nullptr);
    } else {
        const auto &optional = *optional_descriptor;
        copy_sparse_shader_slots(state->optional, optional);
        update_shader_special_pair(state->optional, optional, state->optional_pair_a0d0);
        set_ps_shader(commands, primary_shader_command_address(optional.program_100.get()));
        const auto *optional_program = reinterpret_cast<const ShaderPrimaryProgramView *>(
            optional.program_100.get());
        const uint32_t count = optional_program->count_38;
        if (count != 0) {
            const auto *primary_program = reinterpret_cast<const ShaderPrimaryProgramView *>(
                primary_descriptor->program_100.get());
            const uint32_t first_count = primary_program->count_25;
            const uint32_t first_index = primary_program->index_03;
            const uint32_t second_index = primary_program->index_24;
            const uint32_t optional_index = optional_program->index_03;
            const uintptr_t first = reinterpret_cast<uintptr_t>(primary_program) +
                offsetof(ShaderPrimaryProgramView, source_28) +
                uint64_t(first_index + second_index) * shader_packet_word_bytes;
            const uintptr_t second = reinterpret_cast<uintptr_t>(optional_program) +
                offsetof(ShaderPrimaryProgramView, source_3c) +
                uint64_t(optional_index) * shader_packet_word_bytes;
            Field<uint32_t> words[shader_link_word_capacity];
            build_shader_link_words(words, reinterpret_cast<const void *>(first), first_count,
                                     reinterpret_cast<const void *>(second), count);
            emit_shader_link_words(commands, words, count);
        }
    }
    state->primary.invalid_38.set(primary_shader_invalid);
    state->optional.invalid_38.set(primary_shader_invalid);
    state->invalid_25a8.set(primary_shader_invalid);
}
