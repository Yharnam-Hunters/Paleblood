// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera orientation stage, included only by update.cpp.
#pragma once
#include "math.h"
// Build the basis, restore the saved basis on NaN, then publish the chased point.
#include "chase.h"

namespace camera::update::orientation
{
namespace math = camera::update::math;
namespace direction = camera::update::wing_escape;

RT_GLOBAL(0x04cf24b0, unit_z, const Vec4);
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf2640, exponent_bits, const Vec4);
RT_GLOBAL(0x04d25e6c, one, const float);
RT_GLOBAL(0x04d25e88, near_vertical_threshold, const float);

struct OrientationPreparation
{
    Vec4 candidate;
    Vec4 cast_origin;

    Vec4 saved_axis_x;
    Vec4 saved_axis_y;
    Vec4 saved_forward;
    Vec4 saved_position;
};
static_assert(sizeof(OrientationPreparation) == 0x60 && alignof(OrientationPreparation) == 16);

Vec4 normalized(Vec4 value)
{
    return direction::normalize_direction(value, direction::direction_squared_length(value));
}

void construct_camera_orientation(FollowCamera &state, const OrientationPreparation &input)
{
    const Vec4 mask = xyz_bits.get();
    const Vec4 camera_direction = math::bit_and(math::subtract(input.cast_origin, input.candidate), mask);
    Vec4 forward = normalized(camera_direction);
    const float x_squared = math::multiply(forward.x(), forward.x());
    const float z = math::broadcast<2>(forward).x();
    const float z_squared = math::multiply(z, z);
    const float horizontal_squared = math::add(x_squared, z_squared);
    if (math::ordered_greater(near_vertical_threshold.get(), horizontal_squared))
    {
        forward = state.transform.forward;
    }
    const Vec4 forward_yzx = math::shuffle_words<0xc9>(forward);
    const Vec4 selected_z = math::multiply(forward, unit_z.get());
    const Vec4 selected_z_yzx = math::shuffle_words<0xc9>(selected_z);
    // VMOVSS creates [one,0,0,0]; the packed zero products remain significant.
    const Vec4 selected_x = math::multiply(forward, math::scalar_vector(one.get()));
    const Vec4 first_difference = math::subtract(selected_z_yzx, selected_x);
    const Vec4 cross_mask = xyz_bits.get();
    const Vec4 right_input = math::bit_and(math::shuffle_words<0x49>(first_difference), cross_mask);
    const Vec4 right = normalized(right_input);
    const Vec4 first_cross_product = math::multiply(forward, math::shuffle_words<0xc9>(right));
    const Vec4 second_cross_product = math::multiply(forward_yzx, right);
    const Vec4 up_input = math::bit_and(
        math::shuffle_words<0x49>(math::subtract(first_cross_product, second_cross_product)), cross_mask);
    const Vec4 up = normalized(up_input);
    state.transform.axis_x = right;
    state.transform.axis_y = up;
    state.transform.forward = forward;
    const Vec4 exponents = exponent_bits.get();
    // Original evaluates all three NaN masks even after a prior failure.
    const bool bad_right = math::contains_nan(right, exponents);
    const bool bad_up = math::contains_nan(up, exponents);
    const bool bad_forward = math::contains_nan(forward, exponents);
    if (bad_right || bad_up || bad_forward)
    {
        state.transform.axis_x = input.saved_axis_x;
        state.transform.axis_y = input.saved_axis_y;
        state.transform.forward = input.saved_forward;
        state.transform.position = input.saved_position;
    }
    state.chased_position = input.candidate;
    state.transform.position = input.candidate;
}

} // namespace camera::update::orientation
