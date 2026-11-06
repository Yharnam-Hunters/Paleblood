// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera translation compensation stage, included only by update.cpp.
#pragma once
#include "math.h"
#include "math_calls.h"

namespace camera::update::translation_compensation
{
inline constexpr uint32_t minimum_fulcrum_distance_bits = 0x3f000000;
namespace math = camera::update::math;
namespace root = camera::update::root;

RT_GLOBAL(0x04d25e6c, scalar_one, const float);
RT_GLOBAL(0x04d25e70, distance_floor, const float);
RT_GLOBAL(0x04d25e88, distance_division_threshold, const float);

// Saved values from earlier stages. They remain independent of later camera field writes.
struct TranslationCompensationInput
{
    Vec4 fulcrum;

    Vec4 previous_subject_position;
    Vec4 previous_camera_axis_x;
};

struct TranslationCompensationPreparation
{
    Vec4 fulcrum;
};
static_assert(sizeof(TranslationCompensationInput) == 0x30 && alignof(TranslationCompensationInput) == 16);
static_assert(sizeof(TranslationCompensationPreparation) == 0x10 &&
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

    Vec4 cast_origin;
    float scale;
    const float maximum_distance = state.fulcrum_lateral_move_max_distance;
    // A nonpositive or NaN maximum distance leaves lateral compensation unchanged.
    if (math::ordered_greater(maximum_distance, 0.0f))
    {
        const Vec4 subject_movement =
            math::subtract(state.subject_transform.position, input.previous_subject_position);
        const float x = math::multiply(input.previous_camera_axis_x.x(), subject_movement.x());
        const float z = math::multiply(input.previous_camera_axis_x.z(), subject_movement.z());
        const float projected_movement = math::absolute(math::add(x, z));
        const float ratio = math::divide(projected_movement, maximum_distance);
        float rate = state.fulcrum_lateral_move_min_rate;
        // A ratio ordered below the minimum keeps that rate. Otherwise cap the ratio,
        // retaining a NaN ratio rather than replacing its payload.
        if (!math::ordered_greater(rate, ratio))
        {
            rate = math::minimum(state.fulcrum_lateral_move_max_rate, ratio);
        }
        const float retained_rate = math::subtract(scalar_one.get(), rate);
        const Vec4 camera_position = state.transform.position;
        cast_origin = state.cast_origin;
        const Vec4 camera_length =
            root::refined_length(math::squared_xyz_length(math::subtract(camera_position, cast_origin)), root::FirstProductOrder::reciprocal_then_length);
        const float distance = math::multiply(retained_rate, camera_length.x());
        state.fulcrum_distance = distance;
        const bool below_floor = math::ordered_greater(distance_floor.get(), distance);
        // Capture the fallback scale before clamping the stored fulcrum distance.
        scale = scalar_one.get();
        if (below_floor) state.fulcrum_distance = std::bit_cast<float>(minimum_fulcrum_distance_bits);
    }
    else
    {
        cast_origin = state.cast_origin;
        scale = scalar_one.get();
    }
    const Vec4 difference = math::subtract(prepared_fulcrum, cast_origin);
    const Vec4 length = root::refined_length(math::squared_xyz_length(difference),
                                             root::FirstProductOrder::reciprocal_then_length);
    // Equality, short lengths and unordered lengths retain the already captured scale.
    if (math::ordered_greater(length.x(), distance_division_threshold.get()))
    {
        scale = math::divide(state.fulcrum_distance, length.x());
    }
    const Vec4 displacement = math::multiply(difference, math::broadcast(scale));
    const Vec4 fulcrum = math::add(cast_origin, displacement);
    state.fulcrum = fulcrum;
    return {fulcrum};
}

} // namespace camera::update::translation_compensation
