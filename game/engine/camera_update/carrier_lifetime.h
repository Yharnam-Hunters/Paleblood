// SPDX-License-Identifier: GPL-2.0-or-later
// Keep named carried values alive without observing aggregate padding or guest storage.
#pragma once
namespace engine::camera_observation
{
template <typename Previous, typename Control, typename Fresh, typename Target, typename Angular,
          typename Rotation, typename Frame, typename Translation, typename Wing, typename Chase,
          typename Orientation, typename Validation, typename Territory, typename Rollback>
inline void retain(const Previous &previous, const Control &control, const Fresh &fresh, const Target &target,
                   const Angular &angular, const Rotation &rotation, const Frame &frame,
                   const Translation &translation, const Wing &wing, const Chase &chase,
                   const Orientation &orientation_input, const Validation &validation,
                   const Territory &territory, const Rollback &rollback)
{
    // Observe initialized named carriers only; never aggregate padding or extra guest memory.
    asm volatile(""
                 :
                 : "m"(previous.saved_time_register.v), "m"(previous.broadcast_delta.v),
                   "m"(previous.camera_position.v), "m"(previous.subject_position.v), "m"(previous.target.v),
                   "m"(previous.cast_origin.v), "m"(previous.orbit_origin.v), "m"(previous.fulcrum.v),
                   "m"(previous.candidate_position.v), "m"(previous.chased_position.v),
                   "m"(previous.camera_basis.axis_x.v), "m"(previous.camera_basis.axis_y.v),
                   "m"(previous.camera_basis.forward.v), "m"(previous.subject_basis.axis_x.v),
                   "m"(previous.subject_basis.axis_y.v), "m"(previous.subject_basis.forward.v),
                   "m"(control.hand_input_active), "m"(fresh.control_input.v),
                   "m"(fresh.fresh_transform.axis_x.v), "m"(fresh.fresh_transform.axis_y.v),
                   "m"(fresh.fresh_transform.forward.v), "m"(fresh.fresh_transform.position.v),
                   "m"(target.reset_requested), "m"(target.subject_position.v), "m"(target.fulcrum.v),
                   "m"(target.candidate_position.v)
                 : "memory");
    // GCC permits at most30 operands per asm; retain the remaining named carriers here.
    asm volatile(""
                 :
                 : "m"(angular.front_active), "m"(rotation.captured_lock), "m"(frame.fulcrum.v),
                   "m"(frame.half.v), "m"(frame.three.v), "m"(translation.fulcrum.v), "m"(translation.half.v),
                   "m"(translation.three.v), "m"(wing.half.v), "m"(wing.three.v)
                 : "memory");
    asm volatile(""
                 :
                 : "m"(chase.candidate.v), "m"(chase.cast_origin.v), "m"(chase.half.v), "m"(chase.three.v),
                   "m"(orientation_input.candidate.v), "m"(orientation_input.cast_origin.v),
                   "m"(orientation_input.half.v), "m"(orientation_input.three.v),
                   "m"(orientation_input.saved_axis_x.v), "m"(orientation_input.saved_axis_y.v),
                   "m"(orientation_input.saved_forward.v), "m"(orientation_input.saved_position.v),
                   "m"(validation.position.v), "m"(validation.half.v), "m"(validation.three.v),
                   "m"(territory.position.v), "m"(territory.half.v), "m"(territory.three.v)
                 : "memory");
    asm volatile(""
                 :
                 : "m"(rollback.camera_transform.axis_x.v), "m"(rollback.camera_transform.axis_y.v),
                   "m"(rollback.camera_transform.forward.v), "m"(rollback.camera_transform.position.v),
                   "m"(rollback.subject_transform.axis_x.v), "m"(rollback.subject_transform.axis_y.v),
                   "m"(rollback.subject_transform.forward.v), "m"(rollback.subject_transform.position.v),
                   "m"(rollback.target.v), "m"(rollback.cast_origin.v), "m"(rollback.orbit_origin.v),
                   "m"(rollback.fulcrum.v), "m"(rollback.candidate.v), "m"(rollback.chased.v)
                 : "memory");
}
}
