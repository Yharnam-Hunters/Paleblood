// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera angle frame stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Build the pitch/yaw direction, preserve literal normalization masks, reposition fulcrum.
#include "math_calls.h"
#include "../../engine/camera_update/reciprocal_root.h"

namespace camera::update::angle_frame
{
namespace packed = engine::camera_angle_frame_math;
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

struct AngleFramePreparation
{
    Vec4 fulcrum; // original XMM1 at0x0183d18f, already published to guest e0
    Vec4 half;    // original XMM8, actual captured constant vector
    Vec4 three;   // original XMM9, actual captured constant vector
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

AngleFramePreparation prepare_angle_frame(FollowCamera &state)
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

} // namespace camera::update::angle_frame
