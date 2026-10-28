# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure synthetic camera inputs. Policy selection and original comparison are external.

Names describe intended edge inputs, never established path coverage or expected math.
Each run needs a fresh original reference for that exact policy and fixture bundle.
"""
from __future__ import annotations

from dataclasses import dataclass, field, replace
import json
from pathlib import Path
import struct
import sys

ADDRESS = "0x0183ac60"
CAMERA_START, CAMERA_BYTES = 0x10, 0x800
FLIPPER_BYTES, FRAME_SECONDS_OFFSET = 0x2d0, 0x264
FLIPPER_SLOT, WALL_SLOT = "0x059404f8", "0x05527a94"
SPHERE, FSIN, ATAN, TAN = 0x01c090e0, 0x02fbe518, 0x02fbe698, 0x02fbe648
VIRTUAL, AXES, DIAGNOSTIC = 0x0183fad0, 0x0135d810, 0x024b55b0
NATIVE_HELPERS = (0x00848e80, 0x01e4d420, 0x0083d4d0, 0x00829b00, 0x0084b9d0, 0x0083b7e0)


@dataclass(frozen=True)
class Bits:
    """An authored binary32 input whose payload/sign must not pass through Python float."""
    word: int


def raw8(value: int) -> bytes:
    return struct.pack("<B", value)


def raw32(value: int) -> bytes:
    return struct.pack("<I", value)


def f32(value: float | Bits) -> bytes:
    return raw32(value.word) if isinstance(value, Bits) else struct.pack("<f", value)


def vec4(values: tuple) -> bytes:
    if len(values) != 4:
        raise ValueError("a vector has four supplied lanes")
    return b"".join(f32(value) for value in values)


def store(data: bytearray, offset: int, payload: bytes) -> None:
    if offset < 0 or offset + len(payload) > len(data):
        raise ValueError("field write exceeds its declared buffer")
    data[offset:offset + len(payload)] = payload


# Offsets are relative to FollowCamera, located at camera-buffer+0x10.
# The complete allocation is not inferred: this synthetic buffer is 0x800 bytes,
# leaving 0x7f0 bytes from the supplied state pointer.
CAMERA_FIELDS = {
    'transform_axis_x': (0x10, 'vec4'),
    'transform_axis_y': (0x20, 'vec4'),
    'transform_forward': (0x30, 'vec4'),
    'transform_position': (0x40, 'vec4'),
    'vertical_fov': (0x50, 'f32'),
    'subject_transform_axis_x': (0x60, 'vec4'),
    'subject_transform_axis_y': (0x70, 'vec4'),
    'subject_transform_forward': (0x80, 'vec4'),
    'subject_transform_position': (0x90, 'vec4'),
    'target_reference': (0xa0, 'vec4'),
    'target': (0xb0, 'vec4'),
    'cast_origin': (0xc0, 'vec4'),
    'orbit_origin': (0xd0, 'vec4'),
    'fulcrum': (0xe0, 'vec4'),
    'candidate_position': (0xf0, 'vec4'),
    'chased_position': (0x100, 'vec4'),
    'translation_velocity': (0x110, 'vec4'),
    'angular_velocity': (0x124, 'f32'),
    'rotation_control_rate': (0x130, 'f32'),
    'rotation_control_wait_remaining': (0x134, 'f32'),
    'high_speed_rotation_rate': (0x138, 'f32'),
    'reset_requested': (0x13c, 'u8'),
    'reset_angles_requested': (0x13d, 'u8'),
    'angles_pitch': (0x140, 'f32'),
    'angles_yaw': (0x144, 'f32'),
    'reset_angles_pitch': (0x148, 'f32'),
    'reset_angles_yaw': (0x14c, 'f32'),
    'target_pitch': (0x150, 'f32'),
    'translation_control_end_rate': (0x154, 'f32'),
    'translation_control_wait_remaining': (0x158, 'f32'),
    'hand_control': (0x15c, 'u8'),
    'subject_origin_offset': (0x160, 'vec4'),
    'fulcrum_lateral_move_max_distance': (0x170, 'f32'),
    'fulcrum_lateral_move_min_rate': (0x174, 'f32'),
    'fulcrum_lateral_move_max_rate': (0x178, 'f32'),
    'fulcrum_distance_rate': (0x17c, 'f32'),
    'fulcrum_distance': (0x180, 'f32'),
    'camera_distance': (0x184, 'f32'),
    'target_camera_distance': (0x188, 'f32'),
    'cast_sphere_radius': (0x18c, 'f32'),
    'safe_margin_rate': (0x190, 'f32'),
    'safe_margin_max': (0x194, 'f32'),
    'subject_translation_chase_rate': (0x198, 'f32'),
    'normal_subject_translation_chase_rate': (0x19c, 'f32'),
    'escape_subject_translation_chase_rate': (0x1a0, 'f32'),
    'target_translation_chase_rate': (0x1a4, 'f32'),
    'target_pitch_chase_rate': (0x1a8, 'f32'),
    'target_chase_rate_xz': (0x1ac, 'f32'),
    'normal_target_chase_rate_xz': (0x1b0, 'f32'),
    'escape_target_chase_rate_xz': (0x1b4, 'f32'),
    'target_chase_rate_y': (0x1b8, 'f32'),
    'target_chase_min_angle': (0x1bc, 'f32'),
    'target_chase_max_angle': (0x1c0, 'f32'),
    'target_chase_distance_rate': (0x1c4, 'f32'),
    'lock_target_chase_rate_xz': (0x1c8, 'f32'),
    'lock_target_chase_rate_y': (0x1cc, 'f32'),
    'lock_target_chase_min_angle': (0x1d0, 'f32'),
    'lock_target_chase_max_angle': (0x1d4, 'f32'),
    'translation_control_end_wait_time': (0x1d8, 'f32'),
    'translation_control_end_release_time': (0x1dc, 'f32'),
    'rotation_control_end_wait_time': (0x1e0, 'f32'),
    'rotation_control_end_release_time': (0x1e4, 'f32'),
    'high_speed_rotation_ramp_time': (0x1e8, 'f32'),
    'maximum_pitch': (0x1ec, 'f32'),
    'minimum_pitch': (0x1f0, 'f32'),
    'maximum_lock_pitch': (0x1f4, 'f32'),
    'minimum_lock_pitch': (0x1f8, 'f32'),
    'rotation_range_height_begin': (0x1fc, 'f32'),
    'rotation_range_height_end': (0x200, 'f32'),
    'rotation_speed_pitch': (0x204, 'f32'),
    'rotation_speed_yaw': (0x208, 'f32'),
    'minimum_rotation_speed_pitch': (0x20c, 'f32'),
    'minimum_rotation_speed_yaw': (0x210, 'f32'),
    'maximum_rotation_speed_pitch': (0x214, 'f32'),
    'maximum_rotation_speed_yaw': (0x218, 'f32'),
    'high_rotation_speed_pitch': (0x21c, 'f32'),
    'high_rotation_speed_yaw': (0x220, 'f32'),
    'minimum_high_rotation_speed_pitch': (0x224, 'f32'),
    'minimum_high_rotation_speed_yaw': (0x228, 'f32'),
    'maximum_high_rotation_speed_pitch': (0x22c, 'f32'),
    'maximum_high_rotation_speed_yaw': (0x230, 'f32'),
    'option_rotation_speed_rate': (0x234, 'f32'),
    'direct_pitch_angle': (0x238, 'f32'),
    'direct_pitch_rate': (0x23c, 'f32'),
    'lock_rotation_chase_rate': (0x240, 'f32'),
    'lock_rotation_dead_angle': (0x244, 'f32'),
    'lock_pitch_shift_ratio': (0x248, 'f32'),
    'lock_target': (0x250, 'vec4'),
    'lock_requested': (0x260, 'u8'),
    'boss_lock_requested': (0x261, 'u8'),
    'front_lock_requested': (0x262, 'u8'),
    'front_lock_active': (0x263, 'u8'),
    'front_lock_remaining': (0x264, 'f32'),
    'front_lock_duration': (0x268, 'f32'),
    'front_lock_angles_pitch': (0x26c, 'f32'),
    'front_lock_angles_yaw': (0x270, 'f32'),
    'update_target_pitch_at_lock': (0x274, 'u8'),
    'update_target_pitch_at_front_lock': (0x275, 'u8'),
    'adjust_lock_camera': (0x276, 'u8'),
    'lock_adjustment_pitch': (0x278, 'f32'),
    'lock_adjustment_yaw': (0x27c, 'f32'),
    'maximum_lock_adjustment_pitch': (0x280, 'f32'),
    'maximum_lock_adjustment_yaw': (0x284, 'f32'),
    'lock_adjustment_chase_rate': (0x288, 'f32'),
    'escape_wall_mode': (0x28c, 'u8'),
    'escape_yaw_opaque_prefix': (0x290, 'u32'),
    'escape_wall_yaw': (0x294, 'f32'),
    'escape_wall_yaw_increment_rate': (0x29c, 'f32'),
    'weak_escape_wall_yaw_increment_rate': (0x2a4, 'f32'),
    'weak_escape_wall_begin_fraction': (0x2a8, 'f32'),
    'escape_wall_yaw_speed_limit': (0x2b0, 'f32'),
    'escape_wall_damping': (0x2b4, 'f32'),
    'escape_wall_wing_yaw': (0x2b8, 'f32'),
    'escape_wall_wing_length': (0x2bc, 'f32'),
    'escape_wall_wing_radius': (0x2c0, 'f32'),
    'escape_wall_wing_separation': (0x2c4, 'f32'),
    'positive_wing_hit_fraction': (0x2c8, 'f32'),
    'negative_wing_hit_fraction': (0x2cc, 'f32'),
    'avoid_territories': (0x2d0, 'u8'),
    'draw_territories': (0x2d1, 'u8'),
    'territory_transform_axis_x': (0x2e0, 'vec4'),
    'territory_transform_axis_y': (0x2f0, 'vec4'),
    'territory_transform_forward': (0x300, 'vec4'),
    'territory_transform_position': (0x310, 'vec4'),
    'parameter_override_id': (0x320, 'u32'),
    'event_parameter_id': (0x324, 'u32'),
    'boss_lock_parameter_id': (0x328, 'u32'),
    'map_parameter_id': (0x32c, 'u32'),
    'parameter_chase_rate': (0x330, 'f32'),
    'subject_control_input': (0x334, 'u8'),
    'fov_target_height_adjustment': (0x344, 'u8'),
}


def field_bytes(name: str, value) -> bytes:
    kind = CAMERA_FIELDS[name][1]
    return {"f32": f32, "vec4": vec4, "u8": raw8, "u32": raw32}[kind](value)


def write_fields(data: bytearray, fields: dict, start: int = CAMERA_START) -> None:
    for name, value in fields.items():
        store(data, start + CAMERA_FIELDS[name][0], field_bytes(name, value))


BASE_CAMERA = {
    'transform_axis_x': (1.0, 0.0, 0.0, 0.0),
    'transform_axis_y': (0.0, 1.0, 0.0, 0.0),
    'transform_forward': (0.0, 0.0, 1.0, 0.0),
    'transform_position': (0.0, 0.0, 5.0, 1.0),
    'vertical_fov': 0.75,
    'subject_transform_axis_x': (0.0, 0.0, 1.0, 0.0),
    'subject_transform_axis_y': (0.0, 1.0, 0.0, 0.0),
    'subject_transform_forward': (-1.0, 0.0, 0.0, 0.0),
    'subject_transform_position': (0.0, 0.0, 0.0, 1.0),
    'target_reference': (0.0, 0.0, 0.0, 1.0),
    'target': (0.0, 0.0, 0.0, 1.0),
    'cast_origin': (0.0, 0.0, 0.0, 1.0),
    'orbit_origin': (0.0, 0.0, 0.0, 1.0),
    'fulcrum': (0.0, 0.0, 2.0, 1.0),
    'candidate_position': (0.0, 0.0, 4.0, 1.0),
    'chased_position': (0.0, 0.0, -1.0, 1.0),
    'translation_velocity': (0.0, 0.0, 0.0, 0.0),
    'angular_velocity': 0.0,
    'rotation_control_rate': 0.75,
    'rotation_control_wait_remaining': 0.5,
    'high_speed_rotation_rate': 0.0,
    'reset_requested': 0x1,
    'reset_angles_requested': 0x1,
    'angles_pitch': 0.2,
    'angles_yaw': -0.3,
    'reset_angles_pitch': 0.4,
    'reset_angles_yaw': -0.6,
    'target_pitch': 0.1,
    'translation_control_end_rate': 0.75,
    'translation_control_wait_remaining': 0.5,
    'hand_control': 0x0,
    'subject_origin_offset': (0.0, 0.0, 0.0, 1.0),
    'fulcrum_lateral_move_max_distance': 0.0,
    'fulcrum_lateral_move_min_rate': 0.25,
    'fulcrum_lateral_move_max_rate': 0.75,
    'fulcrum_distance_rate': 0.0,
    'fulcrum_distance': 2.0,
    'camera_distance': 14.0,
    'target_camera_distance': 12.0,
    'cast_sphere_radius': 0.125,
    'safe_margin_rate': 0.0,
    'safe_margin_max': 0.0,
    'subject_translation_chase_rate': 0.625,
    'normal_subject_translation_chase_rate': 1.5,
    'escape_subject_translation_chase_rate': 3.25,
    'target_translation_chase_rate': 0.25,
    'target_pitch_chase_rate': 0.5,
    'target_chase_rate_xz': 0.875,
    'normal_target_chase_rate_xz': 2.25,
    'escape_target_chase_rate_xz': -4.5,
    'target_chase_rate_y': 0.75,
    'target_chase_min_angle': 0.25,
    'target_chase_max_angle': 1.25,
    'target_chase_distance_rate': 0.5,
    'lock_target_chase_rate_xz': 0.375,
    'lock_target_chase_rate_y': 0.625,
    'lock_target_chase_min_angle': 0.5,
    'lock_target_chase_max_angle': 1.5,
    'translation_control_end_wait_time': 1.25,
    'translation_control_end_release_time': 2.0,
    'rotation_control_end_wait_time': 0.75,
    'rotation_control_end_release_time': 2.0,
    'high_speed_rotation_ramp_time': 2.0,
    'maximum_pitch': 1.2,
    'minimum_pitch': -0.8,
    'maximum_lock_pitch': 0.75,
    'minimum_lock_pitch': -0.5,
    'rotation_range_height_begin': 0.2,
    'rotation_range_height_end': 5.0,
    'minimum_rotation_speed_pitch': 0.1,
    'minimum_rotation_speed_yaw': 0.2,
    'maximum_rotation_speed_pitch': 0.9,
    'maximum_rotation_speed_yaw': 1.2,
    'minimum_high_rotation_speed_pitch': 1.0,
    'minimum_high_rotation_speed_yaw': 1.5,
    'maximum_high_rotation_speed_pitch': 3.0,
    'maximum_high_rotation_speed_yaw': 4.5,
    'option_rotation_speed_rate': 0.5,
    'direct_pitch_angle': 0.6,
    'direct_pitch_rate': 0.25,
    'lock_rotation_chase_rate': 0.5,
    'lock_rotation_dead_angle': 0.1,
    'lock_pitch_shift_ratio': 0.4,
    'lock_target': (2.0, 3.0, 5.0, 1.0),
    'lock_requested': 0x0,
    'boss_lock_requested': 0x0,
    'front_lock_requested': 0x0,
    'front_lock_active': 0x0,
    'front_lock_remaining': 3.0,
    'front_lock_duration': 3.0,
    'front_lock_angles_pitch': -0.1,
    'front_lock_angles_yaw': 0.25,
    'update_target_pitch_at_lock': 0x0,
    'update_target_pitch_at_front_lock': 0x0,
    'adjust_lock_camera': 0x1,
    'lock_adjustment_pitch': 0.05,
    'lock_adjustment_yaw': -0.075,
    'maximum_lock_adjustment_pitch': 0.2,
    'maximum_lock_adjustment_yaw': 0.3,
    'lock_adjustment_chase_rate': 0.5,
    'escape_wall_mode': 0x1,
    'escape_yaw_opaque_prefix': 0x6badf00d,
    'escape_wall_yaw': 0.375,
    'escape_wall_yaw_increment_rate': 0.125,
    'weak_escape_wall_yaw_increment_rate': 0.03125,
    'weak_escape_wall_begin_fraction': 0.5,
    'escape_wall_yaw_speed_limit': 0.25,
    'escape_wall_damping': 0.5,
    'escape_wall_wing_yaw': 0.375,
    'escape_wall_wing_length': 4.0,
    'escape_wall_wing_radius': 0.0625,
    'escape_wall_wing_separation': 0.5,
    'positive_wing_hit_fraction': Bits(0x7fc12345),
    'negative_wing_hit_fraction': Bits(0xffc54321),
    'avoid_territories': 0x0,
    'draw_territories': 0x0,
    'parameter_override_id': 0xffffffff,
    'event_parameter_id': 0xffffffff,
    'boss_lock_parameter_id': 0xffffffff,
    'map_parameter_id': 0x5,
    'parameter_chase_rate': 0.25,
    'subject_control_input': 0x1,
    'fov_target_height_adjustment': 0x0,
}


def camera_buffer(fields: dict) -> bytearray:
    data = bytearray([0xa5]) * CAMERA_BYTES
    write_fields(data, BASE_CAMERA)
    write_fields(data, fields)
    return data


@dataclass(frozen=True)
class Write:
    arg: int
    offset: int = 0
    data: bytes = b""
    pointer: str | None = None

    def wire(self) -> dict:
        item = {"arg": self.arg}
        if self.offset:
            item["offset"] = self.offset
        item.update({"pointer": self.pointer} if self.pointer else {"bytes": self.data.hex()})
        return item


def camera_write(name: str, value) -> Write:
    return Write(0, CAMERA_FIELDS[name][0], field_bytes(name, value))


@dataclass(frozen=True)
class Sphere:
    ret: int = 0
    writes: tuple[Write, ...] = ()
    world_capture: str = ""

    def wire(self) -> dict:
        captures = [{"arg": 2, "size": 16}, {"arg": 3, "size": 16}]
        if self.world_capture == "wall":
            captures.append({"arg": 0, "size": 1})
        elif self.world_capture == "camera":
            for name, width in (("escape_wall_mode", 1), ("escape_wall_yaw", 4),
                                ("escape_wall_wing_yaw", 4), ("escape_wall_wing_radius", 4),
                                ("positive_wing_hit_fraction", 8)):
                captures.append({"arg": 0, "offset": CAMERA_FIELDS[name][0], "size": width})
        elif self.world_capture:
            raise ValueError("unknown world capture contract")
        result = {"address": hex(SPHERE), "argc": 7, "argf32": [0],
                  "argmem": captures, "ret": hex(self.ret)}
        if self.writes:
            result["writes"] = [write.wire() for write in self.writes]
        return result


@dataclass(frozen=True)
class Node:
    point: tuple
    extent: int
    flags: int


@dataclass(frozen=True)
class Profile:
    id: str
    category: str
    camera: dict = field(default_factory=dict)
    preferred: tuple = (0.0, 0.0, 0.0, 0.0)
    fallback: tuple = (0.75, 0.125, 13.0, -11.0)
    wall: int = 0
    world: str = "query"
    null_subject: bool = False
    time: tuple = (1.0, 0.0, 0.0, 0.0)
    spheres: tuple[Sphere, ...] = (Sphere(), Sphere(), Sphere())
    territory: bool = False
    nodes: tuple[Node, ...] = ()
    render_mode: str = ""
    measured: Bits = Bits(0x3d088889)
    measured_after: Bits = Bits(0x3d088889)
    parameter_available: bool = False
    row_camera_distance: float | Bits = 16.0
    fps_callbacks: tuple[tuple[int, Write], ...] = ()


# Profiles contain only supplied fields, object aliases, scalar returns and writes.
# Sphere queue positions are authored hypotheses; guards/unscripted calls remain errors.
STOCK_PROFILES = (
    Profile('stock_wall0_finite',
        'V5_integration'),
    Profile('stock_wall1_both_miss',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1),
    Profile('stock_reset_nonboolean',
        'V5_integration',
        camera={
            'reset_requested': 0x80,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('stock_lock_nonboolean',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
            'lock_requested': 0x80,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('stock_front_nonboolean',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
            'lock_rotation_chase_rate': 0.0,
            'front_lock_active': 0x80,
            'front_lock_remaining': 10.0,
            'front_lock_angles_pitch': 2.0,
            'front_lock_angles_yaw': 2.75,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('stock_hand_control',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        preferred=(0.25, 0.0, 0.0, 0.0),
        fallback=(0.25, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x80, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x80, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('stock_candidate_and_wing_queries',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        world='wall_slot',
        spheres=(
            Sphere(world_capture='wall'),
            Sphere(world_capture='wall', writes=(Write(0, data=raw8(1)),)),
            Sphere(world_capture='wall'),
            Sphere(world_capture='wall'),
            Sphere(),
        )),
    Profile('stock_reload_radius',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        world='camera',
        spheres=(
            Sphere(ret=0x1, world_capture='camera', writes=(Write(6, data=f32(0.25)), camera_write('escape_wall_wing_radius', Bits(0xffc54321)),)),
            Sphere(world_capture='camera'),
            Sphere(),
        )),
    Profile('stock_reload_wall_mode_zero',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        world='camera',
        spheres=(
            Sphere(ret=0x80, world_capture='camera', writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x80, world_capture='camera', writes=(Write(6, data=f32(0.75)), camera_write('escape_wall_mode', 0x0),)),
            Sphere(),
        )),
    Profile('stock_saved_inputs_first',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)), Write(2, data=vec4((211.0, -223.0, 227.0, Bits(0xffc54321)))), Write(3, data=vec4((-31.0, 37.0, -41.0, Bits(0x7f812345)))),)),
            Sphere(),
            Sphere(),
        )),
    Profile('stock_saved_inputs_second',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(),
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.75)), Write(2, data=vec4((211.0, -223.0, 227.0, Bits(0xffc54321)))), Write(3, data=vec4((-31.0, 37.0, -41.0, Bits(0x7f812345)))),)),
            Sphere(),
        )),
    Profile('stock_hits_shared_80',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x80, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x80, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('stock_hits_disjoint_01_80',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x80, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('stock_high_rax_al80',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0xdeadbeef00000080, writes=(Write(6, data=f32(0.25)),)),
            Sphere(),
            Sphere(),
        )),
    Profile('stock_fraction_snan',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(Bits(0x7f812345))),)),
            Sphere(),
            Sphere(),
        )),
    Profile('stock_time_upper_noise',
        'V5_integration',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        time=(1.0, Bits(0xffffffff), Bits(0xffffffff), Bits(0xffffffff))),
    Profile('chase_normal_distinct_rates',
        'chase',
        camera={
            'target_chase_rate_y': 0.125,
            'target_chase_distance_rate': 0.75,
        }),
    Profile('chase_reset_bypass',
        'chase',
        camera={
            'reset_requested': 0x80,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('chase_lock_selector',
        'chase',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
            'lock_target_chase_rate_xz': 0.875,
            'lock_target_chase_rate_y': 0.125,
            'lock_requested': 0x80,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('chase_front_selector',
        'chase',
        camera={
            'reset_requested': 0x0,
            'reset_angles_requested': 0x0,
            'lock_target_chase_rate_xz': 0.875,
            'lock_target_chase_rate_y': 0.125,
            'lock_rotation_chase_rate': 0.0,
            'front_lock_active': 0x80,
            'front_lock_remaining': 10.0,
            'front_lock_angles_pitch': 2.0,
            'front_lock_angles_yaw': 2.75,
        },
        fallback=(0.0, 0.0, 0.0, 0.0),
        wall=1,
        spheres=(
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.25)),)),
            Sphere(ret=0x1, writes=(Write(6, data=f32(0.75)),)),
            Sphere(),
        )),
    Profile('chase_repair_both_limits',
        'chase',
        camera={
            'target_chase_min_angle': 1.25,
            'target_chase_max_angle': 0.25,
            'lock_target_chase_min_angle': 1.5,
            'lock_target_chase_max_angle': 0.5,
        }),
    Profile('chase_unordered_limits',
        'chase',
        camera={
            'target_chase_min_angle': Bits(0x7fc12345),
            'target_chase_max_angle': Bits(0xffc54321),
            'lock_target_chase_min_angle': Bits(0xffc54321),
            'lock_target_chase_max_angle': Bits(0x7fc12345),
        }),
    Profile('chase_negative_distance_rate',
        'chase',
        camera={
            'target_chase_min_angle': -3.25,
            'target_chase_max_angle': 7.5,
            'target_chase_distance_rate': -0.5,
        }),
    Profile('chase_signed_zero_rates',
        'chase',
        camera={
            'target_chase_rate_y': Bits(0x80000000),
            'target_chase_min_angle': Bits(0x80000000),
            'target_chase_max_angle': 0.0,
            'target_chase_distance_rate': Bits(0x80000000),
        }),
    Profile('margin_positive_cap',
        'margin',
        camera={
            'safe_margin_rate': 0.5,
            'safe_margin_max': 0.25,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (3.0, 4.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, 0.0, 1.0)),)),
        )),
    Profile('margin_negative_rate',
        'margin',
        camera={
            'safe_margin_rate': -0.25,
            'safe_margin_max': 0.5,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (3.0, 4.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, 0.0, 1.0)),)),
        )),
    Profile('margin_unordered_rate',
        'margin',
        camera={
            'safe_margin_rate': Bits(0x7fc12345),
            'safe_margin_max': 0.25,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (3.0, 4.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, 0.0, 1.0)),)),
        )),
    Profile('margin_unordered_maximum',
        'margin',
        camera={
            'safe_margin_rate': 0.25,
            'safe_margin_max': Bits(0xffc54321),
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (3.0, 4.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, 0.0, 1.0)),)),
        )),
    Profile('null_subject_default',
        'null_subject',
        null_subject=True,
        spheres=(

        )),
    Profile('null_subject_override',
        'null_subject',
        camera={
            'parameter_override_id': 0x7,
        },
        null_subject=True,
        spheres=(

        )),
    Profile('null_subject_boss',
        'null_subject',
        camera={
            'boss_lock_requested': 0x80,
            'boss_lock_parameter_id': 0x3,
        },
        null_subject=True,
        spheres=(

        )),
    Profile('null_subject_signed_ids',
        'null_subject',
        camera={
            'event_parameter_id': 0xfffffffe,
            'map_parameter_id': 0x80000000,
        },
        null_subject=True,
        spheres=(

        )),
    Profile('territory_empty_owner',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        territory=True),
    Profile('territory_disabled_flag_zero',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 255, 0),),
        render_mode='dirty'),
    Profile('territory_other_flag_only',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 255, 1),),
        render_mode='dirty'),
    Profile('territory_enabled_far',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
        ),
        territory=True,
        nodes=(Node((1e+03, 0.0, 0.0, 1.0), 1, 2),)),
    Profile('territory_extent_zero_equal',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
        ),
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 0, 2),)),
    Profile('territory_inside_null_world',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        world='null',
        spheres=(

        ),
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 255, 2),)),
    Profile('territory_inside_query_miss',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
            Sphere(),
        ),
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 4, 2),)),
    Profile('territory_inside_query_high_RAX_hit',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
            Sphere(ret=0xdeadbeef00000080, writes=(Write(4, data=vec4((101.0, -103.0, 107.0, 1.0))), Write(6, data=f32(0.25)),)),
        ),
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 4, 2),)),
    Profile('territory_extent_unsigned_ff',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
            Sphere(),
        ),
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 255, 2),)),
    Profile('territory_two_node_link',
        'territory',
        camera={
            'avoid_territories': 0x80,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
            Sphere(),
        ),
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 4, 0), Node((0.0, 0.0, 0.0, 1.0), 4, 2),)),
    Profile('territory_draw_dirty_fields',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
        ),
        territory=True,
        nodes=(Node((1e+03, 0.0, 0.0, 1.0), 3, 2),),
        render_mode='dirty'),
    Profile('territory_draw_equal_fields',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
        ),
        territory=True,
        nodes=(Node((1e+03, 0.0, 0.0, 1.0), 3, 2),),
        render_mode='equal_fields'),
    Profile('territory_draw_reselect_alias',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
        ),
        territory=True,
        nodes=(Node((1e+03, 0.0, 0.0, 1.0), 3, 2),),
        render_mode='reselected_alias'),
    Profile('territory_draw_slot_one',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
        ),
        territory=True,
        nodes=(Node((1e+03, 0.0, 0.0, 1.0), 3, 2),),
        render_mode='slot_1'),
    Profile('territory_draw_two_nodes_slot_change',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
        ),
        territory=True,
        nodes=(Node((1e+03, 0.0, 0.0, 1.0), 3, 2), Node((1e+03, 0.0, 0.0, 1.0), 3, 2),),
        render_mode='axes_select_slot_1'),
    Profile('territory_query_clears_late_draw',
        'territory',
        camera={
            'avoid_territories': 0x80,
            'draw_territories': 0x1,
        },
        world='camera',
        spheres=(
            Sphere(),
            Sphere(),
            Sphere(writes=(camera_write('transform_forward', (0.0, 0.0, 1.0, 0.0)), camera_write('transform_position', (0.0, 0.0, 0.0, 1.0)), camera_write('cast_origin', (0.0, 0.0, -2.0, 1.0)),)),
            Sphere(writes=(camera_write('draw_territories', 0x0),)),
        ),
        territory=True,
        nodes=(Node((0.0, 0.0, 0.0, 1.0), 4, 2),),
        render_mode='dirty'),
)


BUFFER_LAYOUT = {
    "camera": (0x800, 0xa5), "overrides": (0x40, 0xb6), "repository": (0xa00, 0xc7),
    "holder": (0x80, 0xc3), "file": (0x80, 0xd4), "table": (0x800, 0xe9),
    "guard": (0x30, 0xd8), "tls": (0x40, 0xe5), "query": (0x40, 0xf6),
    "subject": (0x3c0, 0), "subject_context": (0x290, 0),
    "control_preferred": (0x50, 0), "control_fallback": (0x50, 0),
    "early_owner": (0x3c0, 0), "early_link": (0x70, 0), "early_position": (0x1f0, 0),
    "late_link": (0x70, 0), "late_transform": (0x330, 0),
    "territory_owner": (0x20, 0), "territory_0": (0x70, 0), "territory_1": (0x70, 0),
    "render_manager": (0x30, 0), "renderer": (0x30, 0),
    "render_slot_0": (0x50, 0), "render_slot_1": (0x50, 0),
    "render_state_a": (0x80, 0), "render_state_b": (0x80, 0), "render_vtable": (0x30, 0),
    "flipper_before": (FLIPPER_BYTES, 0), "flipper_after": (FLIPPER_BYTES, 0),
}


def parameter_table(profile: Profile, *, version: int = 4, flags: int = 2) -> bytearray:
    """Build key/index pairs and relative row offsets; no recovered image data is read."""
    data, header = bytearray([0xe9]) * 0x800, 0x10
    keys = (0, 5, 7, 9, 11, 13, 0x80000000, 0xffffffff)
    store(data, header - 0x10, raw32(0x100))
    store(data, header + 0xa, struct.pack("<H", len(keys)))
    store(data, header + 0x2d, raw8(version) + raw8(flags))
    for index, key in enumerate(keys):
        store(data, header + 0x100 + index * 8, struct.pack("<Ii", key, index))
    for index in range(len(keys)):
        row = (10.0 + index * 7.0, -40.0 + index * 3.0, 0.25 + index * 0.125,
               1.0 + index * 1.5, Bits(0x7f8abcde), 39.0 + index)
        # Every FPS profile selects key 5/index 1 through the ordinary map-ID path.
        # The skipped fifth word is an independent opaque supplied payload.
        if profile.parameter_available and index == 1:
            row = (profile.row_camera_distance, -45.0, 0.5, 1.0, Bits(0x7f812345), 55.0)
        relative = 0x200 + index * 0x30
        store(data, header + relative, b"".join(f32(value) for value in row))
        if version == 2:
            store(data, header + 0x34 + index * 12, raw32(relative))
        elif version >= 4 and flags & 2:
            store(data, header + 0x48 + index * 24, struct.pack("<Q", relative))
        else:
            store(data, header + 0x44 + index * 12, raw32(relative))
    return data


def preset(memory: list, address: str, *, data: bytes | None = None,
           pointer: str | None = None, guest: int | None = None) -> None:
    if sum(value is not None for value in (data, pointer, guest)) != 1:
        raise ValueError("a preset supplies exactly one payload")
    item = {"addr": address}
    item.update({"bytes": data.hex()} if data is not None else
                {"pointer": pointer} if pointer is not None else {"guest": hex(guest)})
    memory.append(item)


def parameter_graph(buffers: dict, memory: list, profile: Profile) -> None:
    store(buffers["overrides"], 0x14, raw32(0xffffffff) + raw32(0xffffffff))
    store(buffers["repository"], 0x9b8, raw32(int(profile.parameter_available)))
    store(buffers["guard"], 8, struct.pack("<Q", 0x123456789abcdef0))
    buffers["table"] = parameter_table(profile)
    for address, pointer in (("0x05956678", "overrides"), ("0x05940340", "repository"),
                             ("buf:repository+0x9c0", "holder"), ("buf:holder+0x70", "file"),
                             ("buf:file+0x70", "table+0x10"), ("0x057e3be0", "guard+0x8"),
                             ("buf:tls", "tls")):
        preset(memory, address, pointer=pointer)


def subject_graph(memory: list, profile: Profile) -> None:
    for address, pointer in (("buf:subject+0x58", "subject_context"),
                             ("buf:subject+0x3b0", "late_link"),
                             ("buf:late_link+0x68", "late_transform"),
                             ("buf:subject_context+0x8", "early_owner"),
                             ("buf:early_owner+0x3b0", "early_link"),
                             ("buf:early_link+0x68", "early_position"),
                             ("buf:subject_context+0x80", "control_fallback"),
                             ("buf:subject_context+0x280", "control_preferred")):
        preset(memory, address, pointer=pointer)
    for address, vector in (("buf:early_position+0x1e0", (-2.0, 7.0, 11.0, 1.0)),
                            ("buf:late_transform+0x1d0", (0.0, 0.0, 0.0, 1.0)),
                            ("buf:late_transform+0x1e0", (0.0, 0.0, 0.0, 1.0)),
                            ("buf:control_preferred+0x40", profile.preferred),
                            ("buf:control_fallback+0x40", profile.fallback),
                            ("0x0597be80", (0.0, 1.0, 0.0, 0.0))):
        preset(memory, address, data=vec4(vector))
    preset(memory, "buf:late_transform+0x326", data=raw8(0))
    preset(memory, WALL_SLOT, data=raw8(profile.wall))


def territory_graph(buffers: dict, memory: list, profile: Profile) -> None:
    if len(profile.nodes) > 2:
        raise ValueError("territory graphs have at most two acyclic nodes")
    preset(memory, "0x0593e858", pointer="territory_owner")
    if profile.nodes:
        preset(memory, "buf:territory_owner+0x8", pointer="territory_0")
    for index, node in enumerate(profile.nodes):
        name = f"territory_{index}"
        store(buffers[name], 0, vec4(node.point))
        store(buffers[name], 0x60, raw8(node.extent) + raw8(node.flags))
        if index + 1 < len(profile.nodes):
            preset(memory, f"buf:{name}+0x68", pointer=f"territory_{index + 1}")


def renderer_graph(memory: list, profile: Profile) -> None:
    """Even inactive render graphs are declared and bounded; no fatal-recovery write."""
    alias = profile.render_mode == "reselected_alias"
    for address, pointer in (("0x05940298", "render_manager"),
                             ("buf:render_manager+0x20", "renderer"),
                             ("buf:renderer+0x10", "render_state_a" if alias else "render_slot_0"),
                             ("buf:renderer+0x18", "render_slot_1"),
                             ("buf:render_slot_0+0x40", "render_state_a"),
                             ("buf:render_slot_1+0x40", "render_state_b"),
                             ("buf:render_state_a", "render_vtable"),
                             ("buf:render_state_b", "render_vtable")):
        preset(memory, address, pointer=pointer)
    preset(memory, "buf:renderer+0x20", data=raw32(int(profile.render_mode == "slot_1")))
    preset(memory, "buf:render_vtable+0x28", guest=VIRTUAL)
    if alias:
        preset(memory, "buf:render_state_a+0x40", pointer="render_state_a")
    for name in ("render_state_a", "render_state_b"):
        preset(memory, f"buf:{name}+0x8", data=raw8(1))
        if profile.render_mode == "equal_fields":
            preset(memory, f"buf:{name}+0x18", data=raw32(2) + raw32(1))
            for offset in (0x20, 0x30):
                preset(memory, f"buf:{name}+0x{offset:x}", data=vec4((0.25, 0.0, 0.25, 0.4)))
            preset(memory, f"buf:{name}+0x70", data=raw32(2))


def flipper_graph(buffers: dict, memory: list, profile: Profile) -> None:
    store(buffers["flipper_before"], FRAME_SECONDS_OFFSET, f32(profile.measured))
    store(buffers["flipper_after"], FRAME_SECONDS_OFFSET, f32(profile.measured_after))
    preset(memory, FLIPPER_SLOT, pointer="flipper_before")


def trig_scripts() -> list:
    # FIFO return values are supplied callback behavior, not target-result goldens.
    sine_cosine = (0.375, 0.875, -0.625, 0.75, 0.25, 0.75, -0.25, 0.75,
                   0.375, 0.875, -0.375, 0.875, 0.5, 0.875, 0.25, 0.75,
                   -0.375, 0.625, 0.125, 0.75, -0.125, 0.75)
    scripts = [{"address": hex(FSIN), "argc": 2, "argi": [0, 1], "argf32": [0],
                "ret": hex(struct.unpack("<I", f32(value))[0])} for value in sine_cosine]
    for value in (0.25, -0.5, 0.75, -1.0, 0.125, 0.5, -0.25):
        scripts.append({"address": hex(ATAN), "argc": 0, "argf32": [0, 1],
                        "ret": hex(struct.unpack("<I", f32(value))[0])})
    for value in (0.5, -0.25, 0.75):
        scripts.append({"address": hex(TAN), "argc": 0, "argf32": [0],
                        "ret": hex(struct.unpack("<I", f32(value))[0])})
    return scripts


def debug_scripts(profile: Profile) -> list:
    scripts = []
    for index in range(4):
        virtual = {"address": hex(VIRTUAL), "argc": 2, "argi": [0, 1],
                   "argmem": [{"arg": 0, "size": 0x40}], "ret": "0x0"}
        axes = {"address": hex(AXES), "argc": 2,
                "argmem": [{"arg": 1, "size": 0x30}], "ret": "0x0"}
        # Explicit first-entry mutations retain the renderer-alias inputs.
        # All remaining entries are inert guards; counts/gates remain hypotheses.
        if index == 0 and profile.render_mode == "reselected_alias":
            virtual["writes"] = [Write(0, 0x40, pointer="render_state_b").wire()]
        if index == 0 and profile.render_mode == "axes_select_slot_1":
            axes["writes"] = [Write(0, 0x20, raw32(1)).wire()]
        scripts.extend((virtual, axes))
    return scripts


def make_case(profile: Profile) -> dict:
    buffers = {name: bytearray([fill]) * size for name, (size, fill) in BUFFER_LAYOUT.items()}
    buffers["camera"] = camera_buffer(profile.camera)
    memory = []
    parameter_graph(buffers, memory, profile)
    subject_graph(memory, profile)
    territory_graph(buffers, memory, profile)
    renderer_graph(memory, profile)
    flipper_graph(buffers, memory, profile)
    world = {"query": "buf:query", "null": "0x0", "camera": "buf:camera+0x10",
             "wall_slot": "guest:" + WALL_SLOT, "flipper_object": "buf:flipper_before",
             "flipper_slot": "guest:" + FLIPPER_SLOT}[profile.world]
    spheres = list(profile.spheres)
    for index, write in profile.fps_callbacks:
        if index < 0 or index >= len(spheres):
            raise ValueError("callback selects an undeclared query queue entry")
        spheres[index] = replace(spheres[index], writes=(*spheres[index].writes, write))
    # Never read optional sphere outputs before the callback. A miss writes none.
    scripts = [{"address": hex(DIAGNOSTIC), "argc": 4, "ret": 0}]
    scripts += trig_scripts() + debug_scripts(profile)
    scripts += [sphere.wire() for sphere in spheres]
    scripts += [Sphere().wire() for _ in range(3)]
    return {"schema": 1, "address": ADDRESS, "id": profile.id, "returns": "void",
            "args": {"rdi": "buf:camera+0x10", "rsi": "0x0" if profile.null_subject else "buf:subject",
                     "rdx": world, "xmm0": vec4(profile.time).hex()},
            "buffers": {name: {"size": len(data), "bytes": data.hex(), "alignment": 16}
                        for name, data in buffers.items()},
            "memory": memory, "gs": "buf:tls", "imports": [], "stubs": scripts,
            "fixture_scope": "Synthetic full camera input; policy and all path/count sensitivity require original comparison"}


# Below-cap input refinements add only new measured-flipper storage to four stock
# profiles. They are input challenges, without promised path or mutant sensitivity.
STOCK_PROFILES = tuple(replace(profile, measured=Bits(0x3c345678))
                       if profile.id in ("chase_lock_selector", "chase_front_selector",
                                         "chase_reset_bypass", "chase_normal_distinct_rates")
                       else profile for profile in STOCK_PROFILES)


def fps_profiles() -> tuple[Profile, ...]:
    stocks = {profile.id: profile for profile in STOCK_PROFILES}
    profiles = []
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_zero',
                            category='FPS',
                            measured=Bits(0x00000000),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445))))
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_negative_zero',
                            category='FPS',
                            measured=Bits(0x80000000),
                            parameter_available=True,
                            time=(Bits(0x3d800000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445))))
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_negative',
                            category='FPS',
                            measured=Bits(0xbc088889),
                            parameter_available=True,
                            time=(Bits(0x3e000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445))))
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_sixty_exact',
                            category='FPS',
                            measured=Bits(0x3c888889),
                            parameter_available=True,
                            time=(Bits(0x3b088889), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445))))
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_sixty_next',
                            category='FPS',
                            measured=Bits(0x3c88888a),
                            parameter_available=True,
                            time=(Bits(0x3c088889), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445))))
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_thirty_exact',
                            category='FPS',
                            measured=Bits(0x3d088889),
                            parameter_available=True,
                            time=(Bits(0x3a888889), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445))))
    camera = {**stocks['stock_wall0_finite'].camera,
              'parameter_chase_rate': Bits(0x3eaaaaab),
              'target_camera_distance': Bits(0xffc54321),
              'target_chase_rate_xz': Bits(0x7fc12345),
              'target_chase_rate_y': Bits(0xffc54321),
              }
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_qnan_gain_payloads',
                            category='FPS',
                            measured=Bits(0x7fc12345),
                            parameter_available=True,
                            time=(Bits(0x3d4ccccd), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            camera=camera,
                            row_camera_distance=Bits(0x7fc12345)))
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_interval_positive_infinity',
                            category='FPS',
                            measured=Bits(0x7f800000),
                            parameter_available=True,
                            time=(Bits(0x00000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445))))
    camera = {**stocks['stock_wall0_finite'].camera,
              'parameter_chase_rate': Bits(0x00000000),
              'subject_translation_chase_rate': Bits(0xbe4ccccd),
              'target_chase_rate_xz': Bits(0x00000000),
              'target_chase_rate_y': Bits(0xbf000000),
              'target_chase_distance_rate': Bits(0xbeaaaaab),
              'target_pitch_chase_rate': Bits(0x00000000),
              }
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_gain_zero_negative',
                            category='FPS',
                            measured=Bits(0x3c345678),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            camera=camera))
    camera = {**stocks['stock_wall0_finite'].camera,
              'parameter_chase_rate': Bits(0x3ec00000),
              'subject_translation_chase_rate': Bits(0x40800000),
              'target_chase_rate_xz': Bits(0x40800000),
              'target_chase_rate_y': Bits(0x40800000),
              'target_chase_distance_rate': Bits(0x40800000),
              'target_pitch_chase_rate': Bits(0xc0400000),
              'lock_rotation_chase_rate': Bits(0x40800000),
              }
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_gain_denominator_pole',
                            category='FPS',
                            measured=Bits(0x3c088889),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            camera=camera))
    camera = {**stocks['stock_hand_control'].camera,
              'high_speed_rotation_rate': Bits(0x3f800000),
              'high_speed_rotation_ramp_time': Bits(0x3e800000),
              'direct_pitch_angle': Bits(0x3f19999a),
              'direct_pitch_rate': Bits(0x3f400000),
              'maximum_pitch': Bits(0x3f800000),
              'minimum_pitch': Bits(0xbf800000),
              'reset_requested': 0,
              'lock_requested': 0,
              'front_lock_active': 0,
              'front_lock_requested': 0,
              'subject_control_input': 1,
              }
    profiles.append(replace(stocks['stock_hand_control'],
                            id='fps_direct_pitch_positive',
                            category='FPS',
                            measured=Bits(0x3c088889),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            camera=camera,
                            preferred=(Bits(0x3f000000), Bits(0x40000000), Bits(0x00000000), Bits(0x00000000)),
                            fallback=(Bits(0x3f000000), Bits(0x40000000), Bits(0x00000000), Bits(0x00000000))))
    camera = {**stocks['stock_hand_control'].camera,
              'high_speed_rotation_rate': Bits(0x3f800000),
              'high_speed_rotation_ramp_time': Bits(0x3e800000),
              'direct_pitch_angle': Bits(0x3f19999a),
              'direct_pitch_rate': Bits(0x3f400000),
              'maximum_pitch': Bits(0x3f800000),
              'minimum_pitch': Bits(0xbf800000),
              'reset_requested': 0,
              'lock_requested': 0,
              'front_lock_active': 0,
              'front_lock_requested': 0,
              'subject_control_input': 1,
              }
    profiles.append(replace(stocks['stock_hand_control'],
                            id='fps_direct_pitch_negative',
                            category='FPS',
                            measured=Bits(0x3c088889),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            camera=camera,
                            preferred=(Bits(0xbf000000), Bits(0x40000000), Bits(0x00000000), Bits(0x00000000)),
                            fallback=(Bits(0xbf000000), Bits(0x40000000), Bits(0x00000000), Bits(0x00000000))))
    camera = {**stocks['stock_wall0_finite'].camera,
              'parameter_chase_rate': Bits(0x00000000),
              'camera_distance': Bits(0x7fc12345),
              'target_camera_distance': Bits(0xffc54321),
              }
    profiles.append(replace(stocks['stock_wall0_finite'],
                            id='fps_rounded_distance_difference',
                            category='FPS',
                            measured=Bits(0x3c088889),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            camera=camera))
    profiles.append(replace(stocks['stock_wall1_both_miss'],
                            id='fps_reload_measured_first_query',
                            category='FPS',
                            measured=Bits(0x3c088889),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            world='flipper_object',
                            fps_callbacks=((0, Write(0, 0x264, data=raw32(0x3d088889))),)))
    profiles.append(replace(stocks['stock_wall1_both_miss'],
                            id='fps_reload_measured_second_query',
                            category='FPS',
                            measured=Bits(0x3c088889),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            world='flipper_object',
                            fps_callbacks=((0, Write(0, 0x264, data=raw32(0x80000000))), (1, Write(0, 0x264, data=raw32(0x7fc12345))),)))
    profiles.append(replace(stocks['stock_wall1_both_miss'],
                            id='fps_reload_flipper_pointer_slot',
                            category='FPS',
                            measured=Bits(0x3c088889),
                            parameter_available=True,
                            time=(Bits(0x3d000000), Bits(0x3f800000), Bits(0x80000000), Bits(0x7fc33445)),
                            world='flipper_slot',
                            measured_after=Bits(0x3d088889),
                            fps_callbacks=((0, Write(0, 0x0, pointer='flipper_after')),)))
    return tuple(profiles)


ABI = {
    "calling_convention": "sysv_amd64", "confidence": "supported", "stack_alignment": 16,
    "evidence": ["game/camera/follow_camera.h: partial fields and original update declaration",
                 "Original0x0183ac60 instruction/ABI review: RDI/RSI/RDX and scalar XMM0, void",
                 "Scalar tail bridge preserves all 128 supplied physical XMM0 bits"],
    "arguments": [{"register": "rdi", "type": "camera::FollowCamera *", "direction": "inout"},
                  {"register": "rsi", "type": "camera::Subject * or null", "direction": "input"},
                  {"register": "rdx", "type": "camera::QueryWorld * with declared opaque aliases", "direction": "input"},
                  {"register": "xmm0", "type": "float low32; boundary retains opaque physical upper 96", "direction": "input"}],
    "returns": {"type": "void"}, "input_regions": list(BUFFER_LAYOUT),
    "writable_regions": ["camera", "overrides", "renderer", "render_state_a", "render_state_b",
                         "flipper_before", "flipper_after"],
    "execution_limits": {"timeout_seconds": 10},
    "writable_guest_pointer_slots": ["0x05956678", "0x05940340", "0x057e3be0", "0x0597be80",
                                     "0x0593e858", "0x05940298", FLIPPER_SLOT],
    "writable_guest_globals": {WALL_SLOT: "one byte; supplied wall-mode QueryWorld alias only"},
    "scope": "Synthetic full camera inputs; fresh matching original per original/30/60/uncapped policy",
}

# Every preset into image storage is confined to one explicitly sized RW slot.
IMAGE_PRESET_EXTENTS = {0x05956678: 8, 0x05940340: 8, 0x057e3be0: 8,
                        0x0597be80: 16, 0x05527a94: 1, 0x0593e858: 8,
                        0x05940298: 8, 0x059404f8: 8}


def buffer_reference(reference: str, sizes: dict, *, prefix: bool = True) -> tuple[str, int]:
    text = reference[4:] if prefix and reference.startswith("buf:") else reference
    if prefix and not reference.startswith("buf:"):
        raise ValueError("expected a declared buffer reference")
    pieces = text.split("+")
    if len(pieces) > 2 or pieces[0] not in sizes:
        raise ValueError("unknown or malformed buffer reference")
    offset = int(pieces[1], 0) if len(pieces) == 2 else 0
    if offset < 0 or offset >= sizes[pieces[0]]:
        raise ValueError("pointer exceeds declared buffer")
    return pieces[0], offset


def validate_contract(case: dict) -> None:
    """Reject invalid synthetic graph/callback extents before serializing a case."""
    sizes = {name: value["size"] for name, value in case["buffers"].items()}
    if set(sizes) != set(ABI["input_regions"]):
        raise ValueError("every case must declare the complete ABI buffer union")
    for name, definition in case["buffers"].items():
        if definition["alignment"] != 16 or len(bytes.fromhex(definition["bytes"])) != sizes[name]:
            raise ValueError("invalid buffer extent or alignment")
    for item in case["memory"]:
        width = len(bytes.fromhex(item["bytes"])) if "bytes" in item else 8
        address = item["addr"]
        if address.startswith("buf:"):
            name, offset = buffer_reference(address, sizes)
            if offset + width > sizes[name]:
                raise ValueError("preset crosses declared buffer extent")
        elif width != IMAGE_PRESET_EXTENTS.get(int(address, 0)):
            raise ValueError("preset exceeds its explicitly allowed RW image slot")
        if "pointer" in item:
            buffer_reference(item["pointer"], sizes, prefix=False)
    for register in ("rdi", "rsi", "rdx"):
        value = case["args"][register]
        if value.startswith("buf:"):
            buffer_reference(value, sizes)
        elif value != "0x0" and value not in ("guest:" + WALL_SLOT, "guest:" + FLIPPER_SLOT):
            raise ValueError("unknown formal pointer")
    world = case["args"]["rdx"]
    if world.startswith("buf:"):
        world_name, world_offset = buffer_reference(world, sizes)
        world_extent = sizes[world_name] - world_offset
    else:
        world_extent = 1 if world == "guest:" + WALL_SLOT else 8 if world == "guest:" + FLIPPER_SLOT else 0
    for script in case["stubs"]:
        address = int(script["address"], 0)
        if address in NATIVE_HELPERS:
            raise ValueError("original vector helpers must remain unstubbed")
        if address in (FSIN, ATAN, TAN, DIAGNOSTIC):
            if script.get("writes") or script.get("argmem"):
                raise ValueError("scalar/diagnostic callback has no formal object write/read contract")
            continue
        if address not in (SPHERE, VIRTUAL, AXES):
            raise ValueError("undeclared callback")
        if address == SPHERE:
            if script["argc"] != 7 or script["argf32"] != [0]:
                raise ValueError("sphere callback requires seven GP and scalar radius")
            for capture in script["argmem"]:
                arg, offset, width = capture["arg"], capture.get("offset", 0), capture["size"]
                if arg in (2, 3):
                    valid = offset == 0 and width == 16
                else:
                    valid = arg == 0 and offset >= 0 and width > 0 and offset + width <= world_extent
                if not valid:
                    raise ValueError("capture exceeds formal input extent")
        for write in script.get("writes", []):
            arg, offset = write["arg"], write.get("offset", 0)
            width = 8 if "pointer" in write else len(bytes.fromhex(write["bytes"]))
            if "pointer" in write:
                buffer_reference(write["pointer"], sizes, prefix=False)
            valid = False
            if address == SPHERE:
                if arg in (2, 3):
                    valid = offset == 0 and width == 16 and "pointer" not in write
                elif arg in (4, 6):
                    valid = bool(int(script["ret"], 0) & 255) and offset == 0 and width == (16 if arg == 4 else 4)
                elif arg == 0 and world == "buf:camera+0x10":
                    valid = any(offset == field_offset and width == len(field_bytes(name, BASE_CAMERA[name]))
                                for name, (field_offset, _) in CAMERA_FIELDS.items() if name in BASE_CAMERA)
                elif arg == 0 and world == "guest:" + WALL_SLOT:
                    valid = offset == 0 and width == 1 and "pointer" not in write
                elif arg == 0 and world == "buf:flipper_before":
                    valid = offset == FRAME_SECONDS_OFFSET and width == 4 and "pointer" not in write
                elif arg == 0 and world == "guest:" + FLIPPER_SLOT:
                    valid = offset == 0 and width == 8 and write.get("pointer") == "flipper_after"
                if arg == 0:
                    valid = valid and offset >= 0 and offset + width <= world_extent
            elif address == VIRTUAL:
                valid = arg == 0 and offset == 0x40 and width == 8 and write.get("pointer") == "render_state_b"
            elif address == AXES:
                valid = arg == 0 and offset == 0x20 and width == 4 and write.get("bytes") == raw32(1).hex()
            if not valid:
                raise ValueError("callback write violates its formal object contract")


def build_cases() -> list[dict]:
    profiles = (*STOCK_PROFILES, *fps_profiles())
    if len(profiles) != 64 or len({profile.id for profile in profiles}) != 64:
        raise ValueError("the edge suite has exactly 64 distinct named profiles")
    cases = [make_case(profile) for profile in profiles]
    for case in cases:
        validate_contract(case)
    return cases


CASES = build_cases()


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        raise SystemExit("usage: update_cases.py OUTPUT_DIRECTORY")
    output = Path(argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for case in CASES:
        (output / (case["id"] + ".json")).write_text(json.dumps(case, indent=2) + "\n", encoding="utf-8")
    (output / "abi.json").write_text(json.dumps(ABI, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
