// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera angular mode stage, included only by update.cpp.
#pragma once
#include "math.h"
// Select target-locked geometry, chase a front lock, or release rotation control.
#include "target.h"

namespace camera::update::angular_mode
{
namespace packed = camera::update::math;
namespace scalar = camera::update::math;
namespace angular_math = camera::update::math;
using update::subject_transform::FreshSubjectTransformState;
using update::target::TargetPreparation;

static_assert(offsetof(FollowCamera, reset_angles_requested) == 0x13d);
static_assert(offsetof(FollowCamera, angles) == 0x140 && offsetof(FollowCamera, reset_angles) == 0x148);
static_assert(offsetof(FollowCamera, target_pitch) == 0x150);
static_assert(offsetof(FollowCamera, lock_requested) == 0x260 &&
              offsetof(FollowCamera, front_lock_requested) == 0x262);
static_assert(offsetof(FollowCamera, front_lock_active) == 0x263);
static_assert(offsetof(FollowCamera, front_lock_remaining) == 0x264 &&
              offsetof(FollowCamera, front_lock_duration) == 0x268);
static_assert(offsetof(FollowCamera, front_lock_angles) == 0x26c);
static_assert(offsetof(FollowCamera, update_target_pitch_at_front_lock) == 0x275);

RT_GLOBAL(0x04cf2520, pitch_sign_bits, const Vec4);

enum class AngularContinuation : uint8_t
{
    locked_geometry,  // Compute the orientation toward the lock target.
    front_chase,      // Chase the front-lock angles while its timer runs.
    rotation_release, // Continue rotation release without a front chase.
};

struct AngularPreparation
{
    AngularContinuation continuation;
    // Preserve the front-lock flag across later callbacks. Locked geometry does not
    // use this flag, so that continuation carries zero.
    uint8_t front_active;
};

float yaw_or_existing(Vec4 direction, const float &existing)
{
    if (!packed::ordered_zero(direction.x()) || !packed::ordered_zero(direction.z()))
    {
        return atan2_float(direction.x(), direction.z());
    }
    return existing;
}

void apply_reset_angles(FollowCamera &state, Vec4 fresh_forward)
{
    if (state.reset_angles_requested != 0)
    {
        state.reset_angles_requested = uint8_t{0};
        const float pitch = state.reset_angles.pitch;
        state.angles.pitch = pitch;
        const float yaw = state.reset_angles.yaw;
        state.angles.yaw = yaw;
        state.front_lock_active = uint8_t{0};
        state.front_lock_angles.pitch = pitch;
        state.front_lock_angles.yaw = yaw;
        state.target_pitch = pitch;
        return;
    }

    state.angles.pitch = 0.0f;
    // The callback may change pitch or other camera fields after its initial zero store.
    const float yaw = yaw_or_existing(fresh_forward, state.angles.yaw);
    state.angles.yaw = yaw;
    state.front_lock_active = uint8_t{0};
    state.front_lock_angles.pitch = 0.0f;
    state.front_lock_angles.yaw = yaw;
    state.target_pitch = 0.0f;
}

void begin_front_lock(FollowCamera &state, Vec4 fresh_forward)
{
    state.front_lock_active = uint8_t{1};
    state.front_lock_angles.pitch = 0.0f;
    const float yaw = yaw_or_existing(fresh_forward, state.front_lock_angles.yaw);
    state.front_lock_angles.yaw = yaw;
    // This selector is read after atan2 returns; do not retain an earlier guest copy.
    if (state.update_target_pitch_at_front_lock != 0)
    {
        const float current_pitch = state.angles.pitch;
        state.target_pitch = current_pitch;
    }
    else
    {
        const float target_pitch = state.target_pitch;
        state.front_lock_angles.pitch = target_pitch;
    }
    const float duration = state.front_lock_duration;
    state.front_lock_remaining = duration;
}

uint8_t prepare_free_angles(FollowCamera &state, Vec4 fulcrum)
{
    const Vec4 orbit = state.orbit_origin;
    const Vec4 difference = packed::subtract(orbit, fulcrum);
    const float horizontal = angular_math::horizontal_length(difference);
    float pitch;
    if (!packed::ordered_zero(difference.y()) || !packed::ordered_zero(horizontal))
    {
        const float angle = atan2_float(difference.y(), horizontal);
        pitch = angular_math::xor_low(angle, pitch_sign_bits.get());
    }
    else
    {
        pitch = state.angles.pitch;
    }
    state.angles.pitch = pitch;
    // Keep the pre-pitch-call delta for yaw even if that callback changes orbit or fulcrum.
    const float yaw = yaw_or_existing(difference, state.angles.yaw);
    state.angles.yaw = yaw;
    return state.front_lock_active;
}

AngularPreparation prepare_angular_mode(FollowCamera &state, const TargetPreparation &target,
                                        const FreshSubjectTransformState &fresh)
{
    // Use the reset request captured before the tangent callback, which may change it.
    if (target.reset_requested != 0)
    {
        apply_reset_angles(state, fresh.fresh_transform.forward);
        return {AngularContinuation::rotation_release, 0};
    }
    if (state.lock_requested != 0)
    {
        return {AngularContinuation::locked_geometry, 0};
    }
    if (state.front_lock_requested != 0)
    {
        begin_front_lock(state, fresh.fresh_transform.forward);
        // Enter front chase even if the angle calculation changed front_lock_active.
        return {AngularContinuation::front_chase, 1};
    }
    const uint8_t active = prepare_free_angles(state, target.fulcrum);
    if (active != 0) return {AngularContinuation::front_chase, active};
    return {AngularContinuation::rotation_release, 0};
}

} // namespace camera::update::angular_mode
