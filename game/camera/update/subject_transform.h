// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera subject transform stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Capture the late subject transform, call the original quaternion/matrix helpers, normalize
// the generated rows and publish the fresh subject frame. Reset/chase work is not included.
#include "math_calls.h"

namespace camera::update::subject_transform
{
namespace packed = engine::camera_subject_transform_math;
namespace math = camera::math_calls;

// This late direct subject chain is independent from the early angular-pivot chain through
// subject+0x58/context+0x08, even if both happen to reach the same runtime transform.
struct LateSubjectTransform
{
    uint8_t unknown_0x000[0x1d0];
    Vec4 rotation;
    Vec4 position;
    uint8_t unknown_0x1f0[0x136];
    uint8_t yaw_only;
};
struct LateTransformLink
{
    uint8_t unknown_0x00[0x68];
    LateSubjectTransform *transform;
};
struct LateSubjectPrefix
{
    uint8_t unknown_0x000[0x3b0];
    LateTransformLink *transform_link;
};
static_assert(offsetof(LateSubjectTransform, rotation) == 0x1d0);
static_assert(offsetof(LateSubjectTransform, position) == 0x1e0);
static_assert(offsetof(LateSubjectTransform, yaw_only) == 0x326);
static_assert(offsetof(LateTransformLink, transform) == 0x68);
static_assert(offsetof(LateSubjectPrefix, transform_link) == 0x3b0);

RT_GLOBAL(0x0597be80, subject_yaw_axis, const Vec4);
RT_GLOBAL(0x04cf24d0, position_xyz_bits, const Vec4);
RT_GLOBAL(0x04cf24c0, position_unit_w, const Vec4);
RT_GLOBAL(0x02fd5280, transform_scale, const Vec4);
RT_GLOBAL(0x04cf2510, output_w_normalization, const Vec4);

struct FreshSubjectTransformState
{
    Vec4 control_input;      // complete original XMM3 saved to rbp-0x3e0 at stage entry
    Matrix4 fresh_transform; // live XMM1/XMM8/XMM5/XMM0 at continuation; not later chased state
};

// The original packs three columns from the helper output's first three scalar columns,
// then rearranges them back into rows with W0/0/outputZ/outputW. The pinned outputZ is +0,
// but retain that actual lane source too. Keep the exact resulting row
// lanes while avoiding a source-level register/insert transcription. Arithmetic 0-row is
// essential: bitwise sign XOR differs for signed zero and NaN quieting/payloads.
Matrix4 normalize_generated_transform(const Matrix4 &generated)
{
    const Vec4 zero = Vec4::zero();
    const Vec4 x = packed::xyz_with_w(generated.axis_x, zero);
    const Vec4 y = packed::xyz_with_w(generated.axis_y, zero);
    const Vec4 normalization = output_w_normalization.get();
    const Vec4 forward = packed::xyz_with_source_z_in_w(generated.forward, normalization);
    const Vec4 position = packed::xyz_with_w(generated.position, normalization);
    const Vec4 negative_x = packed::subtract(zero, x);
    const Vec4 negative_forward = packed::subtract(zero, forward);
    return {negative_x, y, negative_forward, position};
}

FreshSubjectTransformState prepare_fresh_subject_transform(FollowCamera &state, Subject *subject,
                                                           Vec4 control_input)
{
    FreshSubjectTransformState result;
    result.control_input = control_input;

    auto *prefix = reinterpret_cast<LateSubjectPrefix *>(subject);
    LateTransformLink *link = packed::capture_pointer(prefix->transform_link);
    LateSubjectTransform *transform = packed::capture_pointer(link->transform);
    const uint8_t yaw_only = packed::capture_byte(transform->yaw_only);
    const Vec4 rotation = packed::capture(transform->rotation);

    Vec4 quaternion;
    // Preserve the original output-slot reuse on the full-rotation path: its 16-byte
    // quaternion slot becomes the first vector of the later64-byte matrix output.
    Matrix4 generated;
    if (yaw_only != 0)
    {
        math::axis_angle_quaternion(&quaternion, subject_yaw_axis.address(), rotation.y());
    }
    else
    {
        (void)math::subject_rotation_quaternion(&generated.axis_x, rotation);
        quaternion = packed::capture(generated.axis_x);
    }

    // Calls may alter the captured transform's position or replace its pointer chain.
    // Original b974 reads from the already-captured transform only AFTER the helper returns.
    const Vec4 position = packed::capture(transform->position);
    const Vec4 normalized_position =
        packed::add(packed::bit_and(position, position_xyz_bits.get()), position_unit_w.get());
    const math::TransformInput48 source{
        normalized_position,
        quaternion,
        transform_scale.get(),
    };
    math::transform_matrix(&source, &generated);

    result.fresh_transform = normalize_generated_transform(generated);
    packed::publish(state.subject_transform.axis_x, result.fresh_transform.axis_x);
    packed::publish(state.subject_transform.axis_y, result.fresh_transform.axis_y);
    packed::publish(state.subject_transform.forward, result.fresh_transform.forward);
    packed::publish(state.subject_transform.position, result.fresh_transform.position);
    return result;
}

} // namespace camera::update::subject_transform
