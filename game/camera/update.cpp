// SPDX-License-Identifier: GPL-2.0-or-later
// Advances subject-relative camera, input, wall avoidance and territory constraints.
// The hidden vector wire body preserves opaque physical time bits across the scalar entry.
#include "update.h"
#include "../engine/camera_update/math.h"
#include "../engine/camera_update/carrier_lifetime.h"
#include "../engine/camera_update/scalar_entry.h"
#include "update/parameter.h"
#include "update/free_input.h"
#include "update/angle_frame.h"
#include "update/translation_compensation.h"
#include "update/candidate_collision.h"
#include "update/territory.h"

extern "C" __attribute__((visibility("hidden"), noinline, used)) void
camera_update_follow_body(camera::FollowCamera *state, camera::Subject *subject, camera::QueryWorld *queries,
                          engine::Vec4 time_register)
{
    namespace parameter = camera::update::parameters;
    if (parameter::apply_parameter_stage(*state, subject) ==
        parameter::ParameterStageDecision::return_from_update)
        return;
    const auto previous =
        camera::update::subject::prepare_and_compensate_subject(*state, subject, time_register);
    const auto control = camera::update::control::prepare_control(*state, subject, previous);
    const auto fresh = camera::update::subject_transform::prepare_fresh_subject_transform(
        *state, subject, control.control_input);
    const auto target = camera::update::target::prepare_target_translation(*state, previous, fresh);
    namespace mode = camera::update::angular_mode;
    auto angular = mode::prepare_angular_mode(*state, target, fresh);
    if (angular.continuation == mode::AngularContinuation::locked_geometry)
    {
        angular = camera::update::locked_geometry::prepare_locked_geometry(*state, target);
    }
    else if (angular.continuation == mode::AngularContinuation::front_chase)
    {
        angular = camera::update::front_chase::chase_front_lock(*state, angular, control, previous);
    }
    namespace rotation_stage = camera::update::rotation_dispatch;
    auto rotation = rotation_stage::prepare_rotation_input(*state, angular, target, control, previous);
    if (rotation.continuation == rotation_stage::RotationContinuation::free_input)
    {
        rotation = camera::update::free_input::prepare_free_input(*state, rotation, control, previous);
    }
    const auto frame = camera::update::angle_frame::prepare_angle_frame(*state);
    const camera::update::translation_compensation::TranslationCompensationInput translation_input{
        frame.fulcrum, frame.half, frame.three, previous.subject_position, previous.camera_basis.axis_x};
    const auto translation =
        camera::update::translation_compensation::compensate_translation(*state, translation_input);
    camera::update::candidate_collision::prepare_candidate_collision(*state, queries, translation.fulcrum,
                                                                     translation.half, translation.three);
    const auto wing =
        camera::update::wing_escape::prepare_wing_escape(*state, queries, previous.saved_time_register);
    const auto chase = camera::update::chase::chase_camera_candidate(*state, wing.half, wing.three);
    const camera::update::orientation::OrientationPreparation orientation_input{chase.candidate,
                                                                                chase.cast_origin,
                                                                                chase.half,
                                                                                chase.three,
                                                                                previous.camera_basis.axis_x,
                                                                                previous.camera_basis.axis_y,
                                                                                previous.camera_basis.forward,
                                                                                previous.camera_position};
    camera::update::orientation::construct_camera_orientation(*state, orientation_input);
    camera::update::final_collision::collide_final_camera(*state, queries, chase.candidate);
    const camera::update::validation_margin::RollbackPreparation rollback{
        {previous.camera_basis.axis_x, previous.camera_basis.axis_y, previous.camera_basis.forward,
         previous.camera_position},
        {previous.subject_basis.axis_x, previous.subject_basis.axis_y, previous.subject_basis.forward,
         previous.subject_position},
        previous.target,
        previous.cast_origin,
        previous.orbit_origin,
        previous.fulcrum,
        previous.candidate_position,
        previous.chased_position};
    const auto validation = camera::update::validation_margin::validate_camera(*state, rollback);
    const auto territory = camera::update::territory::avoid_territories(*state, queries, validation);
    camera::update::validation_margin::apply_safe_margin(*state, territory);
    engine::camera_observation::retain(previous, control, fresh, target, angular, rotation, frame,
                                       translation, wing, chase, orientation_input, validation, territory,
                                       rollback);
}

extern "C" ENGINE_CAMERA_SCALAR_ENTRY_ATTRIBUTES void
bb_camera_update_follow(camera::FollowCamera *, camera::Subject *, camera::QueryWorld *, float)
{
    ENGINE_CAMERA_FORWARD_SCALAR_TIME(camera_update_follow_body);
}
