// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera locked geometry stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Locked geometry joins rotation release at0x0183c949; no whole-update entry or hook.
#include "angular_mode.h"

namespace camera::update::locked_geometry
{
namespace packed = engine::camera_subject_math;
namespace scalar = engine::camera_control_math;
namespace arithmetic = engine::camera_target_math;
namespace angular_math = engine::camera_angular_mode_math;
namespace locked_math = engine::camera_locked_geometry_math;
namespace root = engine::camera_root;
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
RT_GLOBAL(0x04d25e7c, ratio_root_bias, const float); // bits3f800001; deliberately above1

// First reduction's ADD is signed_pi + angle; later scalar wraps use angle + signed_pi.
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

// Original c02e..c0a6: retain constructed powers, source order and two HADD levels.
// Labels describe exponents only; do not replace this instruction polynomial with libm.
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
    const float first = locked_math::sum_four(packed::multiply(powers_1_to_7, odd_coefficients_1_to_7.get()));
    const float second =
        locked_math::sum_four(packed::multiply(powers_9_to_15, odd_coefficients_9_to_15.get()));
    const float third =
        locked_math::sum_four(packed::multiply(powers_17_to_23, odd_coefficients_17_to_23.get()));
    return arithmetic::add(arithmetic::add(first, second), third);
}

// Original c0b4..c13e. The caller enters only after ordered lock_length > scaled_offset.
// That one-sided branch does not establish an absolute-domain bound for arbitrary inputs.
float evaluate_ratio_polynomial(float scaled_offset, float lock_length, float one)
{
    const float ratio = scalar::divide(scaled_offset, lock_length);
    const float magnitude = locked_math::absolute(ratio);
    const float magnitude_product = scalar::multiply(magnitude, ratio);
    const float third_product = scalar::multiply(magnitude_product, ratio);
    const float remainder = scalar::subtract(ratio, magnitude_product);
    const float radicand = scalar::subtract(scalar::capture(ratio_root_bias.get()), magnitude);
    // Original c0dd source1.low=ratio; source2.low=bias-abs(ratio), not ratio.
    const float denominator = angular_math::square_root(ratio, radicand);
    const float corrected = scalar::divide(remainder, denominator);
    const Vec4 first = locked_math::lanes(third_product, one, third_product, one);
    const Vec4 second = locked_math::lanes(corrected, corrected, ratio, ratio);
    const Vec4 weights = packed::multiply(first, second);
    const Vec4 absolute_ratio = arithmetic::broadcast(magnitude);
    const Vec4 quadratic = packed::multiply(absolute_ratio, ratio_coefficients_quadratic.get());
    const Vec4 linear = packed::add(quadratic, ratio_coefficients_linear.get());
    const Vec4 product = packed::multiply(absolute_ratio, linear);
    const Vec4 coefficients = packed::add(product, ratio_coefficients_constant.get());
    return locked_math::sum_four(packed::multiply(coefficients, weights));
}

Vec4 refined_distance(Vec4 difference, Vec4 half, Vec4 three)
{
    const Vec4 squares = packed::multiply(difference, difference);
    const Vec4 xy = packed::add(packed::broadcast<0>(squares), packed::broadcast<1>(squares));
    const Vec4 length_squared = packed::add(packed::broadcast<2>(squares), xy);
    return root::refined_length(length_squared, half, three, root::FirstProductOrder::reciprocal_then_length);
}

// The original conversion is signed64 followed by low32 extraction and CVTDQ2PS.
// This deliberately differs from scalar32 wrapping for NaN/large quotients.
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

float pitch_or_captured(Vec4 direction, float captured_pitch, bool first_root)
{
    const float x_squared = scalar::multiply(direction.x(), direction.x());
    const float z_squared = scalar::multiply(direction.z(), direction.z());
    const float sum = arithmetic::add(x_squared, z_squared);
    // bd96 merges the sum; be7e merges delta.Y. Only the low result is consumed.
    const float horizontal = angular_math::square_root(first_root ? sum : direction.y(), sum);
    if (!packed::is_ordered_zero(direction.y()) || !packed::is_ordered_zero(horizontal))
    {
        return angular_math::xor_low(atan2_float(direction.y(), horizontal),
                                     update::angular_mode::pitch_sign_bits.get());
    }
    return captured_pitch;
}

float yaw_or_captured(Vec4 direction, float captured_yaw)
{
    if (!packed::is_ordered_zero(direction.x()) || !packed::is_ordered_zero(direction.z()))
    {
        return atan2_float(direction.x(), direction.z());
    }
    return captured_yaw;
}

AngularPreparation prepare_locked_geometry(FollowCamera &state, const TargetPreparation &target)
{
    // This entry is selected only by AngularContinuation::locked_geometry. All vectors below
    // are snapshots across up to four original atan2 callbacks, not graph/guest rereads.
    const Vec4 lock_target = packed::capture(state.lock_target);
    const Vec4 orbit = packed::capture(state.orbit_origin);
    const Vec4 lock_difference = packed::subtract(lock_target, orbit);
    const float original_pitch = scalar::capture(state.angles.pitch);
    const float desired_pitch = pitch_or_captured(lock_difference, original_pitch, true);
    // Original yaw capture is after the first possible callback, before the second.
    const float original_yaw = scalar::capture(state.angles.yaw);
    const float desired_yaw = yaw_or_captured(lock_difference, original_yaw);
    const Vec4 pivot_difference = packed::subtract(orbit, target.fulcrum);
    const float pivot_pitch = pitch_or_captured(pivot_difference, original_pitch, false);
    const float pivot_yaw = yaw_or_captured(pivot_difference, original_yaw);

    const float fov = scalar::capture(state.vertical_fov);
    const float shift_ratio = scalar::capture(state.lock_pitch_shift_ratio);
    const float shift =
        scalar::multiply(scalar::multiply(fov, shift_ratio), scalar::capture(update::target::half_fov.get()));
    const Vec4 half = update::target::half_vector.get();
    const Vec4 three = update::target::three_vector.get();
    const Vec4 lock_length = refined_distance(lock_difference, half, three);
    const Vec4 pivot_length = refined_distance(pivot_difference, half, three);
    const float period = scalar::capture(two_pi.get());
    const float reduced = reduce_shift(shift, period);
    const float one = scalar::capture(update::target::scalar_one.get());
    const float shift_polynomial = evaluate_shift_polynomial(reduced, one);
    const float scaled_offset = scalar::multiply(pivot_length.x(), shift_polynomial);
    float offset_angle;
    if (scalar::unordered_or_less_equal(lock_length.x(), scaled_offset))
    {
        offset_angle = scalar::capture(half_pi.get());
    }
    else
    {
        offset_angle = evaluate_ratio_polynomial(scaled_offset, lock_length.x(), one);
    }

    const float absolute_height = locked_math::absolute(lock_difference.y());
    const float begin = scalar::capture(state.rotation_range_height_begin);
    const float end = scalar::capture(state.rotation_range_height_end);
    const float height_above_begin = scalar::subtract(absolute_height, begin);
    const float height_span = scalar::subtract(end, begin);
    const float height_ratio = scalar::divide(height_above_begin, height_span);
    float height_blend = 0.0f;
    if (scalar::unordered_or_less_equal(0.0f, height_ratio))
    {
        height_blend = locked_math::minimum(one, height_ratio);
    }
    const float minimum_lock = scalar::capture(state.minimum_lock_pitch);
    const float minimum = scalar::capture(state.minimum_pitch);
    const float lower = arithmetic::add(
        minimum_lock, scalar::multiply(height_blend, scalar::subtract(minimum, minimum_lock)));
    const float shifted_offset = arithmetic::add(shift, offset_angle);
    const float requested_pitch = arithmetic::add(desired_pitch, shifted_offset);
    float selected_pitch = lower;
    if (scalar::unordered_or_less_equal(lower, requested_pitch))
    {
        const float maximum = scalar::capture(state.maximum_pitch);
        const float maximum_lock = scalar::capture(state.maximum_lock_pitch);
        const float upper = arithmetic::add(
            maximum_lock, scalar::multiply(height_blend, scalar::subtract(maximum, maximum_lock)));
        selected_pitch = locked_math::minimum(upper, requested_pitch);
    }

    const float pitch_delta = wrap_angle(scalar::subtract(selected_pitch, pivot_pitch), period);
    const float chase_rate =
        engine::camera_frame_policy::standard_gain(scalar::capture(state.lock_rotation_chase_rate));
    float pitch = wrap_angle(arithmetic::add(pivot_pitch, scalar::multiply(chase_rate, pitch_delta)), period);
    scalar::publish(state.angles.pitch, pitch);
    const float yaw_delta = wrap_angle(scalar::subtract(desired_yaw, pivot_yaw), period);
    float yaw = wrap_angle(arithmetic::add(pivot_yaw, scalar::multiply(chase_rate, yaw_delta)), period);
    scalar::publish(state.angles.yaw, yaw);
    const float pitch_error_squared = scalar::multiply(pitch_delta, pitch_delta);
    const float yaw_error_squared = scalar::multiply(yaw_delta, yaw_delta);
    const float error_squared = arithmetic::add(pitch_error_squared, yaw_error_squared);
    const float dead_angle = scalar::capture(state.lock_rotation_dead_angle);
    const float dead_squared = scalar::multiply(dead_angle, dead_angle);
    if (!scalar::unordered_or_less_equal(dead_squared, error_squared))
    {
        scalar::publish(state.angles.pitch, pivot_pitch);
        scalar::publish(state.angles.yaw, pivot_yaw);
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
            // c48a source2 is total error², not dead² from the low merge source1.
            const float length = angular_math::square_root(dead_squared, error_squared);
            const float inverse_length = scalar::divide(one, length);
            const float pitch_unit = scalar::multiply(pitch_delta, inverse_length);
            const float pitch_removed = scalar::multiply(dead_angle, pitch_unit);
            const float adjusted_pitch = scalar::subtract(pitch_delta, pitch_removed);
            const float yaw_unit = scalar::multiply(yaw_delta, inverse_length);
            const float yaw_removed = scalar::multiply(dead_angle, yaw_unit);
            const float adjusted_yaw = scalar::subtract(yaw_delta, yaw_removed);
            pitch = wrap_angle(arithmetic::add(pivot_pitch, scalar::multiply(chase_rate, adjusted_pitch)),
                               period);
            scalar::publish(state.angles.pitch, pitch);
            // Original c4e6 is adjusted_yaw FIRST, chase_rate SECOND (pitch is opposite).
            yaw = wrap_angle(arithmetic::add(pivot_yaw, scalar::multiply(adjusted_yaw, chase_rate)), period);
            scalar::publish(state.angles.yaw, yaw);
        }
    }

    scalar::publish(state.front_lock_active, uint8_t{0});
    scalar::publish(state.front_lock_angles.pitch, pitch);
    scalar::publish(state.front_lock_angles.yaw, yaw);
    if (scalar::capture(state.update_target_pitch_at_lock) != 0)
    {
        scalar::publish(state.target_pitch, pitch);
    }
    else
    {
        scalar::publish(state.front_lock_angles.pitch, scalar::capture(state.target_pitch));
    }
    // The outer integration retains queryR12, full timeXMM10 and all other stage carriers.
    return {AngularContinuation::rotation_release, 0};
}

} // namespace camera::update::locked_geometry
