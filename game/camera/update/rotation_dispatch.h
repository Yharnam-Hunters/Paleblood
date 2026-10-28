// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera rotation dispatch stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Exits to free-input preparation atcab9 or angle-frame construction atcf2e.
// Rotation release, complete locked adjustment, and the reached target-pitch tail only.
#include "front_chase.h"

namespace camera::update::rotation_dispatch
{
namespace scalar = engine::camera_control_math;
namespace arithmetic = engine::camera_target_math;
namespace front_math = engine::camera_front_chase_math;
namespace rotation_math = engine::camera_rotation_dispatch_math;
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
    free_input,  // original0x0183cab9: its hand-input/geometry/high-speed stage starts next
    angle_frame, // original0x0183cf2e: the original begins pitch sine/cosine construction
};

struct RotationDispatch
{
    RotationContinuation continuation;
    // Original R14B captured atc99e. A future free-input atan2 callback may change guest260;
    // the shared tail still tests this captured byte, rather than reloading that field.
    uint8_t captured_lock;
};

static_assert(offsetof(RotationDispatch, captured_lock) == 0x1);
static_assert(offsetof(AngularPreparation, front_active) == 0x1);
static_assert(offsetof(TargetPreparation, reset_requested) == 0);
static_assert(offsetof(SubjectCompensationState, saved_time_register) == 0);

inline void release_rotation_control(FollowCamera &state, float delta_time)
{
    const float old_wait = scalar::capture(state.rotation_control_wait_remaining);
    const float difference = scalar::subtract(old_wait, delta_time);
    const float remaining = rotation_math::maximum(0.0f, difference);
    scalar::publish(state.rotation_control_wait_remaining, remaining);
    // Original JB skips positive AND unordered; ordered0>=remaining continues.
    if (front_math::ordered_at_least(0.0f, remaining))
    {
        // Original DIV reads release time before the current rate load.
        const float release_time = scalar::capture(state.rotation_control_end_release_time);
        const float step = scalar::divide(delta_time, release_time);
        const float old_rate = scalar::capture(state.rotation_control_rate);
        const float rate = scalar::subtract(old_rate, step);
        scalar::publish(state.rotation_control_rate, rate);
        if (!scalar::unordered_or_less_equal(0.0f, rate))
        {
            scalar::publish(state.rotation_control_rate, 0.0f);
        }
    }
}

inline void apply_locked_adjustment(FollowCamera &state, Vec4 control_input)
{
    const float maximum_pitch = scalar::capture(state.maximum_lock_adjustment.pitch);
    const float target_pitch = scalar::multiply(control_input.x(), maximum_pitch);
    const float old_pitch_adjustment = scalar::capture(state.lock_adjustment.pitch);
    const float rate = scalar::capture(state.lock_adjustment_chase_rate);
    const float pitch_difference = scalar::subtract(target_pitch, old_pitch_adjustment);
    const float pitch_step = scalar::multiply(pitch_difference, rate);
    const float pitch_adjustment = arithmetic::add(old_pitch_adjustment, pitch_step);
    scalar::publish(state.lock_adjustment.pitch, pitch_adjustment);

    const float maximum_yaw = scalar::capture(state.maximum_lock_adjustment.yaw);
    const float target_yaw = scalar::multiply(control_input.y(), maximum_yaw);
    const float old_yaw_adjustment = scalar::capture(state.lock_adjustment.yaw);
    const float yaw_difference = scalar::subtract(target_yaw, old_yaw_adjustment);
    const float yaw_step = scalar::multiply(yaw_difference, rate);
    const float yaw_adjustment = arithmetic::add(old_yaw_adjustment, yaw_step);
    scalar::publish(state.lock_adjustment.yaw, yaw_adjustment);

    // Both adjustment publications precede current pitch's read. Adjustment is source1.
    const float current_pitch = scalar::capture(state.angles.pitch);
    const float pitch_candidate = arithmetic::add(pitch_adjustment, current_pitch);
    const float period = scalar::capture(update::locked_geometry::two_pi.get());
    scalar::publish(state.angles.pitch, wrap_angle(pitch_candidate, period));
    // Current yaw is read only after chased pitch's publication.
    const float current_yaw = scalar::capture(state.angles.yaw);
    const float yaw_candidate = arithmetic::add(yaw_adjustment, current_yaw);
    scalar::publish(state.angles.yaw, wrap_angle(yaw_candidate, period));
}

// Original shared ce97..cf2d. Future free-input reconstruction should invoke this same tail
// after its own effects/callbacks, with R14B captured atc99e and a late guest263 read.
inline void finish_rotation_input(FollowCamera &state, uint8_t captured_lock)
{
    if (captured_lock != 0) return;
    if (scalar::capture(state.front_lock_active) != 0) return;
    const float current_pitch = scalar::capture(state.angles.pitch);
    const float target_pitch = scalar::capture(state.target_pitch);
    const float difference = scalar::subtract(current_pitch, target_pitch);
    const float period = scalar::capture(update::locked_geometry::two_pi.get());
    const float delta = wrap_angle(difference, period);
    // This field is read after the first wrap; original keeps target_pitch across it.
    const float rate =
        engine::camera_frame_policy::complement_gain(scalar::capture(state.target_pitch_chase_rate));
    const float movement = scalar::multiply(delta, rate);
    const float candidate = arithmetic::add(target_pitch, movement);
    scalar::publish(state.angles.pitch, wrap_angle(candidate, period));
}

RotationDispatch prepare_rotation_input(FollowCamera &state, const AngularPreparation &angular,
                                        const TargetPreparation &target, const ControlPreparation &control,
                                        const SubjectCompensationState &previous)
{
    // Preserve full saved time and all other outer stage carriers; only its low lane is used.
    release_rotation_control(state, previous.saved_time_register.x());
    const uint8_t captured_lock = scalar::capture(state.lock_requested);
    if (target.reset_requested == 0)
    {
        if (captured_lock == 0)
        {
            // Original cab1 consumes the carried AL; do not reload guest263 to choose this.
            if (angular.front_active == 0) return {RotationContinuation::free_input, captured_lock};
        }
        else if (scalar::capture(state.adjust_lock_camera) != 0 && angular.front_active == 0)
        {
            apply_locked_adjustment(state, control.control_input);
        }
    }
    finish_rotation_input(state, captured_lock);
    return {RotationContinuation::angle_frame, captured_lock};
}

} // namespace camera::update::rotation_dispatch
