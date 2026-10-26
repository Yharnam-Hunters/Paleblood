// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera target stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Chase subject position, establish offset targets (including optional FOV-height adjustment),
// copy target/camera distance and move the fulcrum/candidate. No whole-update entry or hook.
#include "control.h"
#include "subject_transform.h"

namespace camera::update::target
{
namespace packed = engine::camera_subject_math;
namespace scalar = engine::camera_control_math;
namespace target_math = engine::camera_target_math;
namespace root = engine::camera_root;
using update::subject::SubjectCompensationState;
using update::subject_transform::FreshSubjectTransformState;

RT_GLOBAL(0x04cf24f0, half_vector, const Vec4);
RT_GLOBAL(0x04cf2500, three_vector, const Vec4);
RT_GLOBAL(0x04d25e70, half_fov, const float);
RT_GLOBAL(0x04d25e6c, scalar_one, const float);
RT_ORIGINAL(0x02fbe648, tangent_float, float(float));

struct TargetPreparation
{
    uint8_t reset_requested; // original BL, captured before optional tangent callback
    Vec4 subject_position;   // original XMM4: chased position, not fresh unchased position
    Vec4 fulcrum;            // original XMM2 at0x0183bcd0
    Vec4 candidate_position; // original XMM0 at0x0183bcd0
};

Vec4 camera_to_cast_length(const SubjectCompensationState &previous)
{
    const Vec4 difference = packed::subtract(previous.camera_position, previous.cast_origin);
    const Vec4 squares = packed::multiply(difference, difference);
    const Vec4 x = packed::broadcast<0>(squares);
    const Vec4 y = packed::broadcast<1>(squares);
    const Vec4 z = packed::broadcast<2>(squares);
    const Vec4 xy = packed::add(x, y);
    const Vec4 squared_distance = packed::add(z, xy);
    const Vec4 half = half_vector.get();
    const Vec4 three = three_vector.get();
    // Original root0x0183bb38 is one of the two length-first correction sites.
    return root::refined_length(squared_distance, half, three,
                                root::FirstProductOrder::length_then_reciprocal);
}

TargetPreparation prepare_target_translation(FollowCamera &state, const SubjectCompensationState &previous,
                                             const FreshSubjectTransformState &fresh)
{
    // Retain this byte across tangent_float: the next angular-mode branch uses captured BL,
    // even when the callback changes the corresponding guest flag after this read.
    const uint8_t reset = scalar::capture(state.reset_requested);
    const Matrix4 frame = fresh.fresh_transform;
    Vec4 subject_position = frame.position;
    if (reset == 0)
    {
        const Vec4 difference = packed::subtract(frame.position, previous.subject_position);
        const float chase_rate =
            engine::camera_frame_policy::standard_gain(scalar::capture(state.subject_translation_chase_rate));
        const Vec4 movement = packed::multiply(difference, target_math::broadcast(chase_rate));
        subject_position = packed::add(previous.subject_position, movement);
        packed::publish(state.subject_transform.position, subject_position);
    }

    const Vec4 origin_offset = scalar::capture_input(state.subject_origin_offset);
    const Vec4 x_offset = packed::multiply(frame.axis_x, packed::broadcast<0>(origin_offset));
    const Vec4 reference_x = packed::add(frame.position, x_offset);
    const Vec4 y_offset = packed::multiply(frame.axis_y, packed::broadcast<1>(origin_offset));
    const Vec4 reference_xy = packed::add(y_offset, reference_x);
    const Vec4 z_offset = packed::multiply(frame.forward, packed::broadcast<2>(origin_offset));
    packed::publish(state.target_reference, packed::add(z_offset, reference_xy));

    const Vec4 partial_target = packed::add(subject_position, x_offset);
    const Vec4 target_xy = packed::add(y_offset, partial_target);
    packed::publish(state.target, packed::add(z_offset, target_xy));
    if (scalar::capture(state.fov_target_height_adjustment) != 0)
    {
        const Vec4 distance = camera_to_cast_length(previous);
        const float fov = scalar::capture(state.vertical_fov);
        const float half_fov_angle = scalar::multiply(fov, scalar::capture(half_fov.get()));
        const float tangent = tangent_float(half_fov_angle);
        // All values used below were captured/computed before the callback, including
        // frame.axis_y, partial_target, z_offset and origin_offset.Y. Do not reload them.
        const float fov_height = scalar::multiply(distance.x(), tangent);
        const float old_height = origin_offset.y();
        // Original JBE skips ordered <= and unordered; only ordered > adjusts the target.
        if (!scalar::unordered_or_less_equal(fov_height, old_height))
        {
            const float difference = scalar::subtract(fov_height, old_height);
            const float halfway = scalar::multiply(difference, scalar::capture(half_fov.get()));
            const float height = target_math::add(old_height, halfway);
            const Vec4 adjusted_y = packed::multiply(frame.axis_y, target_math::broadcast(height));
            // This ADD's order differs from the initial target's y_offset + partial_target.
            const Vec4 adjusted_xy = packed::add(partial_target, adjusted_y);
            packed::publish(state.target, packed::add(z_offset, adjusted_xy));
        }
    }

    // Reload the guest target twice after the callback/optional publication. The first
    // copy's cast-origin store precedes the second read used for the orbit-origin store.
    packed::publish(state.cast_origin, packed::capture(state.target));
    packed::publish(state.orbit_origin, packed::capture(state.target));
    const float target_camera_distance = scalar::capture(state.target_camera_distance);
    scalar::publish(state.camera_distance, target_camera_distance);

    const float chase_rate = scalar::capture(state.target_translation_chase_rate);
    const float complement = scalar::subtract(scalar::capture(scalar_one.get()), chase_rate);
    const float end_rate = scalar::capture(state.translation_control_end_rate);
    const float end_movement = scalar::multiply(complement, end_rate);
    const float movement_rate =
        engine::camera_frame_policy::blended_translation_gain(target_math::add(chase_rate, end_movement));
    const Vec4 subject_delta = packed::subtract(subject_position, previous.subject_position);
    const Vec4 movement = packed::multiply(subject_delta, target_math::broadcast(movement_rate));
    const Vec4 fulcrum = packed::add(movement, packed::capture(state.fulcrum));
    packed::publish(state.fulcrum, fulcrum);
    const Vec4 candidate = packed::add(movement, packed::capture(state.candidate_position));
    packed::publish(state.candidate_position, candidate);
    return {reset, subject_position, fulcrum, candidate};
}

} // namespace camera::update::target
