// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera angular mode stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// explicitly dispatch locked geometry atbd42, front chase atc736, or rotation release atc949.
#include "target.h"

namespace camera::update::angular_mode
{
namespace packed = engine::camera_subject_math;
namespace scalar = engine::camera_control_math;
namespace angular_math = engine::camera_angular_mode_math;
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
    locked_geometry,  // original0x0183bd42: lock target geometry/polynomial work starts next
    front_chase,      // original0x0183c736: decrement front remaining and chase its angles next
    rotation_release, // original0x0183c949: bypass front chase, continue rotation release/input
};

struct AngularPreparation
{
    AngularContinuation continuation;
    // Original AL at front-chase/release entries. For locked_geometry there is no live
    // active flag yet; its zero retained value is not consumed on that discriminated path.
    uint8_t front_active;
};

float yaw_or_existing(Vec4 direction, const float &existing)
{
    if (!packed::is_ordered_zero(direction.x()) || !packed::is_ordered_zero(direction.z()))
    {
        return atan2_float(direction.x(), direction.z());
    }
    return scalar::capture(existing);
}

void apply_reset_angles(FollowCamera &state, Vec4 fresh_forward)
{
    if (scalar::capture(state.reset_angles_requested) != 0)
    {
        scalar::publish(state.reset_angles_requested, uint8_t{0});
        const float pitch = scalar::capture(state.reset_angles.pitch);
        scalar::publish(state.angles.pitch, pitch);
        const float yaw = scalar::capture(state.reset_angles.yaw);
        scalar::publish(state.angles.yaw, yaw);
        scalar::publish(state.front_lock_active, uint8_t{0});
        scalar::publish(state.front_lock_angles.pitch, pitch);
        scalar::publish(state.front_lock_angles.yaw, yaw);
        scalar::publish(state.target_pitch, pitch);
        return;
    }

    scalar::publish(state.angles.pitch, 0.0f);
    // The callback may change pitch or other camera fields after its initial zero store.
    const float yaw = yaw_or_existing(fresh_forward, state.angles.yaw);
    scalar::publish(state.angles.yaw, yaw);
    scalar::publish(state.front_lock_active, uint8_t{0});
    scalar::publish(state.front_lock_angles.pitch, 0.0f);
    scalar::publish(state.front_lock_angles.yaw, yaw);
    scalar::publish(state.target_pitch, 0.0f);
}

void begin_front_lock(FollowCamera &state, Vec4 fresh_forward)
{
    scalar::publish(state.front_lock_active, uint8_t{1});
    scalar::publish(state.front_lock_angles.pitch, 0.0f);
    const float yaw = yaw_or_existing(fresh_forward, state.front_lock_angles.yaw);
    scalar::publish(state.front_lock_angles.yaw, yaw);
    // This selector is read after atan2 returns; do not retain an earlier guest copy.
    if (scalar::capture(state.update_target_pitch_at_front_lock) != 0)
    {
        const float current_pitch = scalar::capture(state.angles.pitch);
        scalar::publish(state.target_pitch, current_pitch);
    }
    else
    {
        const float target_pitch = scalar::capture(state.target_pitch);
        scalar::publish(state.front_lock_angles.pitch, target_pitch);
    }
    const float duration = scalar::capture(state.front_lock_duration);
    scalar::publish(state.front_lock_remaining, duration);
}

uint8_t prepare_free_angles(FollowCamera &state, Vec4 fulcrum)
{
    const Vec4 orbit = packed::capture(state.orbit_origin);
    const Vec4 difference = packed::subtract(orbit, fulcrum);
    const float horizontal = angular_math::horizontal_length(difference);
    float pitch;
    if (!packed::is_ordered_zero(difference.y()) || !packed::is_ordered_zero(horizontal))
    {
        const float angle = atan2_float(difference.y(), horizontal);
        pitch = angular_math::xor_low(angle, pitch_sign_bits.get());
    }
    else
    {
        pitch = scalar::capture(state.angles.pitch);
    }
    scalar::publish(state.angles.pitch, pitch);
    // Keep the pre-pitch-call delta for yaw even if that callback changes orbit or fulcrum.
    const float yaw = yaw_or_existing(difference, state.angles.yaw);
    scalar::publish(state.angles.yaw, yaw);
    return scalar::capture(state.front_lock_active);
}

AngularPreparation prepare_angular_mode(FollowCamera &state, const TargetPreparation &target,
                                        const FreshSubjectTransformState &fresh)
{
    // This is original BL captured before the preceding tangent callback, not a guest reread.
    if (target.reset_requested != 0)
    {
        apply_reset_angles(state, fresh.fresh_transform.forward);
        return {AngularContinuation::rotation_release, 0};
    }
    if (scalar::capture(state.lock_requested) != 0)
    {
        return {AngularContinuation::locked_geometry, 0};
    }
    if (scalar::capture(state.front_lock_requested) != 0)
    {
        begin_front_lock(state, fresh.fresh_transform.forward);
        // Original c734 assigns AL1 even if atan2 changed guest front_lock_active.
        return {AngularContinuation::front_chase, 1};
    }
    const uint8_t active = prepare_free_angles(state, target.fulcrum);
    if (active != 0) return {AngularContinuation::front_chase, active};
    return {AngularContinuation::rotation_release, 0};
}

} // namespace camera::update::angular_mode
