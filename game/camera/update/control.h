// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera control stage, included only by update.cpp.
#pragma once
#include "math.h"
// Continue from the compensated snapshots and stop before fresh subject-transform work.
#include "subject.h"

namespace camera::update::control
{
namespace scalar = camera::update::math;
namespace packed = camera::update::math;
namespace root = camera::update::root;
using update::subject::SubjectCompensationState;

// Subject input vectors are 16-byte aligned. A null preferred owner selects the
// fallback owner; the chosen owner must exist.
struct ControlInputOwner
{
    uint8_t unknown_0x00[0x40];
    Vec4 input;
};
struct SubjectControlContext
{
    uint8_t unknown_0x00[0x80];
    const ControlInputOwner *fallback;
    uint8_t unknown_0x088[0x1f8];
    const ControlInputOwner *preferred;
};
struct SubjectControlPrefix
{
    uint8_t unknown_0x00[0x58];
    const SubjectControlContext *context;
};
static_assert(offsetof(ControlInputOwner, input) == 0x40);
static_assert(offsetof(SubjectControlContext, fallback) == 0x80);
static_assert(offsetof(SubjectControlContext, preferred) == 0x280);
static_assert(offsetof(SubjectControlPrefix, context) == 0x58);

RT_GLOBAL(0x04d25e70, minimum_fulcrum_distance, const float);

struct ControlPreparation
{
    Vec4 control_input;
    bool hand_input_active;
};

void update_fulcrum_distance(FollowCamera &state, const SubjectCompensationState &previous)
{
    const float rate = state.fulcrum_distance_rate;
    // Only an ordered positive rate updates the distance.
    if (scalar::unordered_or_less_equal(rate, 0.0f)) return;

    const Vec4 difference = packed::subtract(previous.camera_position, previous.cast_origin);
    const Vec4 squares = packed::multiply(difference, difference);
    const Vec4 x = packed::broadcast<0>(squares);
    const Vec4 y = packed::broadcast<1>(squares);
    const Vec4 z = packed::broadcast<2>(squares);
    const Vec4 xy = packed::add(x, y);
    const Vec4 squared_distance = packed::add(z, xy);

    const Vec4 distance =
        root::refined_length(squared_distance, root::FirstProductOrder::reciprocal_then_length);
    const float scaled_distance = scalar::multiply(distance.x(), rate);
    state.fulcrum_distance = scaled_distance;
    const float minimum_distance = minimum_fulcrum_distance.get();
    // Keep a NaN scaled distance; a generic maximum could replace its payload.
    if (!scalar::unordered_or_less_equal(minimum_distance, scaled_distance))
    {
        state.fulcrum_distance = minimum_distance;
    }
}

Vec4 select_control_input(FollowCamera &state, Subject *subject)
{
    if (state.subject_control_input == 0)
    {
        return default_control_input.get();
    }
    const auto *prefix = reinterpret_cast<const SubjectControlPrefix *>(subject);
    const SubjectControlContext *context = prefix->context;
    const ControlInputOwner *owner = context->preferred;
    if (owner == nullptr) owner = context->fallback;
    // Enabling subject input requires the subject, context and selected owner to exist.
    return owner->input;
}

void begin_translation_release_wait(FollowCamera &state)
{
    state.translation_control_end_rate = 1.0f;
    const float wait_time = state.translation_control_end_wait_time;
    state.translation_control_wait_remaining = wait_time;
}

void update_translation_release(FollowCamera &state, float delta)
{
    // The first frame without hand input starts the wait; later frames count it down.
    const uint8_t was_hand_control = state.hand_control;
    state.hand_control = uint8_t{0};
    if (was_hand_control != 0)
    {
        begin_translation_release_wait(state);
        return;
    }

    const float previous_wait = state.translation_control_wait_remaining;
    const float remaining_wait = scalar::subtract(previous_wait, delta);
    state.translation_control_wait_remaining = remaining_wait;
    // A nonnegative or NaN wait keeps the decremented value. NaN must
    // retain its payload and select rate 1 without release arithmetic.
    if (scalar::unordered_or_less_equal(0.0f, remaining_wait))
    {
        state.translation_control_end_rate = 1.0f;
        return;
    }

    state.translation_control_wait_remaining = 0.0f;
    const float release_time = state.translation_control_end_release_time;
    const float release_step = scalar::divide(delta, release_time);
    const float previous_rate = state.translation_control_end_rate;
    const float rate = scalar::subtract(previous_rate, release_step);
    state.translation_control_end_rate = rate;
    // Preserve unordered results and both signed zeros; only ordered negatives clamp.
    if (!scalar::unordered_or_less_equal(0.0f, rate))
    {
        state.translation_control_end_rate = 0.0f;
    }
}

ControlPreparation prepare_control(FollowCamera &state, Subject *subject,
                                   const SubjectCompensationState &previous)
{
    update_fulcrum_distance(state, previous);
    const Vec4 input = select_control_input(state, subject);
    // The complete vector is loaded before any reset/lock gating or state mutation.
    bool blocked = state.reset_requested != 0;
    if (!blocked && state.lock_requested != 0)
    {
        blocked = state.adjust_lock_camera == 0;
    }
    if (blocked)
    {
        state.hand_control = uint8_t{0};
        begin_translation_release_wait(state);
        return {input, false};
    }

    if (!packed::ordered_zero(input.x()) || !packed::ordered_zero(input.y()))
    {
        state.hand_control = uint8_t{1};
        scalar::clear_translation_control_pair(state);
        return {input, true};
    }
    update_translation_release(state, previous.saved_time_register.x());
    return {input, false};
}

} // namespace camera::update::control
