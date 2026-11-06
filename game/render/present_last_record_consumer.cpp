// SPDX-License-Identifier: GPL-2.0-or-later
// Consume the final record range, or update its tracking bank when commands are
// not admitted. Callers consume the state changes and do not use a return value.
#include "present_last_record_consumer.h"
#include "present_record_calls.h"

namespace render {
namespace {
using namespace record_calls;
constexpr uint8_t suppress_commands = 0x20;
constexpr uint32_t resource_range_changed = 0x2;
constexpr uint32_t all_ranges_changed = 0x7;
constexpr uint8_t all_record_banks_changed = 0xf;
constexpr uint8_t range_marker = 0x1;
constexpr uint32_t command_begin_mode = 0x2c;
constexpr uint32_t state_mode_value = 0xcc;
constexpr uint32_t pipeline_mode = 0x11;
constexpr uint32_t record_step_mode = 0x3;
constexpr uint32_t preparation_slot_count = 0x8;
constexpr uint32_t parameter_half_bits = 0x10;
constexpr uint32_t parameter_half_mask = 0xffff;
constexpr uint32_t unset_word = UINT32_MAX;
constexpr uint64_t unset_address = UINT64_MAX;

void track_last_record_range(LastPresentRecordContext *context,
                            const LastPresentRecordPayload *payload)
{
    const uintptr_t resource = payload->resource.get();
    const uint32_t old_flags = context->flags_b498.get();
    if (resource != 0) {
        const uint32_t extent = payload->extent_14.get();
        context->flags_b498.set(old_flags | resource_range_changed);
        const uint32_t begin = record_range_begin(resource);
        const uint32_t end = record_range_length(uintptr_t(extent)) + begin;
        const uint32_t minimum = context->minimum_b48c.get();
        context->minimum_b48c.set(minimum < begin ? minimum : begin);
        const uint32_t maximum = context->maximum_b490.get();
        context->maximum_b490.set(maximum > end ? maximum : end);
        context->marker_b495.set(context->marker_b495.get() | range_marker);
    } else {
        context->flags_b498.set(old_flags | all_ranges_changed);
    }
    context->flags_b478.set(context->flags_b478.get() | all_record_banks_changed);
}

void copy_last_record(LastPresentRecordView &destination,
                      const LastPresentRecordView *source)
{
    // The high tail is copied first. The final two reads both precede either
    // low store. Only the record bytes are part of the helper's borrowed local.
    destination.parameters_30.set(source->parameters_30.get());
    for (uint32_t index = LastPresentRecordView::word_count; --index > 1;) {
        destination.words[index].set(source->words[index].get());
    }
    const uint64_t first = source->words[0].get();
    const uint64_t second = source->words[1].get();
    destination.words[1].set(second);
    destination.words[0].set(first);
}
}  // namespace
}  // namespace render

extern "C" void bb_render_present_last_record_consume(
    render::LastPresentRecordContext *context,
    render::LastPresentRecordView *record,
    render::LastPresentRecordPayload *payload,
    uint32_t first_index, uint32_t count, uint32_t force)
{
    using namespace render;
    using namespace render::record_calls;
    RecordRegisterState restore_mode;
    RecordRegisterState mode;
    RecordRegisterState register_b;
    RecordRegisterState register_c;
    LastPresentRecordView copy;
    if (last_record_calls::classify_record(record) == 0) {
        track_last_record_range(context, payload);
        return;
    }
    if (uint8_t(force) == 0 &&
        (payload->flags_25.get() & suppress_commands) != 0) {
        return;
    }

    PacketBuffer *const commands = &context->commands;
    RecordShaderState *const shader = &context->shader_state;
    RecordPreparationState *const preparation = &context->preparation;
    last_record_calls::reset_preparation(preparation, context);
    emit_event_write(commands, command_begin_mode);

    const uint32_t parameters = record->parameters_30.get();
    const uint32_t first_parameter = parameters & parameter_half_mask;
    const uint32_t second_parameter = parameters >> parameter_half_bits;
    const float scalar = last_record_calls::scalar_parameter.get();
    configure_record_parameters(commands, 0, 0, first_parameter, second_parameter,
                                scalar, 0.0f);
    const uint32_t enabled = last_record_calls::record_option(record) != 0;
    configure_record_option(commands, enabled, 0);

    initialize_record_register_c(&register_c);
    initialize_record_register_b(&register_b);
    set_db_depth_control(commands, register_c.word);
    context->register_b5a0.set(register_c.word);
    const uint64_t token = record_cache_token_b.get();
    context->token_b6f0.set(token);
    set_db_stencil_control(commands, register_b.word);
    context->register_b5a4.set(register_b.word);
    context->token_b6f0.set(token);

    for (uint32_t slot = 0; slot < preparation_slot_count; ++slot) {
        bind_prepared_record(preparation, commands, slot, nullptr);
    }
    set_cb_color_control(commands, 0, state_mode_value);
    last_record_calls::initialize_mode(&mode);
    last_record_calls::set_first_mode(&mode, 1);
    last_record_calls::set_second_mode(&mode, 1);
    last_record_calls::emit_mode(commands, mode.word);

    configure_record_shader(reinterpret_cast<ShaderPrimaryStateView *>(shader), commands,
                            reinterpret_cast<const ShaderPrimaryDescriptorView *>(shared_present_descriptor.address()), nullptr);
    set_vgt_primitive_type(commands, pipeline_mode);
    emit_num_instances(commands, 1);
    configure_record_shader_secondary(reinterpret_cast<ShaderSecondaryState *>(shader), commands,
        reinterpret_cast<const ShaderSecondaryDescriptorView *>(shared_present_descriptor.address()), nullptr);

    copy_last_record(copy, record);
    uint32_t index = first_index;
    uint32_t remaining = count;
    while (remaining != 0) {
        last_record_calls::set_record_index(&copy, index, index);
        select_preparation(preparation, commands, &copy);
        emit_draw_index_auto(commands, record_step_mode);
        ++index;
        --remaining;
    }

    finish_record_shader(shader, commands);
    // Native OR touches only the low byte here, unlike tracking's DWORD store.
    context->flags_b498.bytes[0] |= uint8_t(all_ranges_changed);
    context->flags_b478.set(context->flags_b478.get() | all_record_banks_changed);
    context->sentinel_b548.set(unset_address);
    context->flags_b550.set(context->flags_b550.get() | range_marker);
    context->sentinel_b540.set(unset_word);
    set_cb_color_control(commands, 1, state_mode_value);
    last_record_calls::initialize_mode(&restore_mode);
    last_record_calls::emit_mode(commands, restore_mode.word);

    auto *targets = reinterpret_cast<TargetRestoreState *>(preparation);
    for (uint32_t slot = 0; slot < preparation_slot_count; ++slot) {
        restore_color_target_from_bank(targets, commands, slot);
    }
    restore_depth_target_from_bank(targets, commands);
    context->mode_b520.set(pipeline_mode);
    context->pending_b518.set(0);
    context->sentinel_b524.set(unset_word);
}
