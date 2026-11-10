// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera locked geometry stage, included only by update.cpp.
#pragma once
#include "math.h"
// Locked geometry hands the resulting orientation back to rotation release.
#include "angular_mode.h"

namespace camera::update::locked_geometry
{
namespace packed = camera::update::math;
namespace scalar = camera::update::math;
namespace arithmetic = camera::update::math;
namespace angular_math = camera::update::math;
namespace locked_math = camera::update::math;
namespace root = camera::update::root;
using update::angular_mode::AngularContinuation;
using update::angular_mode::AngularPreparation;
using update::target::TargetPreparation;

static_assert(offsetof(FollowCamera, vertical_fov) == 0x50);
static_assert(offsetof(FollowCamera, orbit_origin) == 0xd0);
static_assert(offsetof(FollowCamera, angles) == 0x140);
static_assert(offsetof(FollowCamera, maximum_pitch) == 0x1ec);
static_assert(offsetof(FollowCamera, minimum_pitch) == 0x1f0);
static_assert(offsetof(FollowCamera, maximum_lock_pitch) == 0x1f4);
static_assert(offsetof(FollowCamera, minimum_lock_pitch) == 0x1f8);
static_assert(offsetof(FollowCamera, rotation_range_height_begin) == 0x1fc);
static_assert(offsetof(FollowCamera, rotation_range_height_end) == 0x200);
static_assert(offsetof(FollowCamera, lock_rotation_chase_rate) == 0x240);
static_assert(offsetof(FollowCamera, lock_rotation_dead_angle) == 0x244);
static_assert(offsetof(FollowCamera, lock_pitch_shift_ratio) == 0x248);
static_assert(offsetof(FollowCamera, lock_target) == 0x250);
static_assert(offsetof(FollowCamera, front_lock_active) == 0x263);
static_assert(offsetof(FollowCamera, front_lock_angles) == 0x26c);
static_assert(offsetof(FollowCamera, update_target_pitch_at_lock) == 0x274);

RT_GLOBAL(0x04cf2530, odd_coefficients_1_to_7, const Vec4);
RT_GLOBAL(0x04cf2540, odd_coefficients_9_to_15, const Vec4);
RT_GLOBAL(0x04cf2550, odd_coefficients_17_to_23, const Vec4);
RT_GLOBAL(0x04cf2560, ratio_coefficients_quadratic, const Vec4);
RT_GLOBAL(0x04cf2570, ratio_coefficients_linear, const Vec4);
RT_GLOBAL(0x04cf2580, ratio_coefficients_constant, const Vec4);
RT_GLOBAL(0x04cf2590, pair_sign_mask, const Vec4);
RT_GLOBAL(0x04cf25a0, pair_pi_bits, const Vec4);
RT_GLOBAL(0x04cf25b0, pair_two_pi, const Vec4);
RT_GLOBAL(0x04d25e74, two_pi, const float);
RT_GLOBAL(0x04d25e78, half_pi, const float);
RT_GLOBAL(0x04d25e7c, ratio_root_bias, const float); // Just above 1; bits 0x3f800001.

// This first reduction adds signed pi before the angle; later wraps reverse the
// operands. Keep that distinction when either operand is NaN.
float reduce_shift(float angle, float period)
{
    const float biased = arithmetic::add(locked_math::signed_pi(angle), angle);
    const float quotient = scalar::divide(biased, period);
    const float integral = locked_math::truncate_32_to_float(quotient);
    return scalar::subtract(angle, scalar::multiply(integral, period));
}

float wrap_angle(float angle, float period)
{
    const float biased = arithmetic::add(angle, locked_math::signed_pi(angle));
    const float quotient = scalar::divide(biased, period);
    const float integral = locked_math::truncate_32_to_float(quotient);
    return scalar::subtract(angle, scalar::multiply(integral, period));
}

// Keep the explicit powers and two pairwise sums: reassociation changes rounding and NaNs.
// The polynomial approximates the angular shift rather than calling libm.
float evaluate_shift_polynomial(float reduced_shift, float one)
{
    const float squared = scalar::multiply(reduced_shift, reduced_shift);
    const float cubed = scalar::multiply(reduced_shift, squared);
    const Vec4 basis = locked_math::lanes(one, reduced_shift, squared, cubed);
    const Vec4 shift = packed::broadcast<1>(basis);
    const Vec4 powers_1_to_7 = packed::multiply(packed::multiply(basis, basis), shift);
    const Vec4 power_8 = packed::multiply(shift, packed::broadcast<3>(powers_1_to_7));
    const Vec4 powers_9_to_15 = packed::multiply(powers_1_to_7, power_8);
    const Vec4 powers_17_to_23 = packed::multiply(power_8, powers_9_to_15);
    const float first = locked_math::sum_four_scalar(packed::multiply(powers_1_to_7, odd_coefficients_1_to_7.get()));
    const float second =
        locked_math::sum_four_scalar(packed::multiply(powers_9_to_15, odd_coefficients_9_to_15.get()));
    const float third =
        locked_math::sum_four_scalar(packed::multiply(powers_17_to_23, odd_coefficients_17_to_23.get()));
    return arithmetic::add(arithmetic::add(first, second), third);
}

// This path requires ordered lock_length > scaled_offset.
// That one-sided branch does not establish an absolute-domain bound for arbitrary inputs.
float evaluate_ratio_polynomial(float scaled_offset, float lock_length, float one)
{
    const float ratio = scalar::divide(scaled_offset, lock_length);
    const float magnitude = locked_math::absolute(ratio);
    const float magnitude_product = scalar::multiply(magnitude, ratio);
    const float third_product = scalar::multiply(magnitude_product, ratio);
    const float remainder = scalar::subtract(ratio, magnitude_product);
    const float radicand = scalar::subtract(ratio_root_bias.get(), magnitude);
    // The correction divides by the root of the remaining bias after the ratio magnitude.
    const float denominator = angular_math::square_root(radicand);
    const float corrected = scalar::divide(remainder, denominator);
    const Vec4 first = locked_math::lanes(third_product, one, third_product, one);
    const Vec4 second = locked_math::lanes(corrected, corrected, ratio, ratio);
    const Vec4 weights = packed::multiply(first, second);
    const Vec4 absolute_ratio = arithmetic::broadcast(magnitude);
    const Vec4 quadratic = packed::multiply(absolute_ratio, ratio_coefficients_quadratic.get());
    const Vec4 linear = packed::add(quadratic, ratio_coefficients_linear.get());
    const Vec4 product = packed::multiply(absolute_ratio, linear);
    const Vec4 coefficients = packed::add(product, ratio_coefficients_constant.get());
    return locked_math::sum_four_scalar(packed::multiply(coefficients, weights));
}

Vec4 refined_distance(Vec4 difference)
{
    const Vec4 squares = packed::multiply(difference, difference);
    const Vec4 xy = packed::add(packed::broadcast<0>(squares), packed::broadcast<1>(squares));
    const Vec4 length_squared = packed::add(packed::broadcast<2>(squares), xy);
    return root::refined_length(length_squared, root::FirstProductOrder::reciprocal_then_length);
}

// Convert through a signed 64-bit integer, then convert its low 32 bits to float.
// NaNs and large quotients differ from a direct signed 32-bit conversion.
float wrapped_movement_squared(float pitch_movement, float yaw_movement, float period)
{
    const Vec4 movement = locked_math::lanes(pitch_movement, yaw_movement, 0.0f, 0.0f);
    const Vec4 signed_pis = locked_math::signed_pi_pairs(movement, pair_sign_mask.get(), pair_pi_bits.get());
    const Vec4 biased = packed::add(movement, signed_pis);
    // Original divides Y before X, then converts X before Y.
    const float yaw_quotient = scalar::divide(biased.y(), period);
    const float pitch_quotient = scalar::divide(biased.x(), period);
    const Vec4 converted = locked_math::signed64_pair_low_words(pitch_quotient, yaw_quotient);
    const Vec4 turns = packed::multiply(converted, pair_two_pi.get());
    const Vec4 wrapped = packed::subtract(movement, turns);
    const Vec4 squares = packed::multiply(wrapped, wrapped);
    return arithmetic::add(squares.x(), squares.y());
}

float pitch_or_captured(Vec4 direction, float captured_pitch)
{
    const float x_squared = scalar::multiply(direction.x(), direction.x());
    const float z_squared = scalar::multiply(direction.z(), direction.z());
    const float sum = arithmetic::add(x_squared, z_squared);
    // A zero horizontal and vertical displacement retains the captured pitch.
    const float horizontal = angular_math::square_root(sum);
    if (!packed::ordered_zero(direction.y()) || !packed::ordered_zero(horizontal))
    {
        return angular_math::xor_low(atan2_float(direction.y(), horizontal),
                                     update::angular_mode::pitch_sign_bits.get());
    }
    return captured_pitch;
}

float yaw_or_captured(Vec4 direction, float captured_yaw)
{
    if (!packed::ordered_zero(direction.x()) || !packed::ordered_zero(direction.z()))
    {
        return atan2_float(direction.x(), direction.z());
    }
    return captured_yaw;
}

AngularPreparation prepare_locked_geometry(FollowCamera &state, const TargetPreparation &target)
{
    // This entry is selected only by AngularContinuation::locked_geometry. All vectors below
    // are snapshots across up to four original atan2 callbacks, not graph/guest rereads.
    const Vec4 lock_target = state.lock_target;
    const Vec4 orbit = state.orbit_origin;
    const Vec4 lock_difference = packed::subtract(lock_target, orbit);
    const float original_pitch = state.angles.pitch;
    const float desired_pitch = pitch_or_captured(lock_difference, original_pitch);
    // Original yaw capture is after the first possible callback, before the second.
    const float original_yaw = state.angles.yaw;
    const float desired_yaw = yaw_or_captured(lock_difference, original_yaw);
    const Vec4 pivot_difference = packed::subtract(orbit, target.fulcrum);
    const float pivot_pitch = pitch_or_captured(pivot_difference, original_pitch);
    const float pivot_yaw = yaw_or_captured(pivot_difference, original_yaw);

    const float fov = state.vertical_fov;
    const float shift_ratio = state.lock_pitch_shift_ratio;
    const float shift =
        scalar::multiply(scalar::multiply(fov, shift_ratio), update::target::half_fov.get());

    const Vec4 lock_length = refined_distance(lock_difference);
    const Vec4 pivot_length = refined_distance(pivot_difference);
    const float period = two_pi.get();
    const float reduced = reduce_shift(shift, period);
    const float one = update::target::scalar_one.get();
    const float shift_polynomial = evaluate_shift_polynomial(reduced, one);
    const float scaled_offset = scalar::multiply(pivot_length.x(), shift_polynomial);
    float offset_angle;
    if (scalar::unordered_or_less_equal(lock_length.x(), scaled_offset))
    {
        offset_angle = half_pi.get();
    }
    else
    {
        offset_angle = evaluate_ratio_polynomial(scaled_offset, lock_length.x(), one);
    }

    const float absolute_height = locked_math::absolute(lock_difference.y());
    const float begin = state.rotation_range_height_begin;
    const float end = state.rotation_range_height_end;
    const float height_above_begin = scalar::subtract(absolute_height, begin);
    const float height_span = scalar::subtract(end, begin);
    const float height_ratio = scalar::divide(height_above_begin, height_span);
    float height_blend = 0.0f;
    if (scalar::unordered_or_less_equal(0.0f, height_ratio))
    {
        height_blend = locked_math::minimum(one, height_ratio);
    }
    const float minimum_lock = state.minimum_lock_pitch;
    const float minimum = state.minimum_pitch;
    const float lower = arithmetic::add(
        minimum_lock, scalar::multiply(height_blend, scalar::subtract(minimum, minimum_lock)));
    const float shifted_offset = arithmetic::add(shift, offset_angle);
    const float requested_pitch = arithmetic::add(desired_pitch, shifted_offset);
    float selected_pitch = lower;
    if (scalar::unordered_or_less_equal(lower, requested_pitch))
    {
        const float maximum = state.maximum_pitch;
        const float maximum_lock = state.maximum_lock_pitch;
        const float upper = arithmetic::add(
            maximum_lock, scalar::multiply(height_blend, scalar::subtract(maximum, maximum_lock)));
        selected_pitch = locked_math::minimum(upper, requested_pitch);
    }

    const float pitch_delta = wrap_angle(scalar::subtract(selected_pitch, pivot_pitch), period);
    const float chase_rate =
        camera::update::frame_policy::standard_gain(state.lock_rotation_chase_rate);
    float pitch = wrap_angle(arithmetic::add(pivot_pitch, scalar::multiply(chase_rate, pitch_delta)), period);
    state.angles.pitch = pitch;
    const float yaw_delta = wrap_angle(scalar::subtract(desired_yaw, pivot_yaw), period);
    float yaw = wrap_angle(arithmetic::add(pivot_yaw, scalar::multiply(chase_rate, yaw_delta)), period);
    state.angles.yaw = yaw;
    const float pitch_error_squared = scalar::multiply(pitch_delta, pitch_delta);
    const float yaw_error_squared = scalar::multiply(yaw_delta, yaw_delta);
    const float error_squared = arithmetic::add(pitch_error_squared, yaw_error_squared);
    const float dead_angle = state.lock_rotation_dead_angle;
    const float dead_squared = scalar::multiply(dead_angle, dead_angle);
    if (!scalar::unordered_or_less_equal(dead_squared, error_squared))
    {
        state.angles.pitch = pivot_pitch;
        state.angles.yaw = pivot_yaw;
        pitch = pivot_pitch;
        yaw = pivot_yaw;
    }
    else
    {
        const float pitch_movement = scalar::subtract(pitch, pivot_pitch);
        const float yaw_movement = scalar::subtract(yaw, pivot_yaw);
        const float movement_squared = wrapped_movement_squared(pitch_movement, yaw_movement, period);
        if (!scalar::unordered_or_less_equal(dead_squared, movement_squared))
        {
            // Normalize by the full angular error, including both pitch and yaw.
            const float length = angular_math::square_root(error_squared);
            const float inverse_length = scalar::divide(one, length);
            const float pitch_unit = scalar::multiply(pitch_delta, inverse_length);
            const float pitch_removed = scalar::multiply(dead_angle, pitch_unit);
            const float adjusted_pitch = scalar::subtract(pitch_delta, pitch_removed);
            const float yaw_unit = scalar::multiply(yaw_delta, inverse_length);
            const float yaw_removed = scalar::multiply(dead_angle, yaw_unit);
            const float adjusted_yaw = scalar::subtract(yaw_delta, yaw_removed);
            pitch = wrap_angle(arithmetic::add(pivot_pitch, scalar::multiply(chase_rate, adjusted_pitch)),
                               period);
            state.angles.pitch = pitch;
            // Yaw multiplies adjusted angle by chase rate; pitch reverses the operands.
            // Preserve that distinction for NaN payloads.
            yaw = wrap_angle(arithmetic::add(pivot_yaw, scalar::multiply(adjusted_yaw, chase_rate)), period);
            state.angles.yaw = yaw;
        }
    }

    state.front_lock_active = uint8_t{0};
    state.front_lock_angles.pitch = pitch;
    state.front_lock_angles.yaw = yaw;
    if (state.update_target_pitch_at_lock != 0)
    {
        state.target_pitch = pitch;
    }
    else
    {
        state.front_lock_angles.pitch = state.target_pitch;
    }
    // Queries and the saved input time remain available to subsequent update stages.
    return {AngularContinuation::rotation_release, 0};
}

} // namespace camera::update::locked_geometry
