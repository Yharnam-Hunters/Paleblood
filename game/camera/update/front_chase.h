// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera front chase stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
#include "locked_geometry.h"

namespace camera::update::front_chase
{
namespace scalar = engine::camera_control_math;
namespace arithmetic = engine::camera_target_math;
namespace front_math = engine::camera_front_chase_math;
namespace locked_math = engine::camera_locked_geometry_math;
using update::angular_mode::AngularContinuation;
using update::angular_mode::AngularPreparation;
using update::control::ControlPreparation;
using update::subject::SubjectCompensationState;
using update::locked_geometry::wrap_angle;

static_assert(offsetof(AngularPreparation, front_active) == 0x1);
static_assert(offsetof(ControlPreparation, hand_input_active) == 0x10);
static_assert(offsetof(SubjectCompensationState, saved_time_register) == 0);
static_assert(offsetof(FollowCamera, angles) == 0x140);
static_assert(offsetof(FollowCamera, target_pitch) == 0x150);
static_assert(offsetof(FollowCamera, lock_rotation_chase_rate) == 0x240);
static_assert(offsetof(FollowCamera, front_lock_active) == 0x263);
static_assert(offsetof(FollowCamera, front_lock_remaining) == 0x264);
static_assert(offsetof(FollowCamera, front_lock_angles) == 0x26c);
static_assert(offsetof(FollowCamera, update_target_pitch_at_front_lock) == 0x275);

RT_GLOBAL(0x04d25e58, completion_tolerance, const float); // exact bits3c8efa35

AngularPreparation chase_front_lock(FollowCamera &state, const AngularPreparation &dispatch,
                                    const ControlPreparation &control,
                                    const SubjectCompensationState &previous)
{
    // Entry precondition: dispatch.continuation==front_chase. Retain original AL exactly:
    // a new request forced1; free geometry captured the guest byte, including80/ff.
    uint8_t active = dispatch.front_active;
    const float old_remaining = scalar::capture(state.front_lock_remaining);
    float remaining = scalar::subtract(old_remaining, previous.saved_time_register.x());
    scalar::publish(state.front_lock_remaining, remaining);
    if (!scalar::unordered_or_less_equal(0.0f, remaining))
    {
        // Strictly negative only. Signed zeros and NaN do not take original c758.
        scalar::publish(state.front_lock_active, uint8_t{0});
        scalar::publish(state.front_lock_remaining, 0.0f);
        active = 0;
        remaining = 0.0f;
    }

    const float goal_pitch = scalar::capture(state.front_lock_angles.pitch);
    const float old_pitch = scalar::capture(state.angles.pitch);
    const float chase_rate =
        engine::camera_frame_policy::standard_gain(scalar::capture(state.lock_rotation_chase_rate));
    const float pitch_difference = scalar::subtract(goal_pitch, old_pitch);
    const float period = scalar::capture(update::locked_geometry::two_pi.get());
    const float pitch_delta = wrap_angle(pitch_difference, period);
    const float pitch_movement = scalar::multiply(chase_rate, pitch_delta);
    const float pitch_candidate = arithmetic::add(old_pitch, pitch_movement);
    const float pitch = wrap_angle(pitch_candidate, period);
    scalar::publish(state.angles.pitch, pitch);

    // The original reads current yaw before goal yaw, and reuses the same captured rate.
    const float old_yaw = scalar::capture(state.angles.yaw);
    const float goal_yaw = scalar::capture(state.front_lock_angles.yaw);
    const float yaw_difference = scalar::subtract(goal_yaw, old_yaw);
    const float yaw_delta = wrap_angle(yaw_difference, period);
    const float yaw_movement = scalar::multiply(chase_rate, yaw_delta);
    const float yaw_candidate = arithmetic::add(old_yaw, yaw_movement);
    const float yaw = wrap_angle(yaw_candidate, period);
    scalar::publish(state.angles.yaw, yaw);
    if (scalar::capture(state.update_target_pitch_at_front_lock) != 0)
    {
        scalar::publish(state.target_pitch, pitch);
    }

    // Compare against the pre-chase goal snapshots. NaN/equality takes JBE and keeps active.
    const float pitch_error = wrap_angle(scalar::subtract(pitch, goal_pitch), period);
    const float absolute_pitch_error = locked_math::absolute(pitch_error);
    const float tolerance = scalar::capture(completion_tolerance.get());
    if (!scalar::unordered_or_less_equal(tolerance, absolute_pitch_error))
    {
        const float yaw_error = wrap_angle(scalar::subtract(yaw, goal_yaw), period);
        const float absolute_yaw_error = locked_math::absolute(yaw_error);
        if (!scalar::unordered_or_less_equal(tolerance, absolute_yaw_error))
        {
            scalar::publish(state.front_lock_active, uint8_t{0});
            scalar::publish(state.front_lock_remaining, 0.0f);
            remaining = 0.0f;
            active = 0;
        }
    }

    if (control.hand_input_active && front_math::ordered_at_least(0.0f, remaining))
    {
        // Reached cold cb19..cb30 then c1c2: preserve these repeated byte/dword stores,
        // even when expiry or angle completion already cleared both fields above.
        scalar::publish(state.front_lock_active, uint8_t{0});
        scalar::publish(state.front_lock_remaining, 0.0f);
        active = 0;
    }
    // The next stage executes rotation release with original AL; nonzero means this
    // frame's front chase remains active. All prior carriers, including full time, survive.
    return {AngularContinuation::rotation_release, active};
}

} // namespace camera::update::front_chase
