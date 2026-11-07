// SPDX-License-Identifier: GPL-2.0-or-later
// Consume a prepared source record. One classification updates the tracked resource range;
// the other conditionally emits a complete command sequence and restores cached state.
// Original register/shader helpers remain opaque and their return values are ignored.
#include <algorithm>
#include <cstring>
#include <limits>

#include "present_record_calls.h"

namespace {
using namespace render;
using namespace render::record_calls;
constexpr uint32_t record_range_dirty = 2;
constexpr uint32_t all_record_dirty = 7;
constexpr uint8_t preparation_dirty = 0xf;
constexpr uint8_t resource_marker = 0xff;
constexpr uint8_t suppress_record_commands = 0x20;
constexpr uint32_t record_command_mode = 0x2e;
constexpr uint32_t last_register_slot = 7;
constexpr uint32_t record_mask = 0xf;
constexpr uint32_t state_mode_value = 0xcc;
constexpr uint32_t record_pipeline_mode = 0x11;
constexpr uint32_t record_step_mode = 3;
constexpr uint32_t prepared_slot_count = 8;
constexpr uint32_t source_state_mode = 2;
constexpr uint32_t restored_state_mode = 1;
constexpr uint8_t restored_preparation_flag = 1;

void track_record_range(PresentRecordContext *context, PresentRecordPayload *payload)
{
    const uintptr_t resource = payload->resource;
    const uint32_t flags = context->flags_b498;
    if (resource != 0) {
        const uint32_t extent = payload->extent_14;
        context->flags_b498 = flags | record_range_dirty;
        const uint32_t begin = record_range_begin(resource);
        const uint32_t end = begin + record_range_length(extent);
        context->minimum_b484 = std::min(context->minimum_b484, begin);
        context->maximum_b488 = std::max(context->maximum_b488, end);
        context->marker_b494 = resource_marker;
    } else {
        context->flags_b498 = flags | all_record_dirty;
    }
    context->flags_b478 |= preparation_dirty;
}

void configure_record_registers(PresentRecordContext *context, PresentRecordView *record,
                                RecordRegisterState &a, RecordRegisterState &b, RecordRegisterState &c)
{
    auto *commands = &context->commands;
    emit_event_write(commands, record_command_mode);
    const uint32_t first = record_first_parameter(record);
    const uint32_t second = record_second_parameter(record);
    configure_record_parameters(commands, 0, 0, first, second, record_scalar_parameter.get(), 0.0f);
    const uint32_t option = record_option_value(record);
    configure_record_option(commands, option != 0 ? 1 : 0, 0);

    initialize_record_register_a(&a);
    initialize_record_register_b(&b);
    initialize_record_register_c(&c);
    set_record_register_range(&c, 0, last_register_slot);
    set_record_register_enabled(&c, 1);
    emit_record_register_a(commands, 0, a.word);
    context->register_b568 = a.word;
    const uint64_t token_a = record_cache_token_a.get();
    context->token_b6e8 = token_a;
    set_cb_target_mask(commands, record_mask);
    context->mask_b560 = record_mask;
    context->token_b6e8 = token_a;
    set_db_depth_control(commands, c.word);
    context->register_b5a0 = c.word;
    const uint64_t token_b = record_cache_token_b.get();
    context->token_b6f0 = token_b;
    set_db_stencil_control(commands, b.word);
    context->register_b5a4 = b.word;
    context->token_b6f0 = token_b;
}
}  // namespace

extern "C" void bb_render_present_record_consume(render::PresentRecordContext *context,
                                                  render::PresentRecordView *record,
                                                  render::PresentRecordPayload *payload,
                                                  uint32_t first_index, uint32_t count,
                                                  uint32_t force_commands)
{
    using namespace render;
    using namespace render::record_calls;
    const uint8_t classification = record_classification_byte(record);
    const uint8_t secondary = record_secondary_byte(record);
    // The first classification is compared to exactly one, including noncanonical bytes.
    if (secondary != 0 || classification != 1) {
        track_record_range(context, payload);
        return;
    }
    if (static_cast<uint8_t>(force_commands) == 0 &&
        (payload->flags_25 & suppress_record_commands) != 0) return;

    auto *commands = &context->commands;
    auto *shader = &context->shader_state;
    auto *preparation = &context->preparation;
    RecordRegisterState a, b, c;
    configure_record_registers(context, record, a, b, c);
    select_preparation(preparation, commands, 0);
    set_cb_color_control(commands, source_state_mode, state_mode_value);
    // The context embeds shader state and commands; the primary pass reads its descriptor view.
    configure_record_shader(reinterpret_cast<ShaderPrimaryStateView *>(shader),
                            commands,
                            reinterpret_cast<const ShaderPrimaryDescriptorView *>(shared_present_descriptor.address()), nullptr);
    for (uint32_t slot = 1; slot < prepared_slot_count; ++slot)
        bind_prepared_record(preparation, commands, slot, nullptr);
    set_vgt_primitive_type(commands, record_pipeline_mode);
    emit_num_instances(commands, 1);
    configure_record_shader_secondary(reinterpret_cast<ShaderSecondaryState *>(shader), commands,
        reinterpret_cast<const ShaderSecondaryDescriptorView *>(shared_present_descriptor.address()), nullptr);

    // Copy only the observed 44-byte prefix; the last four bytes of local padding remain
    // uninitialized. Reuse that same mutable record across every requested index.
    alignas(16) PresentRecordView copy;
    std::memcpy(&copy, record, present_record_prefix_bytes);
    uint32_t index = first_index;
    for (uint32_t remaining = count; remaining != 0; --remaining, ++index) {
        set_present_record_range(&copy.fields, index, index);
        bind_prepared_record(preparation, commands, 0, &copy);
        emit_draw_index_auto(commands, record_step_mode);
    }
    finish_record_shader(shader, commands);
    // Native ORB changes only the low byte of this DWORD field.
    *reinterpret_cast<uint8_t *>(&context->flags_b498) |= all_record_dirty;
    context->flags_b478 |= preparation_dirty;
    context->sentinel_b548 = std::numeric_limits<uint64_t>::max();
    context->flags_b550 |= restored_preparation_flag;
    context->sentinel_b540 = std::numeric_limits<uint32_t>::max();
    set_cb_color_control(commands, restored_state_mode, state_mode_value);
    auto *targets = reinterpret_cast<TargetRestoreState *>(preparation);
    for (uint32_t slot = 0; slot < prepared_slot_count; ++slot)
        restore_color_target_from_bank(targets, commands, slot);
    restore_depth_target_from_bank(targets, commands);
    context->mode_b520 = record_pipeline_mode;
    context->pending_b518 = 0;
    context->sentinel_b524 = std::numeric_limits<uint32_t>::max();
}
