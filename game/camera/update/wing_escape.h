// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera wing escape stage, included only by update.cpp.
#pragma once
#include "escape_yaw_storage.h"
#include "math.h"
// Build the wall-escape queries from the camera orientation computed earlier.
#include "query_calls.h"
#include "math_calls.h"

namespace camera::update::wing_escape
{
inline constexpr int z_y_x_w = 0xc6;
inline constexpr int sine_zero_cosine_zero = 0xcc;
namespace packed = camera::update::math;
namespace root = camera::update::root;
namespace calls = camera::math_calls;

RT_GLOBAL(0x04cf2630, negate_pitch_sine, const Vec4);
RT_GLOBAL(0x04cf24e0, negate_yaw_sine, const Vec4);
RT_GLOBAL(0x04cf24a0, unit_y, const Vec4);
RT_GLOBAL(0x04cf24c0, unit_w, const Vec4);
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf2640, infinity_vector, const Vec4);
RT_GLOBAL(0x04cf2650, canonical_nan_vector, const Vec4);
RT_GLOBAL(0x04d25e88, short_distance_threshold, const float);
RT_GLOBAL(0x04d25e8c, short_distance_replacement, const float);

struct FulcrumFrame
{
    Vec4 fulcrum; // Rebuilt origin already stored in the camera state.
};

Vec4 pitch_coefficients(FollowCamera &state)
{
    const float pitch = state.angles.pitch;
    const float sine = calls::sine_cosine_select(pitch, calls::sine_selector, 0);
    const float cosine = calls::sine_cosine_select(pitch, calls::cosine_selector, 0);
    const Vec4 zero = Vec4::zero();
    const Vec4 sine_low = packed::scalar_vector(sine);
    const Vec4 cosine_low = packed::scalar_vector(cosine);
    constexpr int zero_cosine_sine_zero = 0xc3;
    const Vec4 coefficients(camera::update::math::shuffle_two<zero_cosine_sine_zero>(cosine_low, sine_low));
    const Vec4 signed_coefficients = packed::multiply(coefficients, negate_pitch_sine.get());
    const Vec4 zero_products = packed::multiply(coefficients, zero);
    constexpr int zero_sine_cosine_zero = 0xd8;
    const Vec4 rearranged(camera::update::math::shuffle_words<zero_sine_cosine_zero>(signed_coefficients));
    const Vec4 signed_plus_zero = packed::add(rearranged, zero);
    const Vec4 zero_products_plus_zero = packed::add(zero_products, zero);
    return packed::add(zero_products_plus_zero, signed_plus_zero);
}

Vec4 direction_from_angles(FollowCamera &state, Vec4 pitch)
{
    // Yaw is read after both pitch callbacks; each pair retains its original angle.
    const float yaw = state.angles.yaw;
    const float sine = calls::sine_cosine_select(yaw, calls::sine_selector, 0);
    const float cosine = calls::sine_cosine_select(yaw, calls::cosine_selector, 0);
    const Vec4 sine_low = packed::scalar_vector(sine);
    const Vec4 cosine_low = packed::scalar_vector(cosine);
    constexpr int sine_zero_cosine_zero = 0xcc;
    const Vec4 yaw_forward(camera::update::math::shuffle_two<sine_zero_cosine_zero>(sine_low, cosine_low));
    const Vec4 signed_yaw = packed::multiply(yaw_forward, negate_yaw_sine.get());
    constexpr int z_y_x_w = 0xc6;
    const Vec4 yaw_x(camera::update::math::shuffle_words<z_y_x_w>(signed_yaw));
    const Vec4 x = packed::multiply(packed::broadcast<0>(pitch), yaw_x);
    const Vec4 y = packed::multiply(packed::broadcast<1>(pitch), unit_y.get());
    const Vec4 z = packed::multiply(yaw_forward, packed::broadcast<Vec4::lane_z>(pitch));
    const Vec4 w = packed::multiply(packed::broadcast<3>(pitch), unit_w.get());
    const Vec4 zx = packed::add(z, x);
    const Vec4 yw = packed::add(y, w);
    return packed::bit_and(packed::add(yw, zx), xyz_bits.get());
}

Vec4 direction_squared_length(Vec4 direction)
{
    // Original reduction: (X²+Z²)+(Y²+W²), retaining the complete shuffle/add tree.
    const Vec4 squares = packed::multiply(direction, direction);
    const Vec4 high(camera::update::math::high_halves(squares, squares));
    const Vec4 pairs = packed::add(squares, high);
    constexpr int x_x_x_y = 0x40;
    const Vec4 pair_pattern(camera::update::math::shuffle_words<x_x_x_y>(pairs));
    constexpr int z_z_yw_yw = 0xf0;
    const Vec4 other_pattern(camera::update::math::shuffle_two<z_z_yw_yw>(high, pair_pattern));
    return packed::broadcast<Vec4::lane_z>(packed::add(pair_pattern, other_pattern));
}

Vec4 normalize_direction(Vec4 direction, Vec4 squared_length)
{
    const Vec4 length =
        root::refined_length(squared_length, root::FirstProductOrder::reciprocal_then_length);
    const Vec4 nonzero = packed::not_equal(Vec4::zero(), length);
    const Vec4 not_infinite = packed::not_equal(squared_length, infinity_vector.get());
    const Vec4 divided = packed::divide(direction, length);
    const Vec4 zero_masked = packed::bit_and(divided, nonzero);
    const Vec4 infinite_fallback =
        packed::bit_and_not(not_infinite, canonical_nan_vector.get());
    return packed::bit_or(infinite_fallback, packed::bit_and(zero_masked, not_infinite));
}

FulcrumFrame rebuild_fulcrum_from_angles(FollowCamera &state)
{
    const Vec4 pitch = pitch_coefficients(state);
    const Vec4 direction = direction_from_angles(state, pitch);
    const Vec4 squared_length = direction_squared_length(direction);

    const Vec4 normalized = normalize_direction(direction, squared_length);

    const Vec4 orbit = state.orbit_origin;
    const Vec4 difference = packed::subtract(state.fulcrum, orbit);
    const Vec4 squares = packed::multiply(difference, difference);
    const Vec4 x = packed::broadcast<0>(squares);
    const Vec4 y = packed::broadcast<1>(squares);
    const Vec4 z = packed::broadcast<Vec4::lane_z>(squares);
    const Vec4 xy = packed::add(x, y);
    const Vec4 distance_squared = packed::add(z, xy);
    const Vec4 distance =
        root::refined_length(distance_squared, root::FirstProductOrder::reciprocal_then_length);
    const Vec4 short_mask =
        packed::less_low(distance, packed::scalar_vector(short_distance_threshold.get()));
    const Vec4 replacement = packed::scalar_vector(short_distance_replacement.get());
    const Vec4 retained = packed::bit_and_not(short_mask, distance);
    const Vec4 substitute = packed::bit_and(short_mask, replacement);
    const Vec4 selected_distance = packed::broadcast<0>(packed::bit_or(retained, substitute));
    const Vec4 displacement = packed::multiply(normalized, selected_distance);
    const Vec4 fulcrum = packed::subtract(orbit, displacement);
    state.fulcrum = fulcrum;
    return {fulcrum};
}

} // namespace camera::update::wing_escape

namespace camera::update::wing_escape
{
namespace query = camera::query_calls;

RT_ORIGINAL(0x02fbe648, tangent_float, float(float));
RT_GLOBAL(0x04d25e6c, one, const float);
RT_GLOBAL(0x04d25e74, period, const float);
RT_GLOBAL(0x04d25e58, minimum_escape_yaw, const float);
RT_GLOBAL(0x04d25e94, normal_chase_interpolation, const float);
RT_GLOBAL(0x04d25e88, distance_division_threshold, const float);
RT_GLOBAL(0x04cf2520, sign_mask, const Vec4);
RT_GLOBAL(0x04cf25c0, even_coefficients_0_to_6, const Vec4);
RT_GLOBAL(0x04cf25d0, even_coefficients_8_to_14, const Vec4);
RT_GLOBAL(0x04cf25e0, even_coefficients_16_to_22, const Vec4);

struct YawBasis
{
    Vec4 axis_x;
    Vec4 forward;
};
struct FirstRotation
{
    Vec4 rotated;
    Vec4 retained_yw;
};
struct WingQueries
{
    Vec4 positive_origin;
    Vec4 negative_origin;
    Vec4 positive_displacement;
    Vec4 negative_displacement;
};
static_assert(sizeof(WingQueries) == 0x40 && alignof(WingQueries) == 16);
static_assert(offsetof(FollowCamera, front_lock_active) == 0x263);
static_assert(offsetof(FollowCamera, escape_wall_mode) == 0x28c);
static_assert(offsetof(FollowCamera, escape_wall_yaw) == 0x294);
static_assert(offsetof(FollowCamera, positive_wing_hit_fraction) == 0x2c8);
static_assert(offsetof(FollowCamera, negative_wing_hit_fraction) == 0x2cc);
static_assert(offsetof(FollowCamera, escape_wall_yaw_increment_rate) == 0x29c);
static_assert(offsetof(FollowCamera, weak_escape_wall_yaw_increment_rate) == 0x2a4);
static_assert(offsetof(FollowCamera, weak_escape_wall_begin_fraction) == 0x2a8);
static_assert(offsetof(FollowCamera, escape_wall_yaw_speed_limit) == 0x2b0);
static_assert(offsetof(FollowCamera, escape_wall_damping) == 0x2b4);
static_assert(offsetof(FollowCamera, escape_wall_wing_yaw) == 0x2b8);
static_assert(offsetof(FollowCamera, escape_wall_wing_length) == 0x2bc);
static_assert(offsetof(FollowCamera, escape_wall_wing_radius) == 0x2c0);
static_assert(offsetof(FollowCamera, escape_wall_wing_separation) == 0x2c4);

YawBasis capture_yaw_basis(float yaw)
{
    const float sine = calls::sine_cosine_select(yaw, calls::sine_selector, 0);
    const float cosine = calls::sine_cosine_select(yaw, calls::cosine_selector, 0);
    const Vec4 sine_low = packed::scalar_vector(sine);
    const Vec4 cosine_low = packed::scalar_vector(cosine);
    const Vec4 forward(camera::update::math::shuffle_two<sine_zero_cosine_zero>(sine_low, cosine_low));
    const Vec4 signed_forward = packed::multiply(forward, negate_yaw_sine.get());
    const Vec4 axis_x(camera::update::math::shuffle_words<z_y_x_w>(signed_forward));
    return {axis_x, forward};
}

Vec4 pack_rotation(Vec4 x, Vec4 z, Vec4 yw)
{
    const Vec4 xz(camera::update::math::unpack_low(x, z));
    return Vec4(camera::update::math::unpack_low(xz, yw));
}

FirstRotation rotate_first_wing(Vec4 direction, const YawBasis &basis)
{
    const Vec4 x = packed::sum_four(packed::multiply(direction, basis.axis_x));
    const Vec4 y = packed::sum_four(packed::multiply(direction, unit_y.get()));
    const Vec4 z = packed::sum_four(packed::multiply(direction, basis.forward));
    const Vec4 w = packed::sum_four(packed::multiply(direction, unit_w.get()));
    const Vec4 yw(camera::update::math::unpack_low(y, w));
    return {pack_rotation(x, z, yw), yw};
}

Vec4 rotate_wing_retaining_yw(Vec4 direction, const YawBasis &basis, Vec4 yw)
{
    const Vec4 x = packed::sum_four(packed::multiply(direction, basis.axis_x));
    const Vec4 z = packed::sum_four(packed::multiply(direction, basis.forward));
    return pack_rotation(x, z, yw);
}

float wing_scale_polynomial(float angle)
{
    const float biased = packed::add(packed::signed_pi(angle), angle);
    const float p = period.get();
    const float integral = packed::truncate_32_to_float(packed::divide(biased, p));
    const float reduced = packed::subtract(angle, packed::multiply(integral, p));
    const float squared = packed::multiply(reduced, reduced);
    const float cubed = packed::multiply(reduced, squared);
    const Vec4 basis = packed::lanes(one.get(), reduced, squared, cubed);
    const Vec4 even_0_to_6 = packed::multiply(basis, basis);
    const Vec4 power_8 =
        packed::multiply(packed::broadcast<Vec4::lane_z>(basis), packed::broadcast<3>(even_0_to_6));
    const Vec4 even_8_to_14 = packed::multiply(even_0_to_6, power_8);
    const Vec4 even_16_to_22 = packed::multiply(power_8, even_8_to_14);
    const float first =
        packed::sum_four(packed::multiply(even_0_to_6, even_coefficients_0_to_6.get())).x();
    const float second =
        packed::sum_four(packed::multiply(even_8_to_14, even_coefficients_8_to_14.get()))
            .x();
    const float third =
        packed::sum_four(packed::multiply(even_16_to_22, even_coefficients_16_to_22.get()))
            .x();
    return packed::add(packed::add(first, second), third);
}

WingQueries prepare_wing_queries(FollowCamera &state)
{
    const Vec4 reference = state.target_reference;
    const Vec4 candidate = state.candidate_position;
    const Vec4 unmasked_difference = packed::subtract(candidate, reference);
    const Vec4 difference = packed::bit_and(unmasked_difference, xyz_bits.get());

    const Vec4 normalized = normalize_direction(difference, direction_squared_length(difference));
    const float separation = state.escape_wall_wing_separation;
    const float yaw = state.escape_wall_wing_yaw;
    const float tangent = tangent_float(yaw);
    const float center_distance = packed::divide(separation, tangent);
    const float origin_distance = packed::divide(center_distance, wing_scale_polynomial(yaw));
    const Vec4 center =
        packed::subtract(reference, packed::multiply(normalized, packed::broadcast(center_distance)));
    const FirstRotation positive = rotate_first_wing(normalized, capture_yaw_basis(yaw));
    const Vec4 positive_origin =
        packed::add(center, packed::multiply(packed::broadcast(origin_distance), positive.rotated));
    const float first_negative_input = state.escape_wall_wing_yaw;
    const float negative_yaw = packed::xor_low(first_negative_input, sign_mask.get());
    const Vec4 negative_rotated =
        rotate_wing_retaining_yw(normalized, capture_yaw_basis(negative_yaw), positive.retained_yw);
    const Vec4 negative_origin =
        packed::add(center, packed::multiply(packed::broadcast(origin_distance), negative_rotated));
    const Vec4 positive_direction = rotate_wing_retaining_yw(
        normalized, capture_yaw_basis(state.escape_wall_wing_yaw), positive.retained_yw);
    const float positive_length = state.escape_wall_wing_length;
    const Vec4 positive_displacement =
        packed::multiply(positive_direction, packed::broadcast(positive_length));
    const float second_negative_input = state.escape_wall_wing_yaw;
    const float second_negative_yaw =
        packed::xor_low(second_negative_input, sign_mask.get());
    const Vec4 negative_direction =
        rotate_wing_retaining_yw(normalized, capture_yaw_basis(second_negative_yaw), positive.retained_yw);
    const float negative_length = state.escape_wall_wing_length;
    const Vec4 negative_displacement =
        packed::multiply(negative_direction, packed::broadcast(negative_length));
    return {positive_origin, negative_origin, positive_displacement, negative_displacement};
}

float interpolated_escape_rate(FollowCamera &state, float fraction, float weak_begin, float captured_one,
                               bool capture_one_after_numerator)
{
    const float numerator = packed::subtract(fraction, weak_begin);
    if (capture_one_after_numerator) captured_one = one.get();
    const float denominator = packed::subtract(captured_one, weak_begin);
    const float position = packed::divide(numerator, denominator);
    float weight = 0.0f;
    if (!packed::ordered_greater(0.0f, position)) weight = packed::minimum(captured_one, position);
    const float strong = state.escape_wall_yaw_increment_rate;
    const float weak = state.weak_escape_wall_yaw_increment_rate;
    const float change = packed::subtract(weak, strong);
    return packed::add(strong, packed::multiply(weight, change));
}

void advance_escape_yaw(FollowCamera &state, bool positive_wing, float fraction, float delta)
{
    float yaw = state.escape_wall_yaw;
    const float weak_begin = state.weak_escape_wall_begin_fraction;
    if (positive_wing ? packed::ordered_greater(yaw, 0.0f) : packed::ordered_greater(0.0f, yaw))
    {
        state.escape_wall_yaw = 0.0f;
        yaw = 0.0f;
    }
    const bool strong_region = packed::ordered_greater(weak_begin, fraction);

    // Positive direction captures one even on the strong path; negative captures only
    // on the interpolation path. Keep this distinction from the original branches.
    float captured_one = 0.0f;
    if (positive_wing) captured_one = one.get();
    float rate;
    if (strong_region)
    {
        rate = state.escape_wall_yaw_increment_rate;
    }
    else
    {
        rate = interpolated_escape_rate(state, fraction, weak_begin, captured_one, !positive_wing);
    }
    const float movement = packed::multiply(rate, delta);
    yaw = positive_wing ? packed::subtract(yaw, movement) : packed::add(yaw, movement);
    state.escape_wall_yaw = yaw;
    return;
}

void reposition_fulcrum_from_candidate(FollowCamera &state, Vec4 candidate)
{
    const Vec4 origin = state.cast_origin;
    const Vec4 difference = packed::subtract(candidate, origin);
    const Vec4 squared_length = packed::squared_xyz_length(difference);

    const Vec4 length = root::refined_length(squared_length,
                                             root::FirstProductOrder::reciprocal_then_length);
    float ratio;
    if (packed::ordered_greater(length.x(), distance_division_threshold.get()))
    {
        ratio = packed::divide(state.fulcrum_distance, length.x());
    }
    else
    {
        ratio = one.get();
    }
    state.fulcrum = packed::add(origin, packed::multiply(difference, packed::broadcast(ratio)));
}

void rotate_escape_candidate(FollowCamera &state, float yaw)
{
    const YawBasis basis = capture_yaw_basis(yaw);
    const Vec4 reference = state.target_reference;
    const Vec4 difference = packed::subtract(state.candidate_position, reference);
    // Escape rotation reverses the X/Z dot operand order relative to wing construction.
    const Vec4 x = packed::sum_four(packed::multiply(basis.axis_x, difference));
    const Vec4 y = packed::sum_four(packed::multiply(difference, unit_y.get()));
    const Vec4 z = packed::sum_four(packed::multiply(basis.forward, difference));
    const Vec4 w = packed::sum_four(packed::multiply(difference, unit_w.get()));
    const Vec4 yw(camera::update::math::unpack_low(y, w));
    const Vec4 candidate = packed::add(reference, pack_rotation(x, z, yw));
    state.candidate_position = candidate;
    reposition_fulcrum_from_candidate(state, candidate);
    const float current_yaw = state.angles.yaw;
    const float current_escape_yaw = state.escape_wall_yaw;
    state.angles.yaw = packed::subtract(current_yaw, current_escape_yaw);
    rebuild_fulcrum_from_angles(state);
}

void finish_escape_rotation(FollowCamera &state, bool advanced_from_hit, Vec4 time)
{
    if (state.escape_wall_mode == 0)
    {
        // Disabling wall escape clears yaw and the preceding four opaque bytes together.
        camera::update::math::clear_qword(escape_yaw_storage(state));
        return;
    }
    const float yaw = state.escape_wall_yaw;
    if (packed::ordered_zero(yaw)) return;
    const float factor =
        advanced_from_hit ? one.get() : state.escape_wall_damping;
    const float retained_yaw = packed::multiply(factor, yaw);
    const float maximum_step = packed::multiply(time.x(), state.escape_wall_yaw_speed_limit);
    float limited = packed::xor_low(maximum_step, sign_mask.get());
    if (!packed::ordered_greater(limited, retained_yaw))
        limited = packed::minimum(maximum_step, retained_yaw);
    const Vec4 tiny = packed::less_low(packed::scalar_vector(packed::absolute(limited)),
                                       packed::scalar_vector(minimum_escape_yaw.get()));
    const float selected = packed::bit_and_not(tiny, packed::scalar_vector(limited)).x();
    state.escape_wall_yaw = selected;
    rotate_escape_candidate(state, selected);
}

void update_escape_chase_rates(FollowCamera &state, bool advanced_from_hit)
{
    if (advanced_from_hit)
    {
        state.subject_translation_chase_rate = state.escape_subject_translation_chase_rate;
        state.target_chase_rate_xz = state.escape_target_chase_rate_xz;
        return;
    }
    const float subject = state.subject_translation_chase_rate;
    const float subject_change =
        packed::subtract(state.normal_subject_translation_chase_rate, subject);
    const float rate =
        camera::update::frame_policy::normal_chase_factor(normal_chase_interpolation.get());
    state.subject_translation_chase_rate = packed::add(subject, packed::multiply(subject_change, rate));
    const float target = state.target_chase_rate_xz;
    const float target_change = packed::subtract(state.normal_target_chase_rate_xz, target);
    state.target_chase_rate_xz = packed::add(target, packed::multiply(target_change, rate));
}

void prepare_wing_escape(FollowCamera &state, QueryWorld *queries, Vec4 saved_time_register)
{
    camera::update::math::clear_qword(wing_hit_fractions(state));
    if (wall_camera_mode.get() == 0)
    {
        state.escape_wall_mode = uint8_t{0};

        if (camera::update::frame_policy::wall_off_copies_escape_rates())
            update_escape_chase_rates(state, true);
        return;
    }
    // Front lock is read after the earlier hand-input preparation.
    if (state.hand_control != 0 || state.front_lock_active != 0 ||
        state.lock_requested != 0 || state.reset_requested != 0)
    {
        state.escape_wall_mode = uint8_t{0};
    }
    bool advanced_from_hit = false;

    if (queries != nullptr)
    {
        WingQueries wings = prepare_wing_queries(state);
        float positive_fraction = 0.0f;
        float negative_fraction = 0.0f;
        const float positive_radius = state.escape_wall_wing_radius;
        const uint8_t positive_hit = query::sphere_query(
            queries, query::follow_camera_query_filter, &wings.positive_origin, &wings.positive_displacement,
            positive_radius, nullptr, nullptr, &positive_fraction);
        const float negative_radius = state.escape_wall_wing_radius;
        const uint8_t negative_hit = query::sphere_query(
            queries, query::follow_camera_query_filter, &wings.negative_origin, &wings.negative_displacement,
            negative_radius, nullptr, nullptr, &negative_fraction);
        const float positive = positive_fraction;
        state.positive_wing_hit_fraction = positive;
        const float negative = negative_fraction;
        state.negative_wing_hit_fraction = negative;
        if (positive_hit == 0 && negative_hit == 0)
        {
            state.escape_wall_mode = camera::update::frame_policy::both_wings_missed_mode();

        }
        else if ((positive_hit & negative_hit) == 0)
        {
            const bool positive_direction = positive_hit != 0;
            advance_escape_yaw(state, positive_direction,
                                           positive_direction ? positive : negative, saved_time_register.x());
            advanced_from_hit = true;
        }
    }
    finish_escape_rotation(state, advanced_from_hit, saved_time_register);
    update_escape_chase_rates(state, advanced_from_hit);
    return;
}

} // namespace camera::update::wing_escape
