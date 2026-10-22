// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera subject stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Prepare angular-pivot compensation, apply it to prior camera/subject state, add translation,
// Fulcrum-distance calculation and later control/fresh-subject-transform stages are excluded.
#include "math_calls.h"

namespace camera::update::subject
{
namespace packed = engine::camera_subject_math;
namespace math = camera::math_calls;

// Only the early chain is modeled here. It is deliberately not conflated with the later
// subject+0x3b0 -> +0x68 path, even if two real objects happen to reach the same position.
struct EarlyPivotPosition
{
    uint8_t unknown_0x000[0x1e0];
    Vec4 position;
};
struct EarlyPivotLink
{
    uint8_t unknown_0x00[0x68];
    EarlyPivotPosition *position;
};
struct EarlyPivotOwner
{
    uint8_t unknown_0x000[0x3b0];
    EarlyPivotLink *link;
};
struct EarlySubjectContext
{
    uint8_t unknown_0x00[0x08];
    EarlyPivotOwner *pivot_owner;
};
struct EarlySubjectPrefix
{
    uint8_t unknown_0x00[0x58];
    EarlySubjectContext *context;
};
static_assert(offsetof(EarlyPivotPosition, position) == 0x1e0);
static_assert(offsetof(EarlyPivotLink, position) == 0x68);
static_assert(offsetof(EarlyPivotOwner, link) == 0x3b0);
static_assert(offsetof(EarlySubjectContext, pivot_owner) == 0x08);
static_assert(offsetof(EarlySubjectPrefix, context) == 0x58);

RT_GLOBAL(0x04d25e6c, scalar_one, const float);
RT_GLOBAL(0x04cf24a0, unit_y, const Vec4);
RT_GLOBAL(0x04cf24b0, unit_z, const Vec4);
RT_GLOBAL(0x04cf24c0, unit_w, const Vec4);
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf24e0, negate_x_scale, const Vec4);

struct CompensationPreparation
{
    // This is an internal carrier of the original saved register, not a claim that the
    // update's scalar float parameter has a Vec4 source prototype. Upper bits are retained
    // until later data-flow proves which parts of the stack snapshot are actually consumed.
    Vec4 saved_time_register;     // original rbp-0x250
    Vec4 broadcast_delta;         // original rbp-0x3f0
    Vec4 translation_delta;       // original rbp-0x270
    Matrix4 angular_compensation; // original XMM11/XMM12/XMM13/XMM3 at0x0183b2c1
};

// Original0x0183b1bf..0x0183b297 applies the positive-pivot translation with an XZ/YW tree.
// Product order is broadcast-first for XYZ, position-first for W. Do not use a generic matrix
// operator: its association and NaN operand selection may differ even for identity lanes.
Vec4 apply_positive_pivot(Vec4 coefficients, Vec4 axis_x, Vec4 axis_y, Vec4 axis_z, Vec4 pivot)
{
    const Vec4 x = packed::multiply(packed::broadcast<0>(coefficients), axis_x);
    const Vec4 y = packed::multiply(packed::broadcast<1>(coefficients), axis_y);
    const Vec4 z = packed::multiply(packed::broadcast<2>(coefficients), axis_z);
    const Vec4 w = packed::multiply(pivot, packed::broadcast<3>(coefficients));
    const Vec4 xz = packed::add(x, z);
    const Vec4 yw = packed::add(y, w);
    return packed::add(xz, yw);
}

CompensationPreparation prepare_angular_pivot(FollowCamera &state, Subject *subject, Vec4 time_register)
{
    // Entry is the already-null-checked nonnull-subject continuation from the parameter stage.
    const Vec4 delta = packed::broadcast<0>(time_register);
    const Vec4 translation_delta = packed::multiply(delta, state.translation_velocity);
    const float angular_delta = packed::multiply_low(time_register.x(), state.angular_velocity);
    const Vec4 initial_x = packed::scalar_vector(scalar_one.get());
    if (packed::is_ordered_zero(angular_delta))
    {
        // Both signed zeros skip the chain and both trig calls; NaN takes the other branch.
        const Matrix4 identity{initial_x, unit_y.get(), unit_z.get(), unit_w.get()};
        return {time_register, delta, translation_delta, identity};
    }

    const auto *prefix = reinterpret_cast<const EarlySubjectPrefix *>(subject);
    const Vec4 pivot = prefix->context->pivot_owner->link->position->position;
    const Vec4 zero = Vec4::zero();
    // Arithmetic 0-position is intentional; a bitwise sign flip is observably different.
    const Vec4 negative_pivot =
        packed::bit_or(packed::bit_and(packed::subtract(zero, pivot), xyz_bits.get()), unit_w.get());

    const float sine = math::sine_cosine_select(angular_delta, math::sine_selector, 0);
    const float cosine = math::sine_cosine_select(angular_delta, math::cosine_selector, 0);
    const Vec4 rotated_z = packed::sine_cosine_vector(sine, cosine);
    const Vec4 rotated_x = packed::swap_x_z(packed::multiply(rotated_z, negate_x_scale.get()));

    // Keep the zero products/additions, including the W lane, for nonfinite inputs.
    const Vec4 z_times_zero = packed::multiply(rotated_z, zero);
    const Vec4 x_basis = packed::add(packed::add(z_times_zero, rotated_x), zero);
    const Vec4 x_times_zero = packed::multiply(rotated_x, zero);
    const Vec4 y_partial = packed::add(z_times_zero, x_times_zero);
    const Vec4 axis_y = unit_y.get();
    const Vec4 y_basis = packed::add(y_partial, axis_y);
    const Vec4 z_basis = packed::add(packed::add(rotated_z, x_times_zero), zero);

    const Vec4 pivot_x = packed::multiply(packed::broadcast<0>(negative_pivot), rotated_x);
    const Vec4 pivot_y = packed::multiply(packed::broadcast<1>(negative_pivot), axis_y);
    const Vec4 pivot_z = packed::multiply(rotated_z, packed::broadcast<2>(negative_pivot));
    const Vec4 pivot_w = packed::multiply(packed::broadcast<3>(negative_pivot), unit_w.get());
    const Vec4 negative_xz = packed::add(pivot_z, pivot_x);
    const Vec4 negative_yw = packed::add(pivot_y, pivot_w);
    const Vec4 translated_origin = packed::add(negative_yw, negative_xz);

    const Vec4 positive_pivot = packed::bit_or(packed::bit_and(pivot, xyz_bits.get()), unit_w.get());
    const Vec4 axis_x = packed::scalar_vector(scalar_one.get());
    const Vec4 axis_z = unit_z.get();
    Matrix4 compensation;
    compensation.axis_x = apply_positive_pivot(x_basis, axis_x, axis_y, axis_z, positive_pivot);
    compensation.axis_y = apply_positive_pivot(y_basis, axis_x, axis_y, axis_z, positive_pivot);
    compensation.forward = apply_positive_pivot(z_basis, axis_x, axis_y, axis_z, positive_pivot);
    compensation.position = apply_positive_pivot(translated_origin, axis_x, axis_y, axis_z, positive_pivot);
    return {time_register, delta, translation_delta, compensation};
}

// Compensation application uses the frame vector first in every multiplication. This is
// deliberately different from apply_positive_pivot, whose first three products are reversed.
Vec4 compensate_transform_vector(const Matrix4 &frame, Vec4 coefficients)
{
    const Vec4 x = packed::multiply(frame.axis_x, packed::broadcast<0>(coefficients));
    const Vec4 y = packed::multiply(frame.axis_y, packed::broadcast<1>(coefficients));
    const Vec4 z = packed::multiply(frame.forward, packed::broadcast<2>(coefficients));
    const Vec4 w = packed::multiply(frame.position, packed::broadcast<3>(coefficients));
    const Vec4 xz = packed::add(x, z);
    const Vec4 yw = packed::add(y, w);
    return packed::add(xz, yw);
}

Vec4 compensate_point_vector(const Matrix4 &frame, Vec4 coefficients)
{
    const Vec4 x = packed::multiply(frame.axis_x, packed::broadcast<0>(coefficients));
    const Vec4 y = packed::multiply(frame.axis_y, packed::broadcast<1>(coefficients));
    const Vec4 z = packed::multiply(frame.forward, packed::broadcast<2>(coefficients));
    const Vec4 w = packed::multiply(frame.position, packed::broadcast<3>(coefficients));
    const Vec4 xy = packed::add(x, y);
    const Vec4 zw = packed::add(z, w);
    return packed::add(xy, zw);
}

Vec4 compensate_candidate_vector(const Matrix4 &frame, Vec4 coefficients)
{
    // Original b5dc computes X+Y before the Z/W products. Retain that sequencing too.
    const Vec4 x = packed::multiply(frame.axis_x, packed::broadcast<0>(coefficients));
    const Vec4 y = packed::multiply(frame.axis_y, packed::broadcast<1>(coefficients));
    const Vec4 xy = packed::add(x, y);
    const Vec4 z = packed::multiply(frame.forward, packed::broadcast<2>(coefficients));
    const Vec4 w = packed::multiply(frame.position, packed::broadcast<3>(coefficients));
    const Vec4 zw = packed::add(z, w);
    return packed::add(xy, zw);
}

struct BasisSnapshot
{
    Vec4 axis_x;
    Vec4 axis_y;
    Vec4 forward;
};

struct SubjectCompensationState
{
    Vec4 saved_time_register;    // rbp-0x250
    Vec4 broadcast_delta;        // rbp-0x3f0
    Vec4 camera_position;        // rbp-0x280
    Vec4 subject_position;       // rbp-0x260
    Vec4 target;                 // rbp-0x310
    Vec4 cast_origin;            // rbp-0x2a0
    Vec4 orbit_origin;           // rbp-0x320
    Vec4 fulcrum;                // rbp-0x330
    Vec4 candidate_position;     // rbp-0x340
    Vec4 chased_position;        // rbp-0x350
    BasisSnapshot camera_basis;  // rbp-0x290 / -0x2c0 / -0x2d0
    BasisSnapshot subject_basis; // rbp-0xa0 / -0x90 / -0x80
};

SubjectCompensationState apply_compensation_and_capture(FollowCamera &state,
                                                        const CompensationPreparation &prepared)
{
    const Matrix4 &frame = prepared.angular_compensation;

    // All four camera inputs precede all four computations and publications.
    const Vec4 old_camera_x = packed::capture(state.transform.axis_x);
    const Vec4 old_camera_y = packed::capture(state.transform.axis_y);
    const Vec4 old_camera_forward = packed::capture(state.transform.forward);
    const Vec4 old_camera_position = packed::capture(state.transform.position);
    const Vec4 camera_x = compensate_transform_vector(frame, old_camera_x);
    const Vec4 camera_y = compensate_transform_vector(frame, old_camera_y);
    const Vec4 camera_forward = compensate_transform_vector(frame, old_camera_forward);
    const Vec4 camera_position = compensate_transform_vector(frame, old_camera_position);
    packed::publish(state.transform.axis_x, camera_x);
    packed::publish(state.transform.axis_y, camera_y);
    packed::publish(state.transform.forward, camera_forward);
    packed::publish(state.transform.position, camera_position);

    const Vec4 old_subject_x = packed::capture(state.subject_transform.axis_x);
    const Vec4 old_subject_y = packed::capture(state.subject_transform.axis_y);
    const Vec4 old_subject_forward = packed::capture(state.subject_transform.forward);
    const Vec4 old_subject_position = packed::capture(state.subject_transform.position);
    const Vec4 subject_x = compensate_transform_vector(frame, old_subject_x);
    const Vec4 subject_y = compensate_transform_vector(frame, old_subject_y);
    const Vec4 subject_forward = compensate_transform_vector(frame, old_subject_forward);
    const Vec4 subject_position = compensate_transform_vector(frame, old_subject_position);
    packed::publish(state.subject_transform.axis_x, subject_x);
    packed::publish(state.subject_transform.axis_y, subject_y);
    packed::publish(state.subject_transform.forward, subject_forward);
    // Unlike the camera position, this fourth subject vector is not published yet.

    const Vec4 old_reference = packed::capture(state.target_reference);
    const Vec4 old_target = packed::capture(state.target);
    const Vec4 old_cast = packed::capture(state.cast_origin);
    const Vec4 old_orbit = packed::capture(state.orbit_origin);
    const Vec4 reference = compensate_point_vector(frame, old_reference);
    const Vec4 target = compensate_point_vector(frame, old_target);
    const Vec4 cast = compensate_point_vector(frame, old_cast);
    const Vec4 orbit = compensate_point_vector(frame, old_orbit);
    const Vec4 fulcrum = compensate_point_vector(frame, packed::capture(state.fulcrum));
    const Vec4 candidate = compensate_candidate_vector(frame, packed::capture(state.candidate_position));
    const Vec4 chased = compensate_point_vector(frame, packed::capture(state.chased_position));

    SubjectCompensationState result;
    result.saved_time_register = prepared.saved_time_register;
    result.broadcast_delta = prepared.broadcast_delta;
    const Vec4 delta = prepared.translation_delta;
    // Original b63b rereads the already compensated camera.position from the guest field.
    // Delta is the left VADDPS source for this and every remaining translation publication.
    result.camera_position = packed::add(delta, packed::capture(state.transform.position));
    packed::publish(state.transform.position, result.camera_position);
    result.subject_position = packed::add(delta, subject_position);
    packed::publish(state.subject_transform.position, result.subject_position);
    packed::publish(state.target_reference, packed::add(delta, reference));
    result.target = packed::add(delta, target);
    packed::publish(state.target, result.target);
    result.cast_origin = packed::add(delta, cast);
    packed::publish(state.cast_origin, result.cast_origin);
    result.orbit_origin = packed::add(delta, orbit);
    packed::publish(state.orbit_origin, result.orbit_origin);
    result.fulcrum = packed::add(delta, fulcrum);
    packed::publish(state.fulcrum, result.fulcrum);
    result.candidate_position = packed::add(delta, candidate);
    packed::publish(state.candidate_position, result.candidate_position);
    result.chased_position = packed::add(delta, chased);
    packed::publish(state.chased_position, result.chased_position);

    // These rollback bases are captured after compensation and translation publication,
    // before fulcrum-distance work or the new subject transform overwrites any of them.
    result.camera_basis.axis_x = packed::capture(state.transform.axis_x);
    result.camera_basis.axis_y = packed::capture(state.transform.axis_y);
    result.camera_basis.forward = packed::capture(state.transform.forward);
    result.subject_basis.axis_x = packed::capture(state.subject_transform.axis_x);
    result.subject_basis.axis_y = packed::capture(state.subject_transform.axis_y);
    result.subject_basis.forward = packed::capture(state.subject_transform.forward);
    return result;
}

SubjectCompensationState prepare_and_compensate_subject(FollowCamera &state, Subject *subject,
                                                        Vec4 time_register)
{
    const CompensationPreparation prepared = prepare_angular_pivot(state, subject, time_register);
    return apply_compensation_and_capture(state, prepared);
}

} // namespace camera::update::subject
