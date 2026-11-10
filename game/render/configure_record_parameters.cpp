// SPDX-License-Identifier: GPL-2.0-or-later
// Build mutable transform triplets, retain each original configuration call, then
// derive the screen offset and guard-band adjustments from their live values.
#include "record_configuration_parameters.h"
#include "../engine/render_parameter_math.h"


extern "C" void bb_render_configure_viewport_and_guard_band(render::PacketBuffer *commands,
                                                       uint32_t x, uint32_t y, uint32_t end_x,
                                                       uint32_t end_y, float scalar_a,
                                                       float scalar_b)
{
    using namespace render;
    RecordConfigurationScratch scratch;
    const float delta_x = engine::signed32_to_float(record_configuration_signed_word(end_x - x));
    const float initial_half = record_configuration_half.get();
    scratch.scale.first.set(engine::multiply(delta_x, initial_half));
    const float delta_y = engine::signed32_to_float(record_configuration_signed_word(end_y - y));
    scratch.scale.second.set(engine::multiply(delta_y, record_configuration_negative_half.get()));
    scratch.scale.third.set(scalar_a);
    scratch.translation.first.set(engine::add(engine::u64_to_float(x), scratch.scale.first.get()));
    const float origin_y = engine::u64_to_float(y);
    const float positive_half_y = engine::multiply(delta_y, initial_half);
    scratch.translation.second.set(engine::add(origin_y, positive_half_y));
    scratch.translation.third.set(scalar_b);

    configure_viewport(commands, 0, &scratch.scale, &scratch.translation, end_y,
                               0.0f, record_configuration_one.get());
    initialize_pa_cl_vte_cntl(&scratch.register_state);
    set_pa_cl_vte_cntl(&scratch.register_state, 0);
    emit_pa_cl_vte_cntl(commands, scratch.register_state.word.get());
    configure_screen_scissor(commands, x, y, end_x, end_y);

    constexpr int32_t upper_bound = 512;
    constexpr int32_t largest_value = upper_bound - 1;
    const float sixteenth = record_configuration_sixteenth.get();
    const float product_x = engine::multiply(sixteenth, scratch.translation.first.get());
    const float half = record_configuration_half.get();
    const float rounded_x = engine::round_down(engine::add(product_x, half));
    int32_t clipped_x = engine::truncate(rounded_x);
    if (clipped_x >= upper_bound) clipped_x = largest_value;
    const float product_y = engine::multiply(sixteenth, scratch.translation.second.get());
    const float rounded_y = engine::round_down(engine::add(product_y, half));
    int32_t clipped_y = engine::truncate(rounded_y);
    if (clipped_y >= upper_bound) clipped_y = largest_value;
    set_screen_offset(commands, static_cast<uint32_t>(clipped_x),
                                   static_cast<uint32_t>(clipped_y));

    const uint32_t magnitude_mask = record_configuration_magnitude_mask.get();
    const float scale_x = scratch.scale.first.get();
    const float scale_y = scratch.scale.second.get();
    const float magnitude_x = mask_record_configuration_float(scale_x, magnitude_mask);
    const float center_x = scratch.translation.first.get();
    const float center_y = scratch.translation.second.get();
    float lower_x = engine::subtract(center_x, magnitude_x);
    constexpr uint32_t integer_shift = 4;
    const uint32_t shifted_x_word = static_cast<uint32_t>(clipped_x) << integer_shift;
    const float shifted_x = engine::signed32_to_float(record_configuration_signed_word(shifted_x_word));
    lower_x = engine::subtract(lower_x, shifted_x);
    const float margin = guard_band_lower_margin.get();
    lower_x = engine::add(lower_x, margin);
    const float limit = guard_band_upper_limit.get();
    float upper_x = engine::subtract(limit, magnitude_x);
    upper_x = engine::subtract(upper_x, center_x);
    upper_x = engine::add(shifted_x, upper_x);
    const float selected_x = engine::min_of(lower_x, upper_x);

    const float magnitude_y = mask_record_configuration_float(scale_y, magnitude_mask);
    float lower_y = engine::subtract(center_y, magnitude_y);
    const uint32_t shifted_y_word = static_cast<uint32_t>(clipped_y) << integer_shift;
    const float shifted_y = engine::signed32_to_float(record_configuration_signed_word(shifted_y_word));
    lower_y = engine::subtract(lower_y, shifted_y);
    lower_y = engine::add(lower_y, margin);
    float upper_y = engine::subtract(limit, magnitude_y);
    upper_y = engine::subtract(upper_y, center_y);
    upper_y = engine::add(shifted_y, upper_y);
    const float selected_y = engine::min_of(lower_y, upper_y);
    const float ratio_x = engine::divide(selected_x, magnitude_x);
    const float ratio_y = engine::divide(selected_y, magnitude_y);
    set_guard_band_clip_adjust(commands, ratio_x, ratio_y);
    const float one = record_configuration_one.get();
    set_guard_band_discard_adjust(commands, one, one);

}
