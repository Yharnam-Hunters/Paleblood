// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera wing escape stage, included only by update.cpp.
#pragma once
#include "escape_yaw_storage.h"
#include "../../engine/camera_update/math.h"
// Shared direction expressions below retain the independently proved angle-frame tree.
#include "query_calls.h"
#include "math_calls.h"
#include "../../engine/camera_update/reciprocal_root.h"

namespace camera::update::wing_escape
{
inline constexpr int z_y_x_w = 0xc6;
inline constexpr int sine_zero_cosine_zero = 0xcc;
namespace packed = engine::camera_wing_math;
namespace root = engine::camera_root;
namespace calls = camera::math_calls;

RT_GLOBAL(0x04cf2630, negate_pitch_sine, const Vec4);
RT_GLOBAL(0x04cf24e0, negate_yaw_sine, const Vec4);
RT_GLOBAL(0x04cf24a0, unit_y, const Vec4);
RT_GLOBAL(0x04cf24c0, unit_w, const Vec4);
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf24f0, half_vector, const Vec4);
RT_GLOBAL(0x04cf2500, three_vector, const Vec4);
RT_GLOBAL(0x04cf2640, infinity_vector, const Vec4);
RT_GLOBAL(0x04cf2650, canonical_nan_vector, const Vec4);
RT_GLOBAL(0x04d25e88, short_distance_threshold, const float);
RT_GLOBAL(0x04d25e8c, short_distance_replacement, const float);

struct FulcrumFrame
{
    Vec4 fulcrum; // rebuilt full fulcrum, already published to guest e0
    Vec4 half;    // final original XMM15, actual captured half
    Vec4 three;   // final original XMM14, actual captured three
};

Vec4 pitch_coefficients(FollowCamera &state)
{
    const float pitch = packed::capture(state.angles.pitch);
    const float sine = calls::sine_cosine_select(pitch, calls::sine_selector, 0);
    const float cosine = calls::sine_cosine_select(pitch, calls::cosine_selector, 0);
    const Vec4 zero = Vec4::zero();
    const Vec4 sine_low = packed::scalar_vector(sine);
    const Vec4 cosine_low = packed::scalar_vector(cosine);
    constexpr int zero_cosine_sine_zero = 0xc3;
    const Vec4 coefficients(engine::camera_vector::shuffle_two<zero_cosine_sine_zero>(cosine_low, sine_low));
    const Vec4 signed_coefficients = packed::multiply(coefficients, packed::capture(negate_pitch_sine.get()));
    const Vec4 zero_products = packed::multiply(coefficients, zero);
    constexpr int zero_sine_cosine_zero = 0xd8;
    const Vec4 rearranged(engine::camera_vector::shuffle_words<zero_sine_cosine_zero>(signed_coefficients));
    const Vec4 signed_plus_zero = packed::add(rearranged, zero);
    const Vec4 zero_products_plus_zero = packed::add(zero_products, zero);
    return packed::add(zero_products_plus_zero, signed_plus_zero);
}

Vec4 direction_from_angles(FollowCamera &state, Vec4 pitch)
{
    // Yaw is read after both pitch callbacks; each pair retains its original angle.
    const float yaw = packed::capture(state.angles.yaw);
    const float sine = calls::sine_cosine_select(yaw, calls::sine_selector, 0);
    const float cosine = calls::sine_cosine_select(yaw, calls::cosine_selector, 0);
    const Vec4 sine_low = packed::scalar_vector(sine);
    const Vec4 cosine_low = packed::scalar_vector(cosine);
    constexpr int sine_zero_cosine_zero = 0xcc;
    const Vec4 yaw_forward(engine::camera_vector::shuffle_two<sine_zero_cosine_zero>(sine_low, cosine_low));
    const Vec4 signed_yaw = packed::multiply(yaw_forward, packed::capture(negate_yaw_sine.get()));
    constexpr int z_y_x_w = 0xc6;
    const Vec4 yaw_x(engine::camera_vector::shuffle_words<z_y_x_w>(signed_yaw));
    const Vec4 x = packed::multiply(packed::broadcast<0>(pitch), yaw_x);
    const Vec4 y = packed::multiply(packed::broadcast<1>(pitch), packed::capture(unit_y.get()));
    const Vec4 z = packed::multiply(yaw_forward, packed::broadcast<Vec4::lane_z>(pitch));
    const Vec4 w = packed::multiply(packed::broadcast<3>(pitch), packed::capture(unit_w.get()));
    const Vec4 zx = packed::add(z, x);
    const Vec4 yw = packed::add(y, w);
    return packed::bit_and(packed::add(yw, zx), packed::capture(xyz_bits.get()));
}

Vec4 direction_squared_length(Vec4 direction)
{
    // Original reduction: (X²+Z²)+(Y²+W²), retaining the complete shuffle/add tree.
    const Vec4 squares = packed::multiply(direction, direction);
    const Vec4 high(engine::camera_vector::high_halves(squares, squares));
    const Vec4 pairs = packed::add(squares, high);
    constexpr int x_x_x_y = 0x40;
    const Vec4 pair_pattern(engine::camera_vector::shuffle_words<x_x_x_y>(pairs));
    constexpr int z_z_yw_yw = 0xf0;
    const Vec4 other_pattern(engine::camera_vector::shuffle_two<z_z_yw_yw>(high, pair_pattern));
    return packed::broadcast<Vec4::lane_z>(packed::add(pair_pattern, other_pattern));
}

Vec4 normalize_direction(Vec4 direction, Vec4 squared_length, Vec4 half, Vec4 three)
{
    const Vec4 length =
        root::refined_length(squared_length, half, three, root::FirstProductOrder::reciprocal_then_length);
    const Vec4 nonzero = packed::not_equal(Vec4::zero(), length);
    const Vec4 not_infinite = packed::not_equal(squared_length, packed::capture(infinity_vector.get()));
    const Vec4 divided = packed::divide(direction, length);
    const Vec4 zero_masked = packed::bit_and(divided, nonzero);
    const Vec4 infinite_fallback =
        packed::bit_and_not(not_infinite, packed::capture(canonical_nan_vector.get()));
    return packed::bit_or(infinite_fallback, packed::bit_and(zero_masked, not_infinite));
}

FulcrumFrame rebuild_fulcrum_from_angles(FollowCamera &state)
{
    const Vec4 pitch = pitch_coefficients(state);
    const Vec4 direction = direction_from_angles(state, pitch);
    const Vec4 squared_length = direction_squared_length(direction);
    const Vec4 half = packed::capture(half_vector.get());
    const Vec4 three = packed::capture(three_vector.get());
    const Vec4 normalized = normalize_direction(direction, squared_length, half, three);

    const Vec4 orbit = packed::capture(state.orbit_origin);
    const Vec4 difference = packed::subtract(packed::capture(state.fulcrum), orbit);
    const Vec4 squares = packed::multiply(difference, difference);
    const Vec4 x = packed::broadcast<0>(squares);
    const Vec4 y = packed::broadcast<1>(squares);
    const Vec4 z = packed::broadcast<Vec4::lane_z>(squares);
    const Vec4 xy = packed::add(x, y);
    const Vec4 distance_squared = packed::add(z, xy);
    const Vec4 distance =
        root::refined_length(distance_squared, half, three, root::FirstProductOrder::reciprocal_then_length);
    const Vec4 short_mask =
        packed::less_low(distance, packed::scalar_vector(packed::capture(short_distance_threshold.get())));
    const Vec4 replacement = packed::scalar_vector(packed::capture(short_distance_replacement.get()));
    const Vec4 retained = packed::bit_and_not(short_mask, distance);
    const Vec4 substitute = packed::bit_and(short_mask, replacement);
    const Vec4 selected_distance = packed::broadcast<0>(packed::bit_or(retained, substitute));
    const Vec4 displacement = packed::multiply(normalized, selected_distance);
    const Vec4 fulcrum = packed::subtract(orbit, displacement);
    packed::publish(state.fulcrum, fulcrum);
    return {fulcrum, half, three};
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

struct RootConstants
{
    Vec4 half;
    Vec4 three;
};
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
static_assert(sizeof(RootConstants) == 0x20 && alignof(RootConstants) == 16);
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

RootConstants capture_root_constants()
{
    const Vec4 half = packed::capture(half_vector.get());
    const Vec4 three = packed::capture(three_vector.get());
    return {half, three};
}

YawBasis capture_yaw_basis(float yaw)
{
    const float sine = calls::sine_cosine_select(yaw, calls::sine_selector, 0);
    const float cosine = calls::sine_cosine_select(yaw, calls::cosine_selector, 0);
    const Vec4 sine_low = packed::scalar_vector(sine);
    const Vec4 cosine_low = packed::scalar_vector(cosine);
    const Vec4 forward(engine::camera_vector::shuffle_two<sine_zero_cosine_zero>(sine_low, cosine_low));
    const Vec4 signed_forward = packed::multiply(forward, packed::capture(negate_yaw_sine.get()));
    const Vec4 axis_x(engine::camera_vector::shuffle_words<z_y_x_w>(signed_forward));
    return {axis_x, forward};
}

Vec4 pack_rotation(Vec4 x, Vec4 z, Vec4 yw)
{
    const Vec4 xz(engine::camera_vector::unpack_low(x, z));
    return Vec4(engine::camera_vector::unpack_low(xz, yw));
}

FirstRotation rotate_first_wing(Vec4 direction, const YawBasis &basis)
{
    const Vec4 x = packed::sum_four(packed::multiply(direction, basis.axis_x));
    const Vec4 y = packed::sum_four(packed::multiply(direction, packed::capture(unit_y.get())));
    const Vec4 z = packed::sum_four(packed::multiply(direction, basis.forward));
    const Vec4 w = packed::sum_four(packed::multiply(direction, packed::capture(unit_w.get())));
    const Vec4 yw(engine::camera_vector::unpack_low(y, w));
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
    const float p = packed::capture(period.get());
    const float integral = packed::truncate_32_to_float(packed::divide(biased, p));
    const float reduced = packed::subtract(angle, packed::multiply(integral, p));
    const float squared = packed::multiply(reduced, reduced);
    const float cubed = packed::multiply(reduced, squared);
    const Vec4 basis = packed::lanes(packed::capture(one.get()), reduced, squared, cubed);
    const Vec4 even_0_to_6 = packed::multiply(basis, basis);
    const Vec4 power_8 =
        packed::multiply(packed::broadcast<Vec4::lane_z>(basis), packed::broadcast<3>(even_0_to_6));
    const Vec4 even_8_to_14 = packed::multiply(even_0_to_6, power_8);
    const Vec4 even_16_to_22 = packed::multiply(power_8, even_8_to_14);
    const float first =
        packed::sum_four(packed::multiply(even_0_to_6, packed::capture(even_coefficients_0_to_6.get()))).x();
    const float second =
        packed::sum_four(packed::multiply(even_8_to_14, packed::capture(even_coefficients_8_to_14.get())))
            .x();
    const float third =
        packed::sum_four(packed::multiply(even_16_to_22, packed::capture(even_coefficients_16_to_22.get())))
            .x();
    return packed::add(packed::add(first, second), third);
}

WingQueries prepare_wing_queries(FollowCamera &state)
{
    const Vec4 reference = packed::capture(state.target_reference);
    const Vec4 candidate = packed::capture(state.candidate_position);
    const Vec4 unmasked_difference = packed::subtract(candidate, reference);
    const Vec4 difference = packed::bit_and(unmasked_difference, packed::capture(xyz_bits.get()));
    const RootConstants constants = capture_root_constants();
    const Vec4 normalized = normalize_direction(difference, direction_squared_length(difference),
                                                constants.half, constants.three);
    const float separation = packed::capture(state.escape_wall_wing_separation);
    const float yaw = packed::capture(state.escape_wall_wing_yaw);
    const float tangent = tangent_float(yaw);
    const float center_distance = packed::divide(separation, tangent);
    const float origin_distance = packed::divide(center_distance, wing_scale_polynomial(yaw));
    const Vec4 center =
        packed::subtract(reference, packed::multiply(normalized, packed::scalar_broadcast(center_distance)));
    const FirstRotation positive = rotate_first_wing(normalized, capture_yaw_basis(yaw));
    const Vec4 positive_origin =
        packed::add(center, packed::multiply(packed::scalar_broadcast(origin_distance), positive.rotated));
    const float first_negative_input = packed::capture(state.escape_wall_wing_yaw);
    const float negative_yaw = packed::xor_low(first_negative_input, packed::capture(sign_mask.get()));
    const Vec4 negative_rotated =
        rotate_wing_retaining_yw(normalized, capture_yaw_basis(negative_yaw), positive.retained_yw);
    const Vec4 negative_origin =
        packed::add(center, packed::multiply(packed::scalar_broadcast(origin_distance), negative_rotated));
    const Vec4 positive_direction = rotate_wing_retaining_yw(
        normalized, capture_yaw_basis(packed::capture(state.escape_wall_wing_yaw)), positive.retained_yw);
    const float positive_length = packed::capture(state.escape_wall_wing_length);
    const Vec4 positive_displacement =
        packed::multiply(positive_direction, packed::scalar_broadcast(positive_length));
    const float second_negative_input = packed::capture(state.escape_wall_wing_yaw);
    const float second_negative_yaw =
        packed::xor_low(second_negative_input, packed::capture(sign_mask.get()));
    const Vec4 negative_direction =
        rotate_wing_retaining_yw(normalized, capture_yaw_basis(second_negative_yaw), positive.retained_yw);
    const float negative_length = packed::capture(state.escape_wall_wing_length);
    const Vec4 negative_displacement =
        packed::multiply(negative_direction, packed::scalar_broadcast(negative_length));
    return {positive_origin, negative_origin, positive_displacement, negative_displacement};
}

float interpolated_escape_rate(FollowCamera &state, float fraction, float weak_begin, float captured_one,
                               bool capture_one_after_numerator)
{
    const float numerator = packed::subtract(fraction, weak_begin);
    if (capture_one_after_numerator) captured_one = packed::capture(one.get());
    const float denominator = packed::subtract(captured_one, weak_begin);
    const float position = packed::divide(numerator, denominator);
    float weight = 0.0f;
    if (!packed::ordered_greater(0.0f, position)) weight = packed::minimum(captured_one, position);
    const float strong = packed::capture(state.escape_wall_yaw_increment_rate);
    const float weak = packed::capture(state.weak_escape_wall_yaw_increment_rate);
    const float change = packed::subtract(weak, strong);
    return packed::add(strong, packed::multiply(weight, change));
}

RootConstants advance_escape_yaw(FollowCamera &state, bool positive_wing, float fraction, float delta)
{
    float yaw = packed::capture(state.escape_wall_yaw);
    const float weak_begin = packed::capture(state.weak_escape_wall_begin_fraction);
    if (positive_wing ? packed::ordered_greater(yaw, 0.0f) : packed::ordered_greater(0.0f, yaw))
    {
        packed::publish(state.escape_wall_yaw, 0.0f);
        yaw = 0.0f;
    }
    const bool strong_region = packed::ordered_greater(weak_begin, fraction);
    const RootConstants constants = capture_root_constants();
    // Positive direction captures one even on the strong path; negative captures only
    // on the interpolation path. Keep this distinction from the original branches.
    float captured_one = 0.0f;
    if (positive_wing) captured_one = packed::capture(one.get());
    float rate;
    if (strong_region)
    {
        rate = packed::capture(state.escape_wall_yaw_increment_rate);
    }
    else
    {
        rate = interpolated_escape_rate(state, fraction, weak_begin, captured_one, !positive_wing);
    }
    const float movement = packed::multiply(rate, delta);
    yaw = positive_wing ? packed::subtract(yaw, movement) : packed::add(yaw, movement);
    packed::publish(state.escape_wall_yaw, yaw);
    return constants;
}

void reposition_fulcrum_from_candidate(FollowCamera &state, Vec4 candidate)
{
    const Vec4 origin = packed::capture(state.cast_origin);
    const Vec4 difference = packed::subtract(candidate, origin);
    const Vec4 squared_length = packed::squared_xyz_length(difference);
    const RootConstants constants = capture_root_constants();
    const Vec4 length = root::refined_length(squared_length, constants.half, constants.three,
                                             root::FirstProductOrder::reciprocal_then_length);
    float ratio;
    if (packed::ordered_greater(length.x(), packed::capture(distance_division_threshold.get())))
    {
        ratio = packed::divide(packed::capture(state.fulcrum_distance), length.x());
    }
    else
    {
        ratio = packed::capture(one.get());
    }
    packed::publish(state.fulcrum,
                    packed::add(origin, packed::multiply(difference, packed::scalar_broadcast(ratio))));
}

RootConstants rotate_escape_candidate(FollowCamera &state, float yaw)
{
    const YawBasis basis = capture_yaw_basis(yaw);
    const Vec4 reference = packed::capture(state.target_reference);
    const Vec4 difference = packed::subtract(packed::capture(state.candidate_position), reference);
    // Escape rotation reverses the X/Z dot operand order relative to wing construction.
    const Vec4 x = packed::sum_four(packed::multiply(basis.axis_x, difference));
    const Vec4 y = packed::sum_four(packed::multiply(difference, packed::capture(unit_y.get())));
    const Vec4 z = packed::sum_four(packed::multiply(basis.forward, difference));
    const Vec4 w = packed::sum_four(packed::multiply(difference, packed::capture(unit_w.get())));
    const Vec4 yw(engine::camera_vector::unpack_low(y, w));
    const Vec4 candidate = packed::add(reference, pack_rotation(x, z, yw));
    packed::publish(state.candidate_position, candidate);
    reposition_fulcrum_from_candidate(state, candidate);
    const float current_yaw = packed::capture(state.angles.yaw);
    const float current_escape_yaw = packed::capture(state.escape_wall_yaw);
    packed::publish(state.angles.yaw, packed::subtract(current_yaw, current_escape_yaw));
    const FulcrumFrame frame = rebuild_fulcrum_from_angles(state);
    return {frame.half, frame.three};
}

RootConstants finish_escape_rotation(FollowCamera &state, bool advanced_from_hit, Vec4 time,
                                     RootConstants constants)
{
    if (packed::capture(state.escape_wall_mode) == 0)
    {
        // Original clears opaque bytes290..293 together with the yaw at294.
        engine::camera_memory::clear_qword(escape_yaw_storage(state));
        return constants;
    }
    const float yaw = packed::capture(state.escape_wall_yaw);
    if (packed::ordered_zero(yaw)) return constants;
    const float factor =
        advanced_from_hit ? packed::capture(one.get()) : packed::capture(state.escape_wall_damping);
    const float retained_yaw = packed::multiply(factor, yaw);
    const float maximum_step = packed::multiply(time.x(), packed::capture(state.escape_wall_yaw_speed_limit));
    float limited = packed::xor_low(maximum_step, packed::capture(sign_mask.get()));
    if (!packed::ordered_greater(limited, retained_yaw))
        limited = packed::minimum(maximum_step, retained_yaw);
    const Vec4 tiny = packed::less_low(packed::scalar_vector(packed::absolute_bits(limited)),
                                       packed::scalar_vector(packed::capture(minimum_escape_yaw.get())));
    const float selected = packed::bit_and_not(tiny, packed::scalar_vector(limited)).x();
    packed::publish(state.escape_wall_yaw, selected);
    return rotate_escape_candidate(state, selected);
}

void update_escape_chase_rates(FollowCamera &state, bool advanced_from_hit)
{
    if (advanced_from_hit)
    {
        packed::publish(state.subject_translation_chase_rate,
                        packed::capture(state.escape_subject_translation_chase_rate));
        packed::publish(state.target_chase_rate_xz, packed::capture(state.escape_target_chase_rate_xz));
        return;
    }
    const float subject = packed::capture(state.subject_translation_chase_rate);
    const float subject_change =
        packed::subtract(packed::capture(state.normal_subject_translation_chase_rate), subject);
    const float rate =
        engine::camera_frame_policy::normal_chase_factor(packed::capture(normal_chase_interpolation.get()));
    packed::publish(state.subject_translation_chase_rate,
                    packed::add(subject, packed::multiply(subject_change, rate)));
    const float target = packed::capture(state.target_chase_rate_xz);
    const float target_change = packed::subtract(packed::capture(state.normal_target_chase_rate_xz), target);
    packed::publish(state.target_chase_rate_xz, packed::add(target, packed::multiply(target_change, rate)));
}

RootConstants prepare_wing_escape(FollowCamera &state, QueryWorld *queries, Vec4 saved_time_register)
{
    engine::camera_memory::clear_qword(wing_hit_fractions(state));
    if (packed::capture(wall_camera_mode.get()) == 0)
    {
        packed::publish(state.escape_wall_mode, uint8_t{0});
        const RootConstants constants = capture_root_constants();
        if (engine::camera_frame_policy::wall_off_copies_escape_rates())
            update_escape_chase_rates(state, true);
        return constants;
    }
    // R15 is the live front-lock-active field, not the earlier hand-input carrier.
    if (packed::capture(state.hand_control) != 0 || packed::capture(state.front_lock_active) != 0 ||
        packed::capture(state.lock_requested) != 0 || packed::capture(state.reset_requested) != 0)
    {
        packed::publish(state.escape_wall_mode, uint8_t{0});
    }
    bool advanced_from_hit = false;
    RootConstants constants;
    if (queries != nullptr)
    {
        WingQueries wings = prepare_wing_queries(state);
        float positive_fraction = 0.0f;
        float negative_fraction = 0.0f;
        const float positive_radius = packed::capture(state.escape_wall_wing_radius);
        const uint8_t positive_hit = query::sphere_query(
            queries, query::follow_camera_query_filter, &wings.positive_origin, &wings.positive_displacement,
            positive_radius, nullptr, nullptr, &positive_fraction);
        const float negative_radius = packed::capture(state.escape_wall_wing_radius);
        const uint8_t negative_hit = query::sphere_query(
            queries, query::follow_camera_query_filter, &wings.negative_origin, &wings.negative_displacement,
            negative_radius, nullptr, nullptr, &negative_fraction);
        const float positive = packed::capture(positive_fraction);
        packed::publish(state.positive_wing_hit_fraction, positive);
        const float negative = packed::capture(negative_fraction);
        packed::publish(state.negative_wing_hit_fraction, negative);
        if (positive_hit == 0 && negative_hit == 0)
        {
            packed::publish(state.escape_wall_mode, engine::camera_frame_policy::both_wings_missed_mode());
            constants = capture_root_constants();
        }
        else if ((positive_hit & negative_hit) == 0)
        {
            const bool positive_direction = positive_hit != 0;
            constants = advance_escape_yaw(state, positive_direction,
                                           positive_direction ? positive : negative, saved_time_register.x());
            advanced_from_hit = true;
        }
        else
        {
            constants = capture_root_constants();
        }
    }
    else
    {
        // Null world does not share the both-miss escape-mode=1 publication.
        constants = capture_root_constants();
    }
    constants = finish_escape_rotation(state, advanced_from_hit, saved_time_register, constants);
    update_escape_chase_rates(state, advanced_from_hit);
    return constants;
}

} // namespace camera::update::wing_escape
