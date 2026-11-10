// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera validation margin stage, included only by update.cpp.
#pragma once
#include "math.h"
// Territory avoidance is the intervening continuation and is deliberately separate.
#include "final_collision.h"

namespace camera::update::validation_margin
{
namespace math = camera::update::math;
namespace root = camera::update::root;
namespace direction = camera::update::wing_escape;
RT_GLOBAL(0x04cf2640, exponent_bits, const Vec4);
RT_GLOBAL(0x04cf2660, unit_vector, const Vec4);
RT_GLOBAL(0x04d25e88, distance_threshold, const float);
RT_GLOBAL(0x04d25e98, margin_length_bias, const float);

struct RollbackPreparation
{
    Matrix4 camera_transform;  // previous camera_basis and camera_position
    Matrix4 subject_transform; // previous subject_basis and subject_position
    Vec4 target;
    Vec4 cast_origin;
    Vec4 orbit_origin;
    Vec4 fulcrum;
    Vec4 candidate;
    Vec4 chased;
};
static_assert(sizeof(RollbackPreparation) == 0xe0 && alignof(RollbackPreparation) == 16);
struct ValidationPreparation
{
    Vec4 position;
};
static_assert(sizeof(ValidationPreparation) == 0x10);

ValidationPreparation validate_camera(FollowCamera &state, const RollbackPreparation &previous)
{
    state.reset_requested = uint8_t{0};
    state.lock_requested = uint8_t{0};
    state.boss_lock_requested = uint8_t{0};
    state.front_lock_requested = uint8_t{0};
    const Vec4 right = state.transform.axis_x;
    const Vec4 exponents = exponent_bits.get();
    const bool bad_right = math::contains_nan(right, exponents);

    const bool bad_up = math::contains_nan(state.transform.axis_y, exponents);
    const bool bad_forward = math::contains_nan(state.transform.forward, exponents);
    Vec4 position = state.transform.position;
    const bool bad_position = math::contains_nan(position, exponents);
    if (bad_right || bad_up || bad_forward || bad_position)
    {
        state.transform.axis_x = previous.camera_transform.axis_x;
        state.transform.axis_y = previous.camera_transform.axis_y;
        state.transform.forward = previous.camera_transform.forward;
        position = previous.camera_transform.position;
        state.transform.position = position;
        state.subject_transform.axis_x = previous.subject_transform.axis_x;
        state.subject_transform.axis_y = previous.subject_transform.axis_y;
        state.subject_transform.forward = previous.subject_transform.forward;
        state.subject_transform.position = previous.subject_transform.position;
        state.target = previous.target;
        state.cast_origin = previous.cast_origin;
        state.orbit_origin = previous.orbit_origin;
        state.fulcrum = previous.fulcrum;
        state.candidate_position = previous.candidate;
        state.chased_position = previous.chased;
    }
    return {position};
}

void apply_safe_margin(FollowCamera &state, ValidationPreparation retained)
{
    const Vec4 origin = state.cast_origin;
    const Vec4 difference = math::subtract(retained.position, origin);
    const Vec4 length = root::refined_length(math::squared_xyz_length(difference), root::FirstProductOrder::reciprocal_then_length);
    if (!math::ordered_greater(length.x(), distance_threshold.get())) return;
    const float biased = math::add(length.x(), margin_length_bias.get());
    const float scaled_margin = math::multiply(biased, state.safe_margin_rate);
    const float margin = math::minimum(scaled_margin, state.safe_margin_max);
    const float remaining = math::subtract(length.x(), margin);
    const Vec4 scaled = math::multiply(difference, math::broadcast(remaining));
    const Vec4 reciprocal = math::divide(unit_vector.get(), math::broadcast<0>(length));
    state.transform.position = math::add(origin, math::multiply(reciprocal, scaled));
}

} // namespace camera::update::validation_margin
