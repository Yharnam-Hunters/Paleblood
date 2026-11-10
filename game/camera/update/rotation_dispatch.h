// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera rotation dispatch stage, included only by update.cpp.
#pragma once
#include "math.h"
// Release rotation control, apply lock adjustment, then select free input or
// angle-frame construction.
#include "front_chase.h"

namespace camera::update::rotation_dispatch
{
namespace scalar = camera::update::math;
namespace arithmetic = camera::update::math;
namespace front_math = camera::update::math;
namespace rotation_math = camera::update::math;
using update::angular_mode::AngularPreparation;
using update::control::ControlPreparation;
using update::subject::SubjectCompensationState;
using update::target::TargetPreparation;
using update::locked_geometry::wrap_angle;

static_assert(offsetof(FollowCamera, rotation_control_rate) == 0x130);
static_assert(offsetof(FollowCamera, rotation_control_wait_remaining) == 0x134);
static_assert(offsetof(FollowCamera, rotation_control_end_release_time) == 0x1e4);
static_assert(offsetof(FollowCamera, target_pitch_chase_rate) == 0x1a8);
static_assert(offsetof(FollowCamera, angles) == 0x140);
static_assert(offsetof(FollowCamera, target_pitch) == 0x150);
static_assert(offsetof(FollowCamera, lock_requested) == 0x260);
static_assert(offsetof(FollowCamera, front_lock_active) == 0x263);
static_assert(offsetof(FollowCamera, adjust_lock_camera) == 0x276);
static_assert(offsetof(FollowCamera, lock_adjustment) == 0x278);
static_assert(offsetof(FollowCamera, maximum_lock_adjustment) == 0x280);
static_assert(offsetof(FollowCamera, lock_adjustment_chase_rate) == 0x288);

enum class RotationContinuation : uint8_t
{
    free_input,  // Apply hand input and high-speed rotation.
    angle_frame, // Construct the camera orientation from its angles.
};

struct RotationDispatch
{
    RotationContinuation continuation;
    // The free-input atan2 callback can change lock_requested after this snapshot;
    // the shared tail still tests this captured byte, rather than reloading that field.
    uint8_t captured_lock;
};

static_assert(offsetof(RotationDispatch, captured_lock) == 0x1);
static_assert(offsetof(AngularPreparation, front_active) == 0x1);
static_assert(offsetof(TargetPreparation, reset_requested) == 0);
static_assert(offsetof(SubjectCompensationState, saved_time_register) == 0);

inline void release_rotation_control(FollowCamera &state, float delta_time)
{
    const float old_wait = state.rotation_control_wait_remaining;
    const float difference = scalar::subtract(old_wait, delta_time);
    const float remaining = rotation_math::maximum(0.0f, difference);
    state.rotation_control_wait_remaining = remaining;
    // Only an expired, ordered wait releases the rate; a NaN wait is retained.
    if (front_math::ordered_at_least(0.0f, remaining))
    {
        // Read the release duration before the rate being reduced.
        const float release_time = state.rotation_control_end_release_time;
        const float step = scalar::divide(delta_time, release_time);
        const float old_rate = state.rotation_control_rate;
        const float rate = scalar::subtract(old_rate, step);
        state.rotation_control_rate = rate;
        if (!scalar::unordered_or_less_equal(0.0f, rate))
        {
            state.rotation_control_rate = 0.0f;
        }
    }
}

inline void apply_locked_adjustment(FollowCamera &state, Vec4 control_input)
{
    const float maximum_pitch = state.maximum_lock_adjustment.pitch;
    const float target_pitch = scalar::multiply(control_input.x(), maximum_pitch);
    const float old_pitch_adjustment = state.lock_adjustment.pitch;
    const float rate = state.lock_adjustment_chase_rate;
    const float pitch_difference = scalar::subtract(target_pitch, old_pitch_adjustment);
    const float pitch_step = scalar::multiply(pitch_difference, rate);
    const float pitch_adjustment = arithmetic::add(old_pitch_adjustment, pitch_step);
    state.lock_adjustment.pitch = pitch_adjustment;

    const float maximum_yaw = state.maximum_lock_adjustment.yaw;
    const float target_yaw = scalar::multiply(control_input.y(), maximum_yaw);
    const float old_yaw_adjustment = state.lock_adjustment.yaw;
    const float yaw_difference = scalar::subtract(target_yaw, old_yaw_adjustment);
    const float yaw_step = scalar::multiply(yaw_difference, rate);
    const float yaw_adjustment = arithmetic::add(old_yaw_adjustment, yaw_step);
    state.lock_adjustment.yaw = yaw_adjustment;

    // Publish both adjustments before reading pitch, preserving the NaN operand order.
    const float current_pitch = state.angles.pitch;
    const float pitch_candidate = arithmetic::add(pitch_adjustment, current_pitch);
    const float period = update::locked_geometry::two_pi.get();
    state.angles.pitch = wrap_angle(pitch_candidate, period);
    // Current yaw is read only after chased pitch's publication.
    const float current_yaw = state.angles.yaw;
    const float yaw_candidate = arithmetic::add(yaw_adjustment, current_yaw);
    state.angles.yaw = wrap_angle(yaw_candidate, period);
}

// Both rotation paths finish here after their callbacks, using the earlier lock
// snapshot and a fresh read of front_lock_active.
inline void finish_rotation_input(FollowCamera &state, uint8_t captured_lock)
{
    if (captured_lock != 0) return;
    if (state.front_lock_active != 0) return;
    const float current_pitch = state.angles.pitch;
    const float target_pitch = state.target_pitch;
    const float difference = scalar::subtract(current_pitch, target_pitch);
    const float period = update::locked_geometry::two_pi.get();
    const float delta = wrap_angle(difference, period);
    // Read the chase rate after wrapping while retaining the captured target pitch.
    const float rate =
        camera::update::frame_policy::complement_gain(state.target_pitch_chase_rate);
    const float movement = scalar::multiply(delta, rate);
    const float candidate = arithmetic::add(target_pitch, movement);
    state.angles.pitch = wrap_angle(candidate, period);
}

RotationDispatch prepare_rotation_input(FollowCamera &state, const AngularPreparation &angular,
                                        const TargetPreparation &target, const ControlPreparation &control,
                                        const SubjectCompensationState &previous)
{
    // Rotation release uses the delta-time lane of the saved time input.
    release_rotation_control(state, previous.saved_time_register.x());
    const uint8_t captured_lock = state.lock_requested;
    if (target.reset_requested == 0)
    {
        if (captured_lock == 0)
        {
            // Use the earlier front-lock decision even if a callback changed the field.
            if (angular.front_active == 0) return {RotationContinuation::free_input, captured_lock};
        }
        else if (state.adjust_lock_camera != 0 && angular.front_active == 0)
        {
            apply_locked_adjustment(state, control.control_input);
        }
    }
    finish_rotation_input(state, captured_lock);
    return {RotationContinuation::angle_frame, captured_lock};
}

} // namespace camera::update::rotation_dispatch
