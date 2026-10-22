// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera validation margin stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Territory avoidance is the intervening continuation and is deliberately separate.
#include "final_collision.h"

namespace camera::update::validation_margin
{
namespace math = engine::camera_orientation_math;
namespace root = engine::camera_root;
namespace direction = camera::update::wing_escape;
RT_GLOBAL(0x04cf24f0, half_vector, const Vec4);
RT_GLOBAL(0x04cf2500, three_vector, const Vec4);
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
    Vec4 position; // retained originalXMM1, either current camera position or saved camera position
    Vec4 half;     // originalXMM9, captured after first axis validation
    Vec4 three;    // originalXMM10
};
static_assert(sizeof(ValidationPreparation) == 0x30);

ValidationPreparation validate_camera(FollowCamera &state, const RollbackPreparation &previous)
{
    math::publish(state.reset_requested, uint8_t{0});
    math::publish(state.lock_requested, uint8_t{0});
    math::publish(state.boss_lock_requested, uint8_t{0});
    math::publish(state.front_lock_requested, uint8_t{0});
    const Vec4 right = math::capture(state.transform.axis_x);
    const Vec4 exponents = math::capture(exponent_bits.get());
    const bool bad_right = math::contains_nan(right, exponents);
    const Vec4 half = math::capture(half_vector.get());
    const Vec4 three = math::capture(three_vector.get());
    const bool bad_up = math::contains_nan(math::capture(state.transform.axis_y), exponents);
    const bool bad_forward = math::contains_nan(math::capture(state.transform.forward), exponents);
    Vec4 position = math::capture(state.transform.position);
    const bool bad_position = math::contains_nan(position, exponents);
    if (bad_right || bad_up || bad_forward || bad_position)
    {
        math::publish(state.transform.axis_x, previous.camera_transform.axis_x);
        math::publish(state.transform.axis_y, previous.camera_transform.axis_y);
        math::publish(state.transform.forward, previous.camera_transform.forward);
        position = previous.camera_transform.position;
        math::publish(state.transform.position, position);
        math::publish(state.subject_transform.axis_x, previous.subject_transform.axis_x);
        math::publish(state.subject_transform.axis_y, previous.subject_transform.axis_y);
        math::publish(state.subject_transform.forward, previous.subject_transform.forward);
        math::publish(state.subject_transform.position, previous.subject_transform.position);
        math::publish(state.target, previous.target);
        math::publish(state.cast_origin, previous.cast_origin);
        math::publish(state.orbit_origin, previous.orbit_origin);
        math::publish(state.fulcrum, previous.fulcrum);
        math::publish(state.candidate_position, previous.candidate);
        math::publish(state.chased_position, previous.chased);
    }
    return {position, half, three};
}

void apply_safe_margin(FollowCamera &state, ValidationPreparation retained)
{
    const Vec4 origin = math::capture(state.cast_origin);
    const Vec4 difference = math::subtract(retained.position, origin);
    const Vec4 length = root::refined_length(math::squared_xyz_length(difference), retained.half,
                                             retained.three, root::FirstProductOrder::reciprocal_then_length);
    if (!math::ordered_greater(length.x(), math::capture(distance_threshold.get()))) return;
    const float biased = math::add(length.x(), math::capture(margin_length_bias.get()));
    const float scaled_margin = math::multiply(biased, math::capture(state.safe_margin_rate));
    const float margin = math::minimum(scaled_margin, math::capture(state.safe_margin_max));
    const float remaining = math::subtract(length.x(), margin);
    const Vec4 scaled = math::multiply(difference, math::scalar_broadcast(remaining));
    const Vec4 reciprocal = math::divide(math::capture(unit_vector.get()), math::broadcast<0>(length));
    math::publish(state.transform.position, math::add(origin, math::multiply(reciprocal, scaled)));
}

} // namespace camera::update::validation_margin
