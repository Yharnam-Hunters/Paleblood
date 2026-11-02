// SPDX-License-Identifier: GPL-2.0-or-later
// Partial views used by ChrFollowCam's update and pitch/yaw getters. Field widths come from
// instruction accesses; names also use the original debug controls. These are not complete
// guest classes: unknown bytes and the camera's full allocation size remain unspecified.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/vector.h"
#include "runtime/original.h"

namespace camera {

using engine::Vec4;

struct Subject;       // followed object; its two transform-access chains remain distinct
struct QueryWorld;    // passed to the original sphere query

struct PitchYaw {
    float pitch;
    float yaw;
};
static_assert(offsetof(PitchYaw, pitch) == 0x0 && offsetof(PitchYaw, yaw) == 0x4);
static_assert(sizeof(PitchYaw) == 0x8);

// Four vectors copied by the camera's caller. The forward vector is also read by both getters.
struct Matrix4 {
    Vec4 axis_x;
    Vec4 axis_y;
    Vec4 forward;
    Vec4 position;
};
static_assert(offsetof(Matrix4, axis_x) == 0x0 && offsetof(Matrix4, axis_y) == 0x10);
static_assert(offsetof(Matrix4, forward) == 0x20 && offsetof(Matrix4, position) == 0x30);
static_assert(sizeof(Matrix4) == 0x40);

struct FollowCamera {
    uint8_t unknown_0x000[0x10];
    Matrix4 transform;
    float vertical_fov;
    uint8_t unknown_0x054[0xc];
    Matrix4 subject_transform;
    // Point roles below name their use in this update, rather than original class members.
    Vec4 target_reference;
    Vec4 target;
    Vec4 cast_origin;
    Vec4 orbit_origin;
    Vec4 fulcrum;
    Vec4 candidate_position;
    Vec4 chased_position;
    Vec4 translation_velocity;
    uint8_t unknown_0x120[0x4];
    float angular_velocity;
    uint8_t unknown_0x128[0x8];
    float rotation_control_rate;
    float rotation_control_wait_remaining;
    float high_speed_rotation_rate;
    uint8_t reset_requested;
    uint8_t reset_angles_requested;
    uint8_t unknown_0x13e[0x2];
    PitchYaw angles;
    PitchYaw reset_angles;
    float target_pitch;
    float translation_control_end_rate;
    float translation_control_wait_remaining;
    uint8_t hand_control;
    uint8_t unknown_0x15d[0x3];
    Vec4 subject_origin_offset;
    float fulcrum_lateral_move_max_distance;
    float fulcrum_lateral_move_min_rate;
    float fulcrum_lateral_move_max_rate;
    float fulcrum_distance_rate;
    float fulcrum_distance;
    float camera_distance;
    float target_camera_distance;
    float cast_sphere_radius;
    float safe_margin_rate;
    float safe_margin_max;
    float subject_translation_chase_rate;
    float normal_subject_translation_chase_rate;
    float escape_subject_translation_chase_rate;
    float target_translation_chase_rate;
    float target_pitch_chase_rate;
    float target_chase_rate_xz;
    float normal_target_chase_rate_xz;
    float escape_target_chase_rate_xz;
    float target_chase_rate_y;
    float target_chase_min_angle;
    float target_chase_max_angle;
    float target_chase_distance_rate;
    float lock_target_chase_rate_xz;
    float lock_target_chase_rate_y;
    float lock_target_chase_min_angle;
    float lock_target_chase_max_angle;
    float translation_control_end_wait_time;
    float translation_control_end_release_time;
    float rotation_control_end_wait_time;
    float rotation_control_end_release_time;
    float high_speed_rotation_ramp_time;
    float maximum_pitch;
    float minimum_pitch;
    float maximum_lock_pitch;
    float minimum_lock_pitch;
    float rotation_range_height_begin;
    float rotation_range_height_end;
    PitchYaw rotation_speed;
    PitchYaw minimum_rotation_speed;
    PitchYaw maximum_rotation_speed;
    PitchYaw high_rotation_speed;
    PitchYaw minimum_high_rotation_speed;
    PitchYaw maximum_high_rotation_speed;
    float option_rotation_speed_rate;
    float direct_pitch_angle;
    float direct_pitch_rate;
    float lock_rotation_chase_rate;
    float lock_rotation_dead_angle;
    float lock_pitch_shift_ratio;
    uint8_t unknown_0x24c[0x4];
    Vec4 lock_target;
    uint8_t lock_requested;
    uint8_t boss_lock_requested;
    uint8_t front_lock_requested;
    uint8_t front_lock_active;
    float front_lock_remaining;
    float front_lock_duration;
    PitchYaw front_lock_angles;
    uint8_t update_target_pitch_at_lock;
    uint8_t update_target_pitch_at_front_lock;
    uint8_t adjust_lock_camera;
    uint8_t unknown_0x277[0x1];
    PitchYaw lock_adjustment;
    PitchYaw maximum_lock_adjustment;
    float lock_adjustment_chase_rate;
    uint8_t escape_wall_mode;
    uint8_t unknown_0x28d[0x7];
    float escape_wall_yaw;
    uint8_t unknown_0x298[0x4];
    float escape_wall_yaw_increment_rate;
    uint8_t unknown_0x2a0[0x4];
    float weak_escape_wall_yaw_increment_rate;
    float weak_escape_wall_begin_fraction;
    uint8_t unknown_0x2ac[0x4];
    float escape_wall_yaw_speed_limit;
    float escape_wall_damping;
    float escape_wall_wing_yaw;
    float escape_wall_wing_length;
    float escape_wall_wing_radius;
    float escape_wall_wing_separation;
    float positive_wing_hit_fraction;
    float negative_wing_hit_fraction;
    uint8_t avoid_territories;
    uint8_t draw_territories;
    uint8_t unknown_0x2d2[0xe];
    Matrix4 territory_transform;
    int32_t parameter_override_id;
    int32_t event_parameter_id;
    int32_t boss_lock_parameter_id;
    int32_t map_parameter_id;
    float parameter_chase_rate;
    uint8_t subject_control_input;
    uint8_t unknown_0x335[0xf];
    uint8_t fov_target_height_adjustment;
};
static_assert(offsetof(FollowCamera, transform) == 0x10);
static_assert(offsetof(FollowCamera, vertical_fov) == 0x50);
static_assert(offsetof(FollowCamera, subject_transform) == 0x60);
static_assert(offsetof(FollowCamera, target_reference) == 0xa0);
static_assert(offsetof(FollowCamera, target) == 0xb0);
static_assert(offsetof(FollowCamera, cast_origin) == 0xc0);
static_assert(offsetof(FollowCamera, orbit_origin) == 0xd0);
static_assert(offsetof(FollowCamera, fulcrum) == 0xe0);
static_assert(offsetof(FollowCamera, candidate_position) == 0xf0);
static_assert(offsetof(FollowCamera, chased_position) == 0x100);
static_assert(offsetof(FollowCamera, translation_velocity) == 0x110);
static_assert(offsetof(FollowCamera, angular_velocity) == 0x124);
static_assert(offsetof(FollowCamera, rotation_control_rate) == 0x130);
static_assert(offsetof(FollowCamera, rotation_control_wait_remaining) == 0x134);
static_assert(offsetof(FollowCamera, high_speed_rotation_rate) == 0x138);
static_assert(offsetof(FollowCamera, reset_requested) == 0x13c);
static_assert(offsetof(FollowCamera, reset_angles_requested) == 0x13d);
static_assert(offsetof(FollowCamera, angles) == 0x140);
static_assert(offsetof(FollowCamera, reset_angles) == 0x148);
static_assert(offsetof(FollowCamera, target_pitch) == 0x150);
static_assert(offsetof(FollowCamera, translation_control_end_rate) == 0x154);
static_assert(offsetof(FollowCamera, translation_control_wait_remaining) == 0x158);
static_assert(offsetof(FollowCamera, hand_control) == 0x15c);
static_assert(offsetof(FollowCamera, subject_origin_offset) == 0x160);
static_assert(offsetof(FollowCamera, fulcrum_lateral_move_max_distance) == 0x170);
static_assert(offsetof(FollowCamera, fulcrum_lateral_move_min_rate) == 0x174);
static_assert(offsetof(FollowCamera, fulcrum_lateral_move_max_rate) == 0x178);
static_assert(offsetof(FollowCamera, fulcrum_distance_rate) == 0x17c);
static_assert(offsetof(FollowCamera, fulcrum_distance) == 0x180);
static_assert(offsetof(FollowCamera, camera_distance) == 0x184);
static_assert(offsetof(FollowCamera, target_camera_distance) == 0x188);
static_assert(offsetof(FollowCamera, cast_sphere_radius) == 0x18c);
static_assert(offsetof(FollowCamera, safe_margin_rate) == 0x190);
static_assert(offsetof(FollowCamera, safe_margin_max) == 0x194);
static_assert(offsetof(FollowCamera, subject_translation_chase_rate) == 0x198);
static_assert(offsetof(FollowCamera, normal_subject_translation_chase_rate) == 0x19c);
static_assert(offsetof(FollowCamera, escape_subject_translation_chase_rate) == 0x1a0);
static_assert(offsetof(FollowCamera, target_translation_chase_rate) == 0x1a4);
static_assert(offsetof(FollowCamera, target_pitch_chase_rate) == 0x1a8);
static_assert(offsetof(FollowCamera, target_chase_rate_xz) == 0x1ac);
static_assert(offsetof(FollowCamera, normal_target_chase_rate_xz) == 0x1b0);
static_assert(offsetof(FollowCamera, escape_target_chase_rate_xz) == 0x1b4);
static_assert(offsetof(FollowCamera, target_chase_rate_y) == 0x1b8);
static_assert(offsetof(FollowCamera, target_chase_min_angle) == 0x1bc);
static_assert(offsetof(FollowCamera, target_chase_max_angle) == 0x1c0);
static_assert(offsetof(FollowCamera, target_chase_distance_rate) == 0x1c4);
static_assert(offsetof(FollowCamera, lock_target_chase_rate_xz) == 0x1c8);
static_assert(offsetof(FollowCamera, lock_target_chase_rate_y) == 0x1cc);
static_assert(offsetof(FollowCamera, lock_target_chase_min_angle) == 0x1d0);
static_assert(offsetof(FollowCamera, lock_target_chase_max_angle) == 0x1d4);
static_assert(offsetof(FollowCamera, translation_control_end_wait_time) == 0x1d8);
static_assert(offsetof(FollowCamera, translation_control_end_release_time) == 0x1dc);
static_assert(offsetof(FollowCamera, rotation_control_end_wait_time) == 0x1e0);
static_assert(offsetof(FollowCamera, rotation_control_end_release_time) == 0x1e4);
static_assert(offsetof(FollowCamera, high_speed_rotation_ramp_time) == 0x1e8);
static_assert(offsetof(FollowCamera, maximum_pitch) == 0x1ec);
static_assert(offsetof(FollowCamera, minimum_pitch) == 0x1f0);
static_assert(offsetof(FollowCamera, maximum_lock_pitch) == 0x1f4);
static_assert(offsetof(FollowCamera, minimum_lock_pitch) == 0x1f8);
static_assert(offsetof(FollowCamera, rotation_range_height_begin) == 0x1fc);
static_assert(offsetof(FollowCamera, rotation_range_height_end) == 0x200);
static_assert(offsetof(FollowCamera, rotation_speed) == 0x204);
static_assert(offsetof(FollowCamera, minimum_rotation_speed) == 0x20c);
static_assert(offsetof(FollowCamera, maximum_rotation_speed) == 0x214);
static_assert(offsetof(FollowCamera, high_rotation_speed) == 0x21c);
static_assert(offsetof(FollowCamera, minimum_high_rotation_speed) == 0x224);
static_assert(offsetof(FollowCamera, maximum_high_rotation_speed) == 0x22c);
static_assert(offsetof(FollowCamera, option_rotation_speed_rate) == 0x234);
static_assert(offsetof(FollowCamera, direct_pitch_angle) == 0x238);
static_assert(offsetof(FollowCamera, direct_pitch_rate) == 0x23c);
static_assert(offsetof(FollowCamera, lock_rotation_chase_rate) == 0x240);
static_assert(offsetof(FollowCamera, lock_rotation_dead_angle) == 0x244);
static_assert(offsetof(FollowCamera, lock_pitch_shift_ratio) == 0x248);
static_assert(offsetof(FollowCamera, lock_target) == 0x250);
static_assert(offsetof(FollowCamera, lock_requested) == 0x260);
static_assert(offsetof(FollowCamera, boss_lock_requested) == 0x261);
static_assert(offsetof(FollowCamera, front_lock_requested) == 0x262);
static_assert(offsetof(FollowCamera, front_lock_active) == 0x263);
static_assert(offsetof(FollowCamera, front_lock_remaining) == 0x264);
static_assert(offsetof(FollowCamera, front_lock_duration) == 0x268);
static_assert(offsetof(FollowCamera, front_lock_angles) == 0x26c);
static_assert(offsetof(FollowCamera, update_target_pitch_at_lock) == 0x274);
static_assert(offsetof(FollowCamera, update_target_pitch_at_front_lock) == 0x275);
static_assert(offsetof(FollowCamera, adjust_lock_camera) == 0x276);
static_assert(offsetof(FollowCamera, lock_adjustment) == 0x278);
static_assert(offsetof(FollowCamera, maximum_lock_adjustment) == 0x280);
static_assert(offsetof(FollowCamera, lock_adjustment_chase_rate) == 0x288);
static_assert(offsetof(FollowCamera, escape_wall_mode) == 0x28c);
static_assert(offsetof(FollowCamera, escape_wall_yaw) == 0x294);
static_assert(offsetof(FollowCamera, escape_wall_yaw_increment_rate) == 0x29c);
static_assert(offsetof(FollowCamera, weak_escape_wall_yaw_increment_rate) == 0x2a4);
static_assert(offsetof(FollowCamera, weak_escape_wall_begin_fraction) == 0x2a8);
static_assert(offsetof(FollowCamera, escape_wall_yaw_speed_limit) == 0x2b0);
static_assert(offsetof(FollowCamera, escape_wall_damping) == 0x2b4);
static_assert(offsetof(FollowCamera, escape_wall_wing_yaw) == 0x2b8);
static_assert(offsetof(FollowCamera, escape_wall_wing_length) == 0x2bc);
static_assert(offsetof(FollowCamera, escape_wall_wing_radius) == 0x2c0);
static_assert(offsetof(FollowCamera, escape_wall_wing_separation) == 0x2c4);
static_assert(offsetof(FollowCamera, positive_wing_hit_fraction) == 0x2c8);
static_assert(offsetof(FollowCamera, negative_wing_hit_fraction) == 0x2cc);
static_assert(offsetof(FollowCamera, avoid_territories) == 0x2d0);
static_assert(offsetof(FollowCamera, draw_territories) == 0x2d1);
static_assert(offsetof(FollowCamera, territory_transform) == 0x2e0);
static_assert(offsetof(FollowCamera, parameter_override_id) == 0x320);
static_assert(offsetof(FollowCamera, event_parameter_id) == 0x324);
static_assert(offsetof(FollowCamera, boss_lock_parameter_id) == 0x328);
static_assert(offsetof(FollowCamera, map_parameter_id) == 0x32c);
static_assert(offsetof(FollowCamera, parameter_chase_rate) == 0x330);
static_assert(offsetof(FollowCamera, subject_control_input) == 0x334);
static_assert(offsetof(FollowCamera, fov_target_height_adjustment) == 0x344);

// Only the record floats read by this update are typed; the skipped fifth word stays opaque.
struct LockCameraParameters {
    float camera_distance;
    float minimum_pitch_degrees;
    float lock_pitch_shift_ratio;
    float subject_height_offset;
    uint8_t unknown_0x10[0x4];
    float vertical_fov_degrees;
};
static_assert(offsetof(LockCameraParameters, camera_distance) == 0x0);
static_assert(offsetof(LockCameraParameters, minimum_pitch_degrees) == 0x4);
static_assert(offsetof(LockCameraParameters, lock_pitch_shift_ratio) == 0x8);
static_assert(offsetof(LockCameraParameters, subject_height_offset) == 0xc);
static_assert(offsetof(LockCameraParameters, vertical_fov_degrees) == 0x14);

struct ParameterOverrides {
    uint8_t unknown_0x00[0x14];
    int32_t event_id;
    int32_t boss_lock_id;
};
static_assert(offsetof(ParameterOverrides, event_id) == 0x14);
static_assert(offsetof(ParameterOverrides, boss_lock_id) == 0x18);

// Variable-format binary parameter data. Row offsets and the size word before this header
// need format-aware access; no fixed C++ array of records is implied by this prefix.
struct ParameterTable {
    uint8_t unknown_0x00[0xa];
    uint16_t row_count;
    uint8_t unknown_0x0c[0x21];
    uint8_t format;
    uint8_t flags;
};
static_assert(offsetof(ParameterTable, row_count) == 0xa);
static_assert(offsetof(ParameterTable, format) == 0x2d);
static_assert(offsetof(ParameterTable, flags) == 0x2e);

struct ParameterFile {
    uint8_t unknown_0x00[0x70];
    const ParameterTable *table;
};
static_assert(offsetof(ParameterFile, table) == 0x70);

struct ParameterHolder {
    uint8_t unknown_0x00[0x70];
    const ParameterFile *file;
};
static_assert(offsetof(ParameterHolder, file) == 0x70);

struct SoloParameterRepository {
    uint8_t unknown_0x000[0x9b8];
    uint32_t lock_camera_available;
    uint8_t unknown_0x9bc[0x4];
    const ParameterHolder *lock_camera;
};
static_assert(offsetof(SoloParameterRepository, lock_camera_available) == 0x9b8);
static_assert(offsetof(SoloParameterRepository, lock_camera) == 0x9c0);

// The register ABI is RDI/RSI/RDX plus XMM0.low, regardless of the source parameter order.
RT_ORIGINAL(0x0183ac60, update_follow_camera, void(FollowCamera *, Subject *, QueryWorld *, float));
RT_ORIGINAL(0x0183fad0, follow_camera_pitch, float(const FollowCamera *));
RT_ORIGINAL(0x0183fb20, follow_camera_yaw, float(const FollowCamera *));
RT_ORIGINAL(0x02fbe698, atan2_float, float(float y, float x));

RT_GLOBAL(0x05956678, parameter_overrides, ParameterOverrides *);
RT_GLOBAL(0x05940340, solo_parameter_repository, SoloParameterRepository *);
RT_GLOBAL(0x05527a94, wall_camera_mode, uint8_t);
RT_GLOBAL(0x04b38000, default_control_input, const Vec4);
RT_GLOBAL(0x0597be80, subject_yaw_axis, const Vec4);
RT_GLOBAL(0x04cf26a0, angle_sign_mask, const uint32_t);

}  // namespace camera
