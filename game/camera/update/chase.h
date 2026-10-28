// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera chase stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Chase the candidate by projected angle and distance; retain the result for the next stage.
// Later orientation and collision stages consume the retained candidate.
#include "wing_escape.h"

namespace camera::update::chase
{
namespace math = engine::camera_chase_math;
namespace root = engine::camera_root;
namespace direction = camera::update::wing_escape;

RT_GLOBAL(0x04cf24a0, y_axis, const Vec4);
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf24f0, half_vector, const Vec4);
RT_GLOBAL(0x04cf2670, fully_chased_axes, const Vec4);
RT_GLOBAL(0x04d25e6c, one, const float);
RT_GLOBAL(0x04d25e88, distance_threshold, const float);

struct ChaseRates
{
    float horizontal;
    float vertical;
    float minimum_angle;
    float maximum_angle;
};
struct CameraChasePreparation
{
    Vec4 candidate;   // originalXMM13, not yet published to camera+40
    Vec4 cast_origin; // originalXMM2 retained for direction construction
    Vec4 half;        // originalXMM15
    Vec4 three;       // originalXMM14 restored after polynomial work
};
static_assert(sizeof(CameraChasePreparation) == 0x40 && alignof(CameraChasePreparation) == 16);

ChaseRates capture_chase_rates(FollowCamera &state)
{
    const uint8_t front = math::capture(state.front_lock_active);
    const bool horizontal_locked = front != 0 || math::capture(state.lock_requested) != 0;
    const float horizontal = horizontal_locked ? math::capture(state.lock_target_chase_rate_xz)
                                               : math::capture(state.target_chase_rate_xz);
    const bool vertical_locked = front != 0 || math::capture(state.lock_requested) != 0;
    const float vertical = vertical_locked ? math::capture(state.lock_target_chase_rate_y)
                                           : math::capture(state.target_chase_rate_y);
    const float normal_minimum = math::capture(state.target_chase_min_angle);
    if (math::ordered_greater(normal_minimum, math::capture(state.target_chase_max_angle)))
    {
        math::publish(state.target_chase_max_angle, normal_minimum);
    }
    float minimum = math::capture(state.lock_target_chase_min_angle);
    float maximum = math::capture(state.lock_target_chase_max_angle);
    if (math::ordered_greater(minimum, maximum))
    {
        math::publish(state.lock_target_chase_max_angle, minimum);
        maximum = minimum;
    }
    if (front == 0)
    {
        const bool locked = math::capture(state.lock_requested) != 0;
        minimum = locked ? math::capture(state.lock_target_chase_min_angle)
                         : math::capture(state.target_chase_min_angle);
        maximum = locked ? math::capture(state.lock_target_chase_max_angle)
                         : math::capture(state.target_chase_max_angle);
    }
    return {horizontal, vertical, minimum, maximum};
}

float projected_direction_dot(Vec4 candidate_offset, Vec4 previous_offset, Vec4 half, Vec4 three)
{
    const Vec4 mask = math::capture(xyz_bits.get());
    const Vec4 candidate_xyz = math::bit_and(candidate_offset, mask);
    const Vec4 a = direction::normalize_direction(
        candidate_xyz, direction::direction_squared_length(candidate_xyz), half, three);
    const Vec4 previous_xyz = math::bit_and(previous_offset, math::capture(xyz_bits.get()));
    const Vec4 b = direction::normalize_direction(
        previous_xyz, direction::direction_squared_length(previous_xyz), half, three);
    return math::dot_xyz(a, b);
}

// One set of original RO captures is shared by both angle evaluations. In
// particular its one survives in original XMM12 as the short-distance scale.
struct AnglePolynomialConstants
{
    float period;
    float one;
    Vec4 coefficients_0_to_6;
    Vec4 coefficients_8_to_14;
    Vec4 coefficients_16_to_22;
};
struct AngularChase
{
    Vec4 rate;
    float captured_one;
};

template <bool CaptureConstants> float chase_angle_cosine(float angle, AnglePolynomialConstants &constants)
{
    const float biased = math::add(math::signed_pi(angle), angle);
    if constexpr (CaptureConstants) constants.period = math::capture(direction::period.get());
    const float integral = math::truncate_32_to_float(math::divide(biased, constants.period));
    const float reduced = math::subtract(angle, math::multiply(integral, constants.period));
    const float squared = math::multiply(reduced, reduced);
    const float cubed = math::multiply(reduced, squared);
    if constexpr (CaptureConstants) constants.one = math::capture(one.get());
    const Vec4 basis = math::lanes(constants.one, reduced, squared, cubed);
    const Vec4 powers_0_to_6 = math::multiply(basis, basis);
    const Vec4 power_8 =
        math::multiply(math::broadcast<Vec4::lane_z>(basis), math::broadcast<3>(powers_0_to_6));
    const Vec4 powers_8_to_14 = math::multiply(powers_0_to_6, power_8);
    const Vec4 powers_16_to_22 = math::multiply(power_8, powers_8_to_14);
    if constexpr (CaptureConstants)
        constants.coefficients_0_to_6 = math::capture(direction::even_coefficients_0_to_6.get());
    const float first = math::sum_four(math::multiply(powers_0_to_6, constants.coefficients_0_to_6)).x();
    if constexpr (CaptureConstants)
        constants.coefficients_8_to_14 = math::capture(direction::even_coefficients_8_to_14.get());
    const float second = math::sum_four(math::multiply(powers_8_to_14, constants.coefficients_8_to_14)).x();
    if constexpr (CaptureConstants)
        constants.coefficients_16_to_22 = math::capture(direction::even_coefficients_16_to_22.get());
    const float third = math::sum_four(math::multiply(powers_16_to_22, constants.coefficients_16_to_22)).x();
    return math::add(math::add(first, second), third);
}

AngularChase angle_chase_rate(FollowCamera &state, const ChaseRates &rates, Vec4 candidate, Vec4 previous,
                              Vec4 orbit, Vec4 half, Vec4 three)
{
    const Vec4 candidate_offset = math::subtract(candidate, orbit);
    const Vec4 candidate_length =
        root::refined_length(math::squared_xyz_length(candidate_offset), half, three,
                             root::FirstProductOrder::reciprocal_then_length);
    const bool candidate_long =
        math::ordered_greater(candidate_length.x(), math::capture(distance_threshold.get()));
    float dot = math::capture(one.get());
    if (candidate_long)
    {
        const Vec4 previous_offset = math::subtract(previous, orbit);
        const Vec4 previous_length =
            root::refined_length(math::squared_xyz_length(previous_offset), half, three,
                                 root::FirstProductOrder::reciprocal_then_length);
        if (math::ordered_greater(previous_length.x(), math::capture(distance_threshold.get())))
        {
            dot = projected_direction_dot(candidate_offset, previous_offset, half, three);
        }
    }
    Vec4 rate = math::lanes(rates.horizontal, rates.vertical, rates.horizontal, 0.0f);
    AnglePolynomialConstants polynomial;
    const float minimum_cosine = chase_angle_cosine<true>(rates.minimum_angle, polynomial);
    if (math::ordered_greater(dot, minimum_cosine))
    {
        rate = math::multiply(rate, math::capture(y_axis.get()));
    }
    else
    {
        const float maximum_cosine = chase_angle_cosine<false>(rates.maximum_angle, polynomial);
        if (math::ordered_greater(dot, maximum_cosine) &&
            math::ordered_greater(minimum_cosine, maximum_cosine))
        {
            const float numerator = math::subtract(dot, minimum_cosine);
            const float denominator = math::subtract(maximum_cosine, minimum_cosine);
            const float weight = math::divide(numerator, denominator);
            rate = math::multiply(rate, math::lanes(weight, math::capture(one.get()), weight, 0.0f));
        }
    }
    rate = engine::camera_frame_policy::packed_standard_gain(rate);
    const Vec4 complement = math::subtract(math::capture(fully_chased_axes.get()), rate);
    const Vec4 controlled =
        math::multiply(complement, math::scalar_broadcast(math::capture(state.rotation_control_rate)));
    return {math::add(rate, controlled), polynomial.one};
}

float interpolated_camera_distance(FollowCamera &state, Vec4 candidate_offset, Vec4 previous_offset,
                                   Vec4 half, Vec4 three)
{
    // Original interleaves these roots. Complete the first correction, compute the
    // previous-distance root, then resume the candidate's second correction.
    const Vec4 candidate_squared = math::squared_xyz_length(candidate_offset);
    const Vec4 reciprocal = root::reciprocal_root(candidate_squared);
    const Vec4 half_reciprocal = math::multiply(reciprocal, half);
    const Vec4 first_product = math::multiply(reciprocal, candidate_squared);
    const Vec4 first_error = math::subtract(three, math::multiply(reciprocal, first_product));
    const Vec4 corrected = math::multiply(half_reciprocal, first_error);
    const Vec4 previous_squared = math::squared_xyz_length(previous_offset);
    const Vec4 previous_length =
        root::refined_length(previous_squared, half, three, root::FirstProductOrder::reciprocal_then_length);
    const Vec4 length_product = math::multiply(candidate_squared, corrected);
    const Vec4 reciprocal_product = math::multiply(corrected, length_product);
    const Vec4 reciprocal_zero = root::equal(reciprocal, Vec4::zero());
    const Vec4 squared_zero = root::equal(candidate_squared, Vec4::zero());
    const Vec4 second_error = math::subtract(three, reciprocal_product);
    const Vec4 half_corrected = math::multiply(corrected, math::capture(half_vector.get()));
    const Vec4 second_correction = math::multiply(half_corrected, second_error);
    const Vec4 use_original = root::mask_union(squared_zero, reciprocal_zero);
    const Vec4 estimate = math::multiply(candidate_squared, second_correction);
    const Vec4 candidate_length = root::select(estimate, candidate_squared, use_original);
    const float difference = math::subtract(candidate_length.x(), previous_length.x());
    const float movement = math::multiply(difference, engine::camera_frame_policy::distance_chase_gain(
                                                          math::capture(state.target_chase_distance_rate)));
    return math::add(previous_length.x(), movement);
}

CameraChasePreparation chase_camera_candidate(FollowCamera &state, Vec4 half, Vec4 three)
{
    const Vec4 candidate = math::capture(state.candidate_position);
    if (math::capture(state.reset_requested) != 0)
    {
        return {candidate, math::capture(state.cast_origin), half, three};
    }
    const ChaseRates rates = capture_chase_rates(state);
    const Vec4 orbit = math::capture(state.orbit_origin);
    const Vec4 previous = math::capture(state.chased_position);
    const AngularChase angular = angle_chase_rate(state, rates, candidate, previous, orbit, half, three);
    const Vec4 movement = math::multiply(math::subtract(candidate, previous), angular.rate);
    const Vec4 interpolated = math::add(previous, movement);
    const Vec4 cast = math::capture(state.cast_origin);
    const Vec4 difference = math::subtract(interpolated, cast);
    const Vec4 length = root::refined_length(math::squared_xyz_length(difference), half, three,
                                             root::FirstProductOrder::reciprocal_then_length);
    float scale = angular.captured_one; // original XMM12 is overwritten at e681
    if (math::ordered_greater(length.x(), math::capture(distance_threshold.get())))
    {
        const Vec4 candidate_offset = math::subtract(candidate, cast);
        const Vec4 previous_offset = math::subtract(previous, cast);
        scale = math::divide(
            interpolated_camera_distance(state, candidate_offset, previous_offset, half, three), length.x());
    }
    const Vec4 chased = math::add(cast, math::multiply(difference, math::scalar_broadcast(scale)));
    return {chased, cast, half, three};
}

} // namespace camera::update::chase
