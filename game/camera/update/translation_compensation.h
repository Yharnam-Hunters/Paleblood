// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera translation compensation stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
#include "math_calls.h"
#include "../../engine/camera_update/reciprocal_root.h"

namespace camera::update::translation_compensation
{
inline constexpr uint32_t minimum_fulcrum_distance_bits = 0x3f000000;
namespace math = engine::camera_translation_compensation_math;
namespace root = engine::camera_root;

RT_GLOBAL(0x04d25e6c, scalar_one, const float);
RT_GLOBAL(0x04d25e70, distance_floor, const float);
RT_GLOBAL(0x04d25e88, distance_division_threshold, const float);

// Integration carriers. These are already captured values, not guest pointers.
// The containing integration must keep this carrier disjoint from guest FollowCamera.
struct TranslationCompensationInput
{
    Vec4 fulcrum;                   // AngleFramePreparation.fulcrum, original XMM1
    Vec4 half;                      // AngleFramePreparation.half, original XMM8
    Vec4 three;                     // AngleFramePreparation.three, original XMM9
    Vec4 previous_subject_position; // previous.subject_position, original [RBP-0x260]
    Vec4 previous_camera_axis_x;    // previous.camera_basis.axis_x, original [RBP-0x290]
};

struct TranslationCompensationPreparation
{
    Vec4 fulcrum; // Full original XMM1 at 0x0183d35b, already published
    Vec4 half;    // Original XMM8 remains captured from the angle frame
    Vec4 three;   // Original XMM9 remains captured from the angle frame
};
static_assert(sizeof(TranslationCompensationInput) == 0x50 && alignof(TranslationCompensationInput) == 16);
static_assert(sizeof(TranslationCompensationPreparation) == 0x30 &&
              alignof(TranslationCompensationPreparation) == 16);
static_assert(offsetof(FollowCamera, subject_transform) + offsetof(Matrix4, position) == 0x90);
static_assert(offsetof(FollowCamera, transform) + offsetof(Matrix4, position) == 0x40);
static_assert(offsetof(FollowCamera, cast_origin) == 0xc0);
static_assert(offsetof(FollowCamera, fulcrum) == 0xe0);
static_assert(offsetof(FollowCamera, fulcrum_lateral_move_max_distance) == 0x170);
static_assert(offsetof(FollowCamera, fulcrum_lateral_move_min_rate) == 0x174);
static_assert(offsetof(FollowCamera, fulcrum_lateral_move_max_rate) == 0x178);
static_assert(offsetof(FollowCamera, fulcrum_distance) == 0x180);

TranslationCompensationPreparation compensate_translation(FollowCamera &state,
                                                          const TranslationCompensationInput &input)
{
    const Vec4 prepared_fulcrum = input.fulcrum;
    const Vec4 half = input.half;
    const Vec4 three = input.three;
    Vec4 cast_origin;
    float scale;
    const float maximum_distance = math::capture(state.fulcrum_lateral_move_max_distance);
    // JBE bypasses the update for negative, zero and unordered maximum-distance inputs.
    if (math::ordered_greater(maximum_distance, 0.0f))
    {
        const Vec4 subject_movement =
            math::subtract(math::capture(state.subject_transform.position), input.previous_subject_position);
        const float x = math::multiply(input.previous_camera_axis_x.x(), subject_movement.x());
        const float z = math::multiply(input.previous_camera_axis_x.z(), subject_movement.z());
        const float projected_movement = math::absolute_bits(math::add(x, z));
        const float ratio = math::divide(projected_movement, maximum_distance);
        float rate = math::capture(state.fulcrum_lateral_move_min_rate);
        // JA retains min-rate only for ordered min-rate > ratio. Unordered takes MINSS.
        if (!math::ordered_greater(rate, ratio))
        {
            rate = math::minimum(math::capture(state.fulcrum_lateral_move_max_rate), ratio);
        }
        const float retained_rate = math::subtract(math::capture(scalar_one.get()), rate);
        const Vec4 camera_position = math::capture(state.transform.position);
        cast_origin = math::capture(state.cast_origin);
        const Vec4 camera_length =
            root::refined_length(math::squared_xyz_length(math::subtract(camera_position, cast_origin)), half,
                                 three, root::FirstProductOrder::reciprocal_then_length);
        const float distance = math::multiply(retained_rate, camera_length.x());
        math::publish(state.fulcrum_distance, distance);
        const bool below_floor = math::ordered_greater(math::capture(distance_floor.get()), distance);
        // Original captures this second one before the possible immediate half store.
        scale = math::capture(scalar_one.get());
        if (below_floor) math::publish_bits(state.fulcrum_distance, minimum_fulcrum_distance_bits);
    }
    else
    {
        cast_origin = math::capture(state.cast_origin);
        scale = math::capture(scalar_one.get());
    }
    const Vec4 difference = math::subtract(prepared_fulcrum, cast_origin);
    const Vec4 length = root::refined_length(math::squared_xyz_length(difference), half, three,
                                             root::FirstProductOrder::reciprocal_then_length);
    // Equality, short lengths and unordered lengths retain the already captured scale.
    if (math::ordered_greater(length.x(), math::capture(distance_division_threshold.get())))
    {
        scale = math::divide(math::capture(state.fulcrum_distance), length.x());
    }
    const Vec4 displacement = math::multiply(difference, math::broadcast(scale));
    const Vec4 fulcrum = math::add(cast_origin, displacement);
    math::publish(state.fulcrum, fulcrum);
    return {fulcrum, half, three};
}

} // namespace camera::update::translation_compensation
