// SPDX-License-Identifier: GPL-2.0-or-later
// Apply the camera gains and escape behavior selected by the frame-rate option.
#pragma once
#include <bit>
#include "../vector.h"
#include "../../frame_timing/flipper.h"
#include "../../frame_timing/target_fps.h"

namespace engine::camera_frame_policy
{

inline float add(float a, float b)
{
    float v;
    asm volatile("%{vex3%} vaddss %2, %1, %0" : "=x"(v) : "x"(a), "x"(b));
    return v;
}
inline float subtract(float a, float b)
{
    float v;
    asm volatile("%{vex3%} vsubss %2, %1, %0" : "=x"(v) : "x"(a), "x"(b));
    return v;
}
inline float multiply(float a, float b)
{
    float v;
    asm volatile("%{vex3%} vmulss %2, %1, %0" : "=x"(v) : "x"(a), "x"(b));
    return v;
}
inline float divide(float a, float b)
{
    float v;
    asm volatile("%{vex3%} vdivss %2, %1, %0" : "=x"(v) : "x"(a), "x"(b));
    return v;
}
inline float minimum(float a, float b)
{
    float v;
    asm volatile("%{vex3%} vminss %2, %1, %0" : "=x"(v) : "x"(a), "x"(b));
    return v;
}
inline Vec4 add(Vec4 a, Vec4 b)
{
    Vec4 v;
    asm volatile("%{vex3%} vaddps %2, %1, %0" : "=x"(v.v) : "x"(a.v), "x"(b.v));
    return v;
}
inline Vec4 subtract(Vec4 a, Vec4 b)
{
    Vec4 v;
    asm volatile("%{vex3%} vsubps %2, %1, %0" : "=x"(v.v) : "x"(a.v), "x"(b.v));
    return v;
}
inline Vec4 multiply(Vec4 a, Vec4 b)
{
    Vec4 v;
    asm volatile("%{vex3%} vmulps %2, %1, %0" : "=x"(v.v) : "x"(a.v), "x"(b.v));
    return v;
}
inline Vec4 divide(Vec4 a, Vec4 b)
{
    Vec4 v;
    asm volatile("%{vex3%} vdivps %2, %1, %0" : "=x"(v.v) : "x"(a.v), "x"(b.v));
    return v;
}
inline Vec4 broadcast(float value)
{
    return Vec4(_mm_set1_ps(value));
}
inline float capture(const float &source)
{
    float v;
    asm volatile("vmovss %1, %0" : "=x"(v) : "m"(source) : "memory");
    return v;
}
template <typename T> inline T *capture(T *const &source)
{
    T *v;
    asm volatile("movq %1, %0" : "=r"(v) : "m"(source) : "memory");
    return v;
}

RT_GLOBAL(0x04d26318, scalar_one, const float);
RT_GLOBAL(0x04d26364, thirty_interval, const float);
RT_GLOBAL(0x04d2632c, thirty_scale, const float);
constexpr uint32_t sixty_interval_bits = 0x3c888889;
constexpr uint32_t sixty_scale_bits = 0x42700000;
constexpr uint32_t fifteen_interval_bits = 0x3d888889;
constexpr uint32_t alternate_two_bits = 0x40000000;

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
    const frame_timing::SprjFlipper *flipper = capture(frame_timing::flipper_instance.get());
    return capture(flipper->frame_seconds);
}
inline float capped_interval_thirty()
{
    const float measured = measured_frame_seconds();
    return minimum(measured, capture(thirty_interval.get()));
}
inline float scaled_interval_thirty()
{
    const float measured = measured_frame_seconds();
    const float capped = minimum(measured, capture(thirty_interval.get()));
    return multiply(capped, capture(thirty_scale.get()));
}
inline float scaled_interval_sixty()
{
    const float measured = measured_frame_seconds();
    const float capped = minimum(measured, std::bit_cast<float>(sixty_interval_bits));
    return multiply(capped, std::bit_cast<float>(sixty_scale_bits));
}

inline float standard_rational(float rate, float scale)
{
    const float complement = subtract(capture(scalar_one.get()), scale);
    const float complemented_rate = multiply(complement, rate);
    const float scaled_rate = multiply(scale, rate);
    const float numerator = add(scaled_rate, scaled_rate);
    const float one = capture(scalar_one.get());
    const float two = add(one, one);
    return divide(numerator, subtract(two, complemented_rate));
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
    const float complemented = subtract(capture(scalar_one.get()), rate);
    const float corrected = standard_rational(complemented, scaled_interval_sixty());
    return subtract(capture(scalar_one.get()), corrected);
}
inline Vec4 packed_standard_gain(Vec4 rates)
{
    if (!measured_time_enabled()) return rates;
    const Vec4 scale = broadcast(scaled_interval_sixty());
    const Vec4 complement = subtract(broadcast(capture(scalar_one.get())), scale);
    const Vec4 complemented_rates = multiply(complement, rates);
    const Vec4 scaled_rates = multiply(scale, rates);
    const Vec4 numerator = add(scaled_rates, scaled_rates);
    const Vec4 one = broadcast(capture(scalar_one.get()));
    const Vec4 denominator = subtract(add(one, one), complemented_rates);
    return divide(numerator, denominator);
}
inline float blended_translation_gain(float rate)
{
    if (!measured_time_enabled()) return rate;
    const float scaled_rate = multiply(scaled_interval_sixty(), rate);
    const float numerator = add(scaled_rate, scaled_rate);
    const float difference = subtract(scaled_rate, rate);
    const float denominator = add(difference, std::bit_cast<float>(alternate_two_bits));
    return divide(numerator, denominator);
}
inline float distance_chase_gain(float rate)
{
    if (!measured_time_enabled()) return rate;
    const float scaled_rate = multiply(scaled_interval_sixty(), rate);
    const float difference = subtract(scaled_rate, rate);
    const float denominator = add(difference, std::bit_cast<float>(alternate_two_bits));
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
    return control_edits_enabled() ? std::bit_cast<float>(fifteen_interval_bits) : stock_factor;
}
inline uint8_t both_wings_missed_mode()
{
    return control_edits_enabled() ? uint8_t{0} : uint8_t{1};
}
// The patched wall-zero redirect reaches a test of retained R14b=0x94 (wall-global pointer),
// selecting the escape-rate copy branch even though it cleared escape mode and skipped yaw.
inline bool wall_off_copies_escape_rates()
{
    return control_edits_enabled();
}

} // namespace engine::camera_frame_policy
