// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera control stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Continue from the compensated snapshots and stop before fresh subject-transform work.
#include "subject.h"
#include "../../engine/camera_update/reciprocal_root.h"

namespace camera::update::control
{
namespace scalar = engine::camera_control_math;
namespace packed = engine::camera_subject_math;
namespace root = engine::camera_root;
using update::subject::SubjectCompensationState;

// Narrow role-based views of the subject's control-input graph; both selected input
// addresses are 16-aligned for the original VMOVDQA. Null preferred falls back once.
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

RT_GLOBAL(0x04cf24f0, half_vector, const Vec4);
RT_GLOBAL(0x04cf2500, three_vector, const Vec4);
RT_GLOBAL(0x04d25e70, minimum_fulcrum_distance, const float);

struct ControlPreparation
{
    Vec4 control_input;     // original XMM3, retained at rbp-0x3e0 by next stage's b918
    bool hand_input_active; // only low byte of original rbp-0x3c4 is consumed later
};

void update_fulcrum_distance(FollowCamera &state, const SubjectCompensationState &previous)
{
    const float rate = scalar::capture(state.fulcrum_distance_rate);
    // Original JBE skips unordered and both signed zeros as well as negative rates.
    if (scalar::unordered_or_less_equal(rate, 0.0f)) return;

    const Vec4 difference = packed::subtract(previous.camera_position, previous.cast_origin);
    const Vec4 squares = packed::multiply(difference, difference);
    const Vec4 x = packed::broadcast<0>(squares);
    const Vec4 y = packed::broadcast<1>(squares);
    const Vec4 z = packed::broadcast<2>(squares);
    const Vec4 xy = packed::add(x, y);
    const Vec4 squared_distance = packed::add(z, xy);
    const Vec4 half = half_vector.get();
    const Vec4 three = three_vector.get();
    const Vec4 distance =
        root::refined_length(squared_distance, half, three, root::FirstProductOrder::reciprocal_then_length);
    const float scaled_distance = scalar::multiply(distance.x(), rate);
    scalar::publish(state.fulcrum_distance, scaled_distance);
    const float minimum_distance = scalar::capture(minimum_fulcrum_distance.get());
    // JBE preserves unordered scaled results. A generic maximum can mask those NaNs.
    if (!scalar::unordered_or_less_equal(minimum_distance, scaled_distance))
    {
        scalar::publish(state.fulcrum_distance, minimum_distance);
    }
}

Vec4 select_control_input(FollowCamera &state, Subject *subject)
{
    if (scalar::capture(state.subject_control_input) == 0)
    {
        return scalar::capture_input(default_control_input.get());
    }
    const auto *prefix = reinterpret_cast<const SubjectControlPrefix *>(subject);
    const SubjectControlContext *context = scalar::capture(prefix->context);
    const ControlInputOwner *owner = scalar::capture(context->preferred);
    if (owner == nullptr) owner = scalar::capture(context->fallback);
    // Subject, context and the final owner are unchecked, as in the nonnull original path.
    return scalar::capture_input(owner->input);
}

void begin_translation_release_wait(FollowCamera &state)
{
    scalar::publish(state.translation_control_end_rate, 1.0f);
    const float wait_time = scalar::capture(state.translation_control_end_wait_time);
    scalar::publish(state.translation_control_wait_remaining, wait_time);
}

void update_translation_release(FollowCamera &state, float delta)
{
    // This is the cold c579 path reached only when both control X/Y are ordered zeros.
    const uint8_t was_hand_control = scalar::capture(state.hand_control);
    scalar::publish(state.hand_control, uint8_t{0});
    if (was_hand_control != 0)
    {
        begin_translation_release_wait(state);
        return;
    }

    const float previous_wait = scalar::capture(state.translation_control_wait_remaining);
    const float remaining_wait = scalar::subtract(previous_wait, delta);
    scalar::publish(state.translation_control_wait_remaining, remaining_wait);
    // Original c5a9/JBE reaches f7ab for nonnegative OR unordered wait. NaN must
    // retain its decremented payload here and set rate1, without release arithmetic.
    if (scalar::unordered_or_less_equal(0.0f, remaining_wait))
    {
        scalar::publish(state.translation_control_end_rate, 1.0f);
        return;
    }

    scalar::publish(state.translation_control_wait_remaining, 0.0f);
    const float release_time = scalar::capture(state.translation_control_end_release_time);
    const float release_step = scalar::divide(delta, release_time);
    const float previous_rate = scalar::capture(state.translation_control_end_rate);
    const float rate = scalar::subtract(previous_rate, release_step);
    scalar::publish(state.translation_control_end_rate, rate);
    // Preserve unordered results and both signed zeros; only ordered negatives clamp.
    if (!scalar::unordered_or_less_equal(0.0f, rate))
    {
        scalar::publish(state.translation_control_end_rate, 0.0f);
    }
}

ControlPreparation prepare_control(FollowCamera &state, Subject *subject,
                                   const SubjectCompensationState &previous)
{
    update_fulcrum_distance(state, previous);
    const Vec4 input = select_control_input(state, subject);
    // The complete vector is loaded before any reset/lock gating or state mutation.
    bool blocked = scalar::capture(state.reset_requested) != 0;
    if (!blocked && scalar::capture(state.lock_requested) != 0)
    {
        blocked = scalar::capture(state.adjust_lock_camera) == 0;
    }
    if (blocked)
    {
        scalar::publish(state.hand_control, uint8_t{0});
        begin_translation_release_wait(state);
        return {input, false};
    }

    if (!packed::is_ordered_zero(input.x()) || !packed::is_ordered_zero(input.y()))
    {
        scalar::publish(state.hand_control, uint8_t{1});
        scalar::clear_translation_control_pair(state);
        return {input, true};
    }
    update_translation_release(state, previous.saved_time_register.x());
    return {input, false};
}

} // namespace camera::update::control
