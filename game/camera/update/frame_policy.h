// SPDX-License-Identifier: GPL-2.0-or-later
// Apply the camera gains and escape behavior selected by the frame-rate option.
#pragma once
#include <bit>
#include "../../engine/vector_operations.h"
#include "../../frame_timing/flipper.h"
#include "../../frame_timing/target_fps.h"

namespace camera::update::frame_policy
{
using engine::Vec4;
using namespace engine::vector_operations;

RT_GLOBAL(0x04d26318, scalar_one, const float);
RT_GLOBAL(0x04d26364, thirty_interval, const float);
RT_GLOBAL(0x04d2632c, thirty_scale, const float);
constexpr float sixty_interval = 1.0f / 60.0f;
constexpr float sixty_frames_per_second = 60.0f;
constexpr float fifteen_interval = 1.0f / 15.0f;
constexpr float rational_gain_denominator = 2.0f;

inline bool control_edits_enabled()
{
    return frame_timing::target_fps() != frame_timing::Target::original;
}
inline bool measured_time_enabled()
{
    const auto policy = frame_timing::target_fps();
    return policy == frame_timing::Target::fps60 || policy == frame_timing::Target::uncapped;
}

inline float measured_frame_seconds()
{
    // Every cave reads this pointer and member anew, with the original unchecked dereference.
    const frame_timing::SprjFlipper *flipper = frame_timing::flipper_instance.get();
    return flipper->frame_seconds;
}
inline float capped_interval_thirty()
{
    const float measured = measured_frame_seconds();
    return minimum(measured, thirty_interval.get());
}
inline float scaled_interval_thirty()
{
    const float measured = measured_frame_seconds();
    const float capped = minimum(measured, thirty_interval.get());
    return multiply(capped, thirty_scale.get());
}
inline float scaled_interval_sixty()
{
    const float measured = measured_frame_seconds();
    const float capped = minimum(measured, sixty_interval);
    return multiply(capped, sixty_frames_per_second);
}

inline float standard_rational(float rate, float scale)
{
    const float complement = subtract(scalar_one.get(), scale);
    const float complemented_rate = multiply(complement, rate);
    const float scaled_rate = multiply(scale, rate);
    const float numerator = add(scaled_rate, scaled_rate);
    return divide(numerator, subtract(rational_gain_denominator, complemented_rate));
}
inline float parameter_gain(float rate)
{
    return measured_time_enabled() ? standard_rational(rate, scaled_interval_thirty()) : rate;
}
inline float standard_gain(float rate)
{
    return measured_time_enabled() ? standard_rational(rate, scaled_interval_sixty()) : rate;
}
inline float complement_gain(float rate)
{
    if (!measured_time_enabled()) return rate;
    const float complemented = subtract(scalar_one.get(), rate);
    const float corrected = standard_rational(complemented, scaled_interval_sixty());
    return subtract(scalar_one.get(), corrected);
}
inline Vec4 packed_standard_gain(Vec4 rates)
{
    if (!measured_time_enabled()) return rates;
    const Vec4 scale = broadcast(scaled_interval_sixty());
    const Vec4 complement = subtract(broadcast(scalar_one.get()), scale);
    const Vec4 complemented_rates = multiply(complement, rates);
    const Vec4 scaled_rates = multiply(scale, rates);
    const Vec4 numerator = add(scaled_rates, scaled_rates);
    const Vec4 denominator = subtract(broadcast(rational_gain_denominator), complemented_rates);
    return divide(numerator, denominator);
}
inline float blended_translation_gain(float rate)
{
    if (!measured_time_enabled()) return rate;
    const float scaled_rate = multiply(scaled_interval_sixty(), rate);
    const float numerator = add(scaled_rate, scaled_rate);
    const float difference = subtract(scaled_rate, rate);
    const float denominator = add(difference, rational_gain_denominator);
    return divide(numerator, denominator);
}
inline float distance_chase_gain(float rate)
{
    if (!measured_time_enabled()) return rate;
    const float scaled_rate = multiply(scaled_interval_sixty(), rate);
    const float difference = subtract(scaled_rate, rate);
    const float denominator = add(difference, rational_gain_denominator);
    const float numerator = add(scaled_rate, scaled_rate);
    return divide(numerator, denominator);
}
inline float direct_pitch_movement(float rounded_movement)
{
    return measured_time_enabled() ? multiply(rounded_movement, scaled_interval_sixty()) : rounded_movement;
}
inline float normal_chase_factor(float stock_factor)
{
    if (measured_time_enabled()) return capped_interval_thirty();
    return control_edits_enabled() ? fifteen_interval : stock_factor;
}
inline uint8_t both_wings_missed_mode()
{
    return control_edits_enabled() ? uint8_t{0} : uint8_t{1};
}
// The patched wall-disabled path copies escape rates after clearing escape mode,
// while skipping the yaw update.
inline bool wall_off_copies_escape_rates()
{
    return control_edits_enabled();
}

} // namespace camera::update::frame_policy
