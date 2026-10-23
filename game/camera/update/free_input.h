// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera free input stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Reuses the proven shared ce97..cf2d tail after preparation. No full camera entry/hook.
#include "rotation_dispatch.h"

namespace camera::update::free_input
{
namespace packed = engine::camera_subject_math;
namespace scalar = engine::camera_control_math;
namespace arithmetic = engine::camera_target_math;
namespace angular_math = engine::camera_angular_mode_math;
namespace locked_math = engine::camera_locked_geometry_math;
namespace free_math = engine::camera_free_input_math;
namespace front_math = engine::camera_front_chase_math;
namespace root = engine::camera_root;
using update::control::ControlPreparation;
using update::subject::SubjectCompensationState;
using update::rotation_dispatch::RotationContinuation;
using update::rotation_dispatch::RotationDispatch;

static_assert(offsetof(FollowCamera, orbit_origin) == 0xd0 &&
              offsetof(FollowCamera, chased_position) == 0x100);
static_assert(offsetof(FollowCamera, high_speed_rotation_rate) == 0x138);
static_assert(offsetof(FollowCamera, angles) == 0x140 && offsetof(FollowCamera, target_pitch) == 0x150);
static_assert(offsetof(FollowCamera, rotation_speed) == 0x204);
static_assert(offsetof(FollowCamera, high_rotation_speed) == 0x21c);
static_assert(offsetof(FollowCamera, direct_pitch_angle) == 0x238 &&
              offsetof(FollowCamera, direct_pitch_rate) == 0x23c);
static_assert(offsetof(FollowCamera, high_speed_rotation_ramp_time) == 0x1e8);
static_assert(offsetof(FollowCamera, minimum_pitch) == 0x1f0 &&
              offsetof(FollowCamera, maximum_pitch) == 0x1ec);
static_assert(offsetof(FollowCamera, rotation_control_rate) == 0x130);
static_assert(offsetof(FollowCamera, rotation_control_wait_remaining) == 0x134);
static_assert(offsetof(FollowCamera, rotation_control_end_wait_time) == 0x1e0);

RT_GLOBAL(0x04cf25c0, even_coefficients_0_to_6, const Vec4);
RT_GLOBAL(0x04cf25d0, even_coefficients_8_to_14, const Vec4);
RT_GLOBAL(0x04cf25e0, even_coefficients_16_to_22, const Vec4);
RT_GLOBAL(0x04cf25f0, reciprocal_period, const Vec4);
RT_GLOBAL(0x04cf2600, magnitude_mask, const Vec4);
RT_GLOBAL(0x04cf2610, conversion_threshold_bits, const Vec4);
RT_GLOBAL(0x04cf2620, period_vector, const Vec4);
RT_GLOBAL(0x04d25e80, high_input_threshold, const float);
RT_GLOBAL(0x04d25e84, negative_one, const float);

struct PolynomialBasis
{
    Vec4 values;  // [one, reduced angle, angle², angle³]
    Vec4 squares; // exactly one packed squaring, retained across the even/odd evaluations
};

static_assert(offsetof(PolynomialBasis, values) == 0 && offsetof(PolynomialBasis, squares) == 0x10);
static_assert(sizeof(PolynomialBasis) == 0x20 && alignof(PolynomialBasis) == 0x10);

PolynomialBasis make_basis(float reduced, float one)
{
    const float squared = scalar::multiply(reduced, reduced);
    const float cubed = scalar::multiply(reduced, squared);
    const Vec4 values = locked_math::lanes(one, reduced, squared, cubed);
    return {values, packed::multiply(values, values)};
}

float evaluate_even_polynomial(const PolynomialBasis &basis)
{
    const Vec4 power_8 =
        packed::multiply(packed::broadcast<2>(basis.values), packed::broadcast<3>(basis.squares));
    const Vec4 powers_8_to_14 = packed::multiply(basis.squares, power_8);
    const Vec4 powers_16_to_22 = packed::multiply(power_8, powers_8_to_14);
    const float first =
        locked_math::sum_four(packed::multiply(basis.squares, even_coefficients_0_to_6.get()));
    const float second =
        locked_math::sum_four(packed::multiply(powers_8_to_14, even_coefficients_8_to_14.get()));
    const float third =
        locked_math::sum_four(packed::multiply(powers_16_to_22, even_coefficients_16_to_22.get()));
    return arithmetic::add(arithmetic::add(first, second), third);
}

float evaluate_odd_polynomial(const PolynomialBasis &basis)
{
    const Vec4 angle = packed::broadcast<1>(basis.values);
    const Vec4 powers_1_to_7 = packed::multiply(basis.squares, angle);
    const Vec4 power_8 = packed::multiply(angle, packed::broadcast<3>(powers_1_to_7));
    const Vec4 powers_9_to_15 = packed::multiply(powers_1_to_7, power_8);
    const Vec4 powers_17_to_23 = packed::multiply(power_8, powers_9_to_15);
    const float first = locked_math::sum_four(
        packed::multiply(powers_1_to_7, update::locked_geometry::odd_coefficients_1_to_7.get()));
    const float second = locked_math::sum_four(
        packed::multiply(powers_9_to_15, update::locked_geometry::odd_coefficients_9_to_15.get()));
    const float third = locked_math::sum_four(
        packed::multiply(powers_17_to_23, update::locked_geometry::odd_coefficients_17_to_23.get()));
    return arithmetic::add(arithmetic::add(first, second), third);
}

Vec4 wrap_angle_pair(Vec4 angles)
{
    const Vec4 scaled = packed::multiply(angles, reciprocal_period.get());
    const Vec4 magnitude = packed::bit_and(scaled, magnitude_mask.get());
    const Vec4 negative_magnitude =
        free_math::bit_xor(magnitude, update::angular_mode::pitch_sign_bits.get());
    const Vec4 convert = free_math::signed_greater_bits(conversion_threshold_bits.get(), negative_magnitude);
    const Vec4 integers = free_math::rounded_integer_bits(scaled);
    const Vec4 rounded = free_math::integer_bits_to_float(integers);
    const Vec4 unchanged_part = free_math::bit_and_not(convert, scaled);
    const Vec4 rounded_part = packed::bit_and(convert, rounded);
    const Vec4 selected = packed::bit_or(rounded_part, unchanged_part);
    return packed::subtract(angles, packed::multiply(selected, period_vector.get()));
}

Vec4 capture_angle_pair(FollowCamera &state)
{
    const float pitch = scalar::capture(state.angles.pitch);
    const float yaw = scalar::capture(state.angles.yaw);
    return locked_math::lanes(pitch, yaw, 0.0f, 0.0f);
}

Vec4 rotation_speeds(FollowCamera &state, float effective_rate)
{
    const float base_pitch = scalar::capture(state.rotation_speed.pitch);
    const float high_pitch = scalar::capture(state.high_rotation_speed.pitch);
    const float pitch_difference = scalar::subtract(high_pitch, base_pitch);
    const float magnitude = locked_math::absolute(effective_rate);
    const float pitch_step = scalar::multiply(magnitude, pitch_difference);
    const float pitch_speed = arithmetic::add(base_pitch, pitch_step);
    const float base_yaw = scalar::capture(state.rotation_speed.yaw);
    const float high_yaw = scalar::capture(state.high_rotation_speed.yaw);
    const float yaw_difference = scalar::subtract(high_yaw, base_yaw);
    const float yaw_step = scalar::multiply(magnitude, yaw_difference);
    const float yaw_speed = arithmetic::add(base_yaw, yaw_step);
    return locked_math::lanes(pitch_speed, yaw_speed, 0.0f, 0.0f);
}

float apply_normal_input(FollowCamera &state, Vec4 input, Vec4 broadcast_delta, float effective_rate)
{
    const Vec4 angles = capture_angle_pair(state);
    const Vec4 speeds = rotation_speeds(state, effective_rate);
    // Preserve every lane, including input.Z/W * +0 and the subsequent time products.
    const Vec4 input_movement = packed::multiply(input, speeds);
    const Vec4 movement = packed::multiply(broadcast_delta, input_movement);
    const Vec4 wrapped = wrap_angle_pair(packed::add(angles, movement));
    const float minimum = scalar::capture(state.minimum_pitch);
    float pitch = minimum;
    if (scalar::unordered_or_less_equal(minimum, wrapped.x()))
    {
        const float maximum = scalar::capture(state.maximum_pitch);
        pitch = locked_math::minimum(maximum, wrapped.x());
    }
    // Normal path publishes pitch before yaw (high-speed path below does the opposite).
    scalar::publish(state.angles.pitch, pitch);
    scalar::publish(state.angles.yaw, wrapped.y());
    return pitch;
}

float reduce_negative_direct_angle(float raw_angle, float period)
{
    const float negative = angular_math::xor_low(raw_angle, update::angular_mode::pitch_sign_bits.get());
    const float signed_pi = locked_math::signed_pi(negative);
    // Original f9e2 is SUB(pi_for_negative_angle, raw_angle), not ADD(pi, negated_angle).
    const float biased = scalar::subtract(signed_pi, raw_angle);
    const float quotient = scalar::divide(biased, period);
    const float integral = locked_math::truncate_32_to_float(quotient);
    return scalar::subtract(negative, scalar::multiply(integral, period));
}

float apply_high_input(FollowCamera &state, Vec4 input, Vec4 broadcast_delta, float effective_rate,
                       float raw_direct_angle, float period, const PolynomialBasis &basis)
{
    // Original f832 comparison precedes the following guest captures and survives the math.
    const bool positive_y = !scalar::unordered_or_less_equal(input.y(), 0.0f);
    const Vec4 angles = capture_angle_pair(state);
    const Vec4 speeds = rotation_speeds(state, effective_rate);
    // High-speed motion has no input*speeds product: only broadcast time FIRST * speeds.
    const Vec4 movement = packed::multiply(broadcast_delta, speeds);
    const Vec4 candidate = positive_y ? packed::add(angles, movement) : packed::subtract(angles, movement);
    const Vec4 wrapped = wrap_angle_pair(candidate);
    scalar::publish(state.angles.yaw, wrapped.y());
    float pitch;
    if (!scalar::unordered_or_less_equal(input.x(), 0.0f))
    {
        const float polynomial = evaluate_odd_polynomial(basis);
        const float ratio = scalar::divide(input.x(), polynomial);
        const float maximum = scalar::capture(state.maximum_pitch);
        const float scaled = scalar::multiply(ratio, maximum);
        float requested = 0.0f;
        if (scalar::unordered_or_less_equal(0.0f, scaled))
        {
            requested = locked_math::minimum(maximum, scaled);
        }
        const float difference = scalar::subtract(requested, angles.x());
        const float rate = scalar::capture(state.direct_pitch_rate);
        const float movement_pitch =
            engine::camera_frame_policy::direct_pitch_movement(scalar::multiply(difference, rate));
        const float chased = arithmetic::add(angles.x(), movement_pitch);
        const float minimum = scalar::capture(state.minimum_pitch);
        pitch = minimum;
        if (scalar::unordered_or_less_equal(minimum, chased))
        {
            // f9a2..f9b8 chooses a pointer and RELOADS guest maximum only for ordered >.
            pitch = !scalar::unordered_or_less_equal(chased, maximum) ? scalar::capture(state.maximum_pitch)
                                                                      : chased;
        }
    }
    else
    {
        const float reduced = reduce_negative_direct_angle(raw_direct_angle, period);
        const PolynomialBasis negative_basis =
            make_basis(reduced, scalar::capture(update::target::scalar_one.get()));
        const float polynomial = evaluate_odd_polynomial(negative_basis);
        const float ratio = scalar::divide(input.x(), polynomial);
        const float minimum = scalar::capture(state.minimum_pitch);
        const float scaled = scalar::multiply(ratio, minimum);
        float requested = minimum;
        if (scalar::unordered_or_less_equal(minimum, scaled))
        {
            requested = locked_math::minimum(0.0f, scaled);
        }
        const float difference = scalar::subtract(requested, angles.x());
        const float rate = scalar::capture(state.direct_pitch_rate);
        const float movement_pitch =
            engine::camera_frame_policy::direct_pitch_movement(scalar::multiply(difference, rate));
        const float chased = arithmetic::add(angles.x(), movement_pitch);
        pitch = minimum; // retain the min captured before scaled/chase, not a late reread
        if (scalar::unordered_or_less_equal(minimum, chased))
        {
            pitch = locked_math::minimum(scalar::capture(state.maximum_pitch), chased);
        }
    }
    scalar::publish(state.angles.pitch, pitch);
    return pitch;
}

void prepare_hand_angles(FollowCamera &state)
{
    const Vec4 orbit = packed::capture(state.orbit_origin);
    const Vec4 chased = packed::capture(state.chased_position);
    const Vec4 difference = packed::subtract(orbit, chased);
    const float horizontal = angular_math::horizontal_length(difference);
    float pitch;
    if (!packed::is_ordered_zero(difference.y()) || !packed::is_ordered_zero(horizontal))
    {
        pitch = angular_math::xor_low(atan2_float(difference.y(), horizontal),
                                      update::angular_mode::pitch_sign_bits.get());
    }
    else
    {
        pitch = scalar::capture(state.angles.pitch);
    }
    scalar::publish(state.angles.pitch, pitch);
    // Snapshot delta X/Z survives the pitch callback; only the zero/zero fallback reloads yaw.
    const float yaw = update::angular_mode::yaw_or_existing(difference, state.angles.yaw);
    scalar::publish(state.angles.yaw, yaw);
}

float prepare_input_angles(FollowCamera &state, Vec4 input, Vec4 broadcast_delta, float delta_time)
{
    const Vec4 squares = packed::multiply(input, input);
    const Vec4 xy = packed::add(packed::broadcast<0>(squares), packed::broadcast<1>(squares));
    const Vec4 squared_length = packed::add(packed::broadcast<2>(squares), xy);
    // Original cbc3 is the exceptional L_r first-product order, unlike the two locked roots.
    const Vec4 magnitude = root::refined_length(squared_length, update::target::half_vector.get(),
                                                update::target::three_vector.get(),
                                                root::FirstProductOrder::length_then_reciprocal);
    if (scalar::unordered_or_less_equal(magnitude.x(), scalar::capture(high_input_threshold.get())))
    {
        scalar::publish(state.high_speed_rotation_rate, 0.0f);
        return apply_normal_input(state, input, broadcast_delta, 0.0f);
    }
    const float absolute_y = locked_math::absolute(input.y());
    const float raw_angle = scalar::capture(state.direct_pitch_angle);
    const float period = scalar::capture(update::locked_geometry::two_pi.get());
    const float reduced = update::locked_geometry::reduce_shift(raw_angle, period);
    const float one = scalar::capture(update::target::scalar_one.get());
    const PolynomialBasis basis = make_basis(reduced, one);
    const float polynomial = evaluate_even_polynomial(basis);
    if (scalar::unordered_or_less_equal(absolute_y, polynomial))
    {
        scalar::publish(state.high_speed_rotation_rate, 0.0f);
        return apply_normal_input(state, input, broadcast_delta, 0.0f);
    }
    const bool positive_y = !scalar::unordered_or_less_equal(input.y(), 0.0f);
    float rate = scalar::capture(state.high_speed_rotation_rate);
    float effective;
    if (positive_y)
    {
        if (front_math::ordered_at_least(0.0f, rate))
        {
            scalar::publish(state.high_speed_rotation_rate, 0.0f);
            rate = 0.0f;
        }
        const float step = scalar::divide(delta_time, scalar::capture(state.high_speed_rotation_ramp_time));
        rate = arithmetic::add(step, rate);
        scalar::publish(state.high_speed_rotation_rate, rate);
        if (!scalar::unordered_or_less_equal(rate, one))
        {
            scalar::publish(state.high_speed_rotation_rate, 1.0f);
            effective = one;
        }
        else
        {
            effective = rate;
        }
    }
    else
    {
        if (front_math::ordered_at_least(rate, 0.0f))
        {
            scalar::publish(state.high_speed_rotation_rate, 0.0f);
            rate = 0.0f;
        }
        const float step = scalar::divide(delta_time, scalar::capture(state.high_speed_rotation_ramp_time));
        rate = scalar::subtract(rate, step);
        scalar::publish(state.high_speed_rotation_rate, rate);
        const float minus_one = scalar::capture(negative_one.get());
        if (!scalar::unordered_or_less_equal(minus_one, rate))
        {
            scalar::publish(state.high_speed_rotation_rate, -1.0f);
            effective = minus_one;
        }
        else
        {
            effective = rate;
        }
    }
    if (packed::is_ordered_zero(effective))
    {
        return apply_normal_input(state, input, broadcast_delta, effective);
    }
    return apply_high_input(state, input, broadcast_delta, effective, raw_angle, period, basis);
}

RotationDispatch prepare_free_input(FollowCamera &state, const RotationDispatch &dispatch,
                                    const ControlPreparation &control,
                                    const SubjectCompensationState &previous)
{
    // Entry precondition: free_input atcab9 (incoming AL0 has already been consumed).
    // Retain all original retained snapshots across the optional atan2 callbacks.
    const uint8_t captured_lock = dispatch.captured_lock;
    const Vec4 input = control.control_input;
    const Vec4 saved_time = previous.saved_time_register;
    const Vec4 broadcast_delta = previous.broadcast_delta;
    const bool hand = control.hand_input_active;
    if (hand) prepare_hand_angles(state);
    const float pitch = prepare_input_angles(state, input, broadcast_delta, saved_time.x());
    if (hand)
    {
        scalar::publish(state.target_pitch, pitch);
        scalar::publish(state.rotation_control_rate, 1.0f);
        const float wait = scalar::capture(state.rotation_control_end_wait_time);
        scalar::publish(state.rotation_control_wait_remaining, wait);
    }
    update::rotation_dispatch::finish_rotation_input(state, captured_lock);
    return {RotationContinuation::angle_frame, captured_lock};
}

} // namespace camera::update::free_input
