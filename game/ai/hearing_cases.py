#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for hearing.cpp (invented data, not game recordings): whether a sphere
around a sound point reaches the ear, and the hearing test that asks it.

usage: hearing_cases.py OUT_DIR

Writes one directory per function address. The ear's height angle comes from an invented
NpcThinkParam row (or none); tanf is scripted with the float32 tangent of the angle the
function passes, so a replacement that asks for another angle, or skips or adds the call, fails.

The hearing test's enemy and memory are invented objects whose vtables point at one-instruction
getters in the executable (they run as original code on both sides): the enemy's position,
facing and size, and the memory's remembered flag and position, read from fields the case sets.
The ear's shape runs as original code too, so its tanf call is scripted (once; a run that does
not reach it leaves the answer unused on both sides).
"""
import json
import math
from pathlib import Path
import random
import struct
import sys

IN_EAR_RANGE = '0x0169da30'
ROW_SIZE = 0x60
EAR_ANG_X = 0x53
NAN_A, NAN_B, SNAN = 0x7fc12345, 0xffc54321, 0x7f812345
INF = float('inf')
DEGREES_TO_RADIANS = struct.unpack('<f', struct.pack('<I', 0x3c8efa35))[0]
RIGHT_ANGLE = struct.unpack('<f', struct.pack('<I', 0x3fc90fdb))[0]


def f32(value):
    """A float rounded to float32, or an explicit bit pattern, as 4 bytes."""
    return struct.pack('<I', value & 0xffffffff) if isinstance(value, int) else struct.pack('<f', value)


def as_float(value):
    return struct.unpack('<f', f32(value))[0]


def vec(x, y, z, w=1.0):
    return b''.join(f32(v) for v in (x, y, z, w))


def xmm(value):
    return (f32(value) + bytes(12)).hex()


def pattern(size, byte):
    return bytearray((byte + i * 7) & 0xff for i in range(size))


def tangent_bits(angle):
    """tanf(angle) as float32 bits (the script's answer; the replay compares the argument)."""
    try:
        result = struct.pack('<f', math.tan(angle))
    except (OverflowError, ValueError):
        result = struct.pack('<I', NAN_A)
    return struct.unpack('<I', result)[0]


def asks_tangent(ear, point):
    """Whether the call reaches tanf: not when the point is closer than 0.0001 to the ear (NaN
    and infinite distances do reach it). Cases keep away from the 0.0001 boundary."""
    offsets = [as_float(p) - as_float(e) for p, e in zip(point[:3], ear[:3])]
    if any(math.isnan(o) for o in offsets):
        return True
    try:
        distance = math.sqrt(sum(o * o for o in offsets))
    except OverflowError:
        return True
    return not distance < 0.0001


def case(cid, ear, point, reach, radius, angle_degrees=30, w_ear=1.0, w_point=1.0, tangent=None):
    """One question. angle_degrees None: no row (the ear's angle is a right angle). tangent: the
    scripted tanf answer as float32 bits, instead of the angle's tangent."""
    listener, holder = pattern(0x18, 0x31), pattern(0x10, 0x37)
    buffers = {'listener': listener, 'holder': holder, 'ear': bytearray(vec(*ear, w_ear))}
    memory = [{'addr': 'buf:listener+16', 'pointer': 'holder'}]
    if angle_degrees is None:
        holder[8:16] = bytes(8)
        angle = RIGHT_ANGLE
    else:
        row = pattern(ROW_SIZE, 0x3b)
        row[EAR_ANG_X] = angle_degrees
        buffers['row'] = row
        memory.append({'addr': 'buf:holder+8', 'pointer': 'row'})
        angle = as_float(as_float(float(angle_degrees)) * DEGREES_TO_RADIANS)
    point_bytes = vec(*point, w_point)
    imports = []
    if asks_tangent(ear, point):
        answer = tangent_bits(angle) if tangent is None else tangent
        imports.append({'name': 'tanf', 'argc': 0, 'argf32': [0], 'ret': hex(answer)})
    return {
        'schema': 1, 'address': IN_EAR_RANGE, 'id': cid, 'returns': 'i8',
        'args': {'rdi': 'buf:listener', 'rsi': 'buf:ear', 'xmm0': xmm(reach), 'xmm1': point_bytes.hex(),
                 'xmm2': xmm(radius) if not isinstance(radius, bytes) else radius.hex()},
        'buffers': {n: {'size': len(b), 'bytes': bytes(b).hex()} for n, b in buffers.items()},
        'memory': memory, 'imports': imports, 'stubs': [],
    }


ORIGIN = (0.0, 0.0, 0.0)
EAR = (10.0, 2.0, -5.0)


def at(dx, dy, dz, ear=EAR):
    return (ear[0] + dx, ear[1] + dy, ear[2] + dz)


def named_cases():
    # At the ear: heard without asking the row or tanf.
    yield case('at_the_ear', EAR, EAR, 1.0, 0.0)
    yield case('almost_at_the_ear', EAR, at(0.00004, 0.0, 0.00003), 0.0, 0.0)
    yield case('at_the_ear_no_reach', ORIGIN, ORIGIN, -5.0, NAN_A)
    yield case('beside_the_ear', EAR, at(0.0003, 0.0, 0.0004), 0.0, 0.0)
    # Within the height angle: reach plus radius against the distance.
    yield case('in_angle_in_reach', EAR, at(3.0, 0.5, 4.0), 4.0, 1.5)
    yield case('in_angle_out_of_reach', EAR, at(3.0, 0.5, 4.0), 3.0, 1.0)
    yield case('in_angle_exact_sum', ORIGIN, (3.0, 0.0, 4.0), 4.0, 1.0)
    yield case('in_angle_below', EAR, at(-3.0, -0.5, 4.0), 4.5, 1.0)
    yield case('in_angle_on_edge', ORIGIN, (1.0, 1.0, 0.0), 2.0, 0.0, angle_degrees=45)
    yield case('no_row_right_angle', EAR, at(0.5, 30.0, 0.5), 40.0, 0.0, angle_degrees=None)
    yield case('no_row_far', EAR, at(0.5, 30.0, 0.5), 10.0, 1.0, angle_degrees=None)
    yield case('flat_angle_level_point', ORIGIN, (6.0, 0.0, 0.0), 5.0, 2.0, angle_degrees=0)
    yield case('full_byte_angle', ORIGIN, (1.0, 7.0, 1.0), 9.0, 0.5, angle_degrees=255)
    # Beyond the angle: the sphere against the edge segment.
    yield case('above_crosses_edge', ORIGIN, (4.0, 5.0, 0.0), 6.0, 3.0)
    yield case('above_misses_edge', ORIGIN, (4.0, 9.0, 0.0), 6.0, 1.0)
    yield case('below_crosses_edge', ORIGIN, (0.0, -5.0, 4.0), 6.0, 3.0)
    yield case('below_misses_edge', ORIGIN, (0.0, -9.0, 4.0), 6.0, 1.0)
    yield case('sphere_holds_segment', ORIGIN, (1.0, 3.0, 0.0), 2.0, 10.0)
    # Straight above with a tangent of exactly 1: the far end lies exactly on the sphere.
    yield case('far_end_on_sphere', ORIGIN, (0.0, 4.0, 0.0), 10.0, 6.0, tangent=0x3f800000)
    yield case('far_end_inside_sphere', ORIGIN, (0.0, 4.0, 0.0), 10.0, 6.5, tangent=0x3f800000)
    yield case('listener_inside_far_end_out', ORIGIN, (1.0, 3.0, 0.0), 8.0, 3.2)
    yield case('past_the_far_end', ORIGIN, (30.0, 40.0, 0.0), 2.0, 49.0)
    yield case('past_the_far_end_short', ORIGIN, (30.0, 40.0, 0.0), 2.0, 45.0)
    yield case('negative_reach_behind', ORIGIN, (4.0, 5.0, 0.0), -6.0, 3.0)
    yield case('negative_reach_inside', ORIGIN, (1.0, 3.0, 0.0), -2.0, 3.5)
    yield case('straight_above', EAR, at(0.0, 5.0, 0.0), 6.0, 1.5)
    yield case('straight_below_no_row', EAR, at(0.0, -5.0, 0.0), 6.0, 1.5, angle_degrees=None)
    yield case('nearly_straight_above', ORIGIN, (0.000003, 5.0, 0.0), 6.0, 2.0)
    yield case('zero_radius_beyond', ORIGIN, (4.0, 5.0, 0.0), 6.0, 0.0)
    yield case('negative_radius_beyond', ORIGIN, (4.0, 5.0, 0.0), 6.0, -3.0)
    yield case('negative_radius_in_angle', EAR, at(3.0, 0.5, 4.0), 9.0, -1.0)
    # Signed zeros, subnormals, infinities and NaNs.
    yield case('negative_zero_offsets', (-0.0, -0.0, -0.0), (0.5, -0.0, -0.0), 1.0, 0.0)
    yield case('subnormal_height', ORIGIN, (3.0, 0x00000001, 4.0), 5.0, 1.0)
    yield case('subnormal_ground', ORIGIN, (0x00000001, 5.0, 0x80000001), 6.0, 1.0)
    yield case('infinite_point_x', ORIGIN, (INF, 1.0, 0.0), 5.0, 1.0)
    yield case('infinite_point_y', ORIGIN, (1.0, -INF, 0.0), 5.0, 1.0)
    yield case('infinite_reach', ORIGIN, (3.0, 0.5, 4.0), INF, 0.0)
    yield case('infinite_reach_beyond', ORIGIN, (4.0, 5.0, 0.0), INF, 3.0)
    yield case('infinite_radius', ORIGIN, (4.0, 5.0, 0.0), 6.0, INF)
    yield case('nan_point_x', ORIGIN, (NAN_A, 1.0, 0.0), 5.0, 1.0)
    yield case('nan_point_y', ORIGIN, (1.0, NAN_B, 1.0), 5.0, 1.0)
    yield case('signaling_nan_point_z', ORIGIN, (1.0, 1.0, SNAN), 5.0, 1.0)
    yield case('nan_reach', ORIGIN, (3.0, 0.5, 4.0), NAN_A, 1.0)
    yield case('nan_reach_beyond', ORIGIN, (4.0, 5.0, 0.0), NAN_B, 3.0)
    yield case('nan_radius', ORIGIN, (3.0, 0.5, 4.0), 9.0, NAN_A)
    yield case('nan_radius_beyond', ORIGIN, (4.0, 5.0, 0.0), 6.0, NAN_B)
    yield case('huge_offsets', ORIGIN, (3e19, 4e19, 0.0), 1e19, 1e19)
    # W lanes and the radius register's upper lanes are not part of the question.
    yield case('w_lanes_unused', EAR, at(4.0, 5.0, 0.0), 6.0, 3.0, w_ear=NAN_A, w_point=-7.0)
    yield case('radius_upper_lanes', ORIGIN, (4.0, 5.0, 0.0), 6.0,
               f32(3.0) + f32(NAN_A) + f32(-1.0) + f32(9.0))


def random_cases(count=400, seed=0x6ea2):
    rng = random.Random(seed)
    for n in range(count):
        ear = tuple(rng.uniform(-50.0, 50.0) for _ in range(3))
        scale = rng.choice([0.5, 3.0, 12.0, 40.0])
        point = tuple(e + rng.uniform(-scale, scale) for e in ear)
        if rng.random() < 0.3:   # mostly straight up or down
            point = (ear[0] + rng.uniform(-0.3, 0.3), point[1] * 2.0 - ear[1], ear[2] + rng.uniform(-0.3, 0.3))
        reach = rng.choice([rng.uniform(0.0, 2.0 * scale), rng.uniform(-scale, 0.0), 0.0])
        radius = rng.choice([rng.uniform(0.0, scale), 0.0, rng.uniform(-1.0, 0.0)])
        angle = rng.choice([None, 0, 15, 30, 45, 60, 89, 90, 120, rng.randrange(256)])
        yield case(f'random_{n:03d}', ear, point, reach, radius, angle_degrees=angle)



# ---- the hearing test ----

HEARING_TEST = '0x0169d160'
EAR_DIST, EAR_SOUNDCUT_DIST, EAR_ANG_Y = 0x32, 0x34, 0x54
# One-instruction getters in the executable: vmovaps xmm0, [rdi+0x10] / [rdi+0x20] / [rdi+0x70];
# vmovss xmm0, [rdi+0x30]; movzx eax, byte [rdi+0x8].
GET_VECTOR_0x10, GET_VECTOR_0x20, GET_VECTOR_0x70 = '0x016a2750', '0x016a2910', '0x00959190'
GET_FLOAT_0x30, GET_BYTE_0x08 = '0x00acad20', '0x01d61a60'


def hearing_case(cid, ear, facing, sound, sound_range, row=None, owner_size=0.0, source_size=0.0,
                 hearing_scale=1.0, audible=1, remembers=0, remembered=(0.0, 0.0, 0.0), sound_w=1.0,
                 size_register=None):
    """One sound. row: None (no NpcThinkParam row) or (ear_dist, ear_soundcut_dist, ear_ang_x,
    ear_ang_y)."""
    listener, holder = pattern(0x28, 0x51), pattern(0x10, 0x57)
    owner, owner_vt = pattern(0x40, 0x61), pattern(0x48, 0x67)
    memory, memory_vt = pattern(0x80, 0x71), pattern(0x88, 0x77)
    sound_buffer = pattern(0x40, 0x81)
    owner[0x10:0x20] = vec(*ear)
    owner[0x20:0x30] = vec(*facing)
    owner[0x30:0x34] = f32(owner_size)
    memory[0x8] = remembers
    memory[0x70:0x80] = vec(*remembered)
    sound_buffer[0x10:0x20] = vec(*sound, sound_w)
    sound_buffer[0x28:0x2c] = f32(sound_range)
    sound_buffer[0x30:0x34] = struct.pack('<i', 4100 + len(cid))
    listener[0x20:0x24] = f32(hearing_scale)
    buffers = {'listener': listener, 'holder': holder, 'owner': owner, 'owner_vt': owner_vt,
               'memory': memory, 'memory_vt': memory_vt, 'sound': sound_buffer}
    memory_entries = [
        {'addr': 'buf:listener+8', 'pointer': 'owner'}, {'addr': 'buf:listener+16', 'pointer': 'holder'},
        {'addr': 'buf:listener+24', 'pointer': 'memory'}, {'addr': 'buf:owner+0', 'pointer': 'owner_vt'},
        {'addr': 'buf:memory+0', 'pointer': 'memory_vt'},
        {'addr': 'buf:owner_vt+24', 'guest': GET_VECTOR_0x10}, {'addr': 'buf:owner_vt+40', 'guest': GET_VECTOR_0x20},
        {'addr': 'buf:owner_vt+64', 'guest': GET_FLOAT_0x30},
        {'addr': 'buf:memory_vt+40', 'guest': GET_BYTE_0x08}, {'addr': 'buf:memory_vt+128', 'guest': GET_VECTOR_0x70},
    ]
    if row is None:
        holder[8:16] = bytes(8)
        angle = RIGHT_ANGLE
    else:
        ear_dist, soundcut, ang_x, ang_y = row
        data = pattern(ROW_SIZE, 0x3b)
        data[EAR_DIST:EAR_DIST + 2] = struct.pack('<H', ear_dist)
        data[EAR_SOUNDCUT_DIST:EAR_SOUNDCUT_DIST + 2] = struct.pack('<H', soundcut)
        data[EAR_ANG_X], data[EAR_ANG_Y] = ang_x, ang_y
        buffers['row'] = data
        memory_entries.append({'addr': 'buf:holder+8', 'pointer': 'row'})
        angle = as_float(as_float(float(ang_x)) * DEGREES_TO_RADIANS)
    size = size_register if size_register is not None else f32(source_size)
    return {
        'schema': 1, 'address': HEARING_TEST, 'id': cid, 'returns': 'void',
        'args': {'rdi': 'buf:listener', 'rsi': 'buf:sound', 'rdx': audible, 'xmm0': (size + bytes(16))[:16].hex()},
        'buffers': {n: {'size': len(b), 'bytes': bytes(b).hex()} for n, b in buffers.items()},
        'memory': memory_entries,
        'imports': [{'name': 'tanf', 'argc': 0, 'argf32': [0], 'ret': hex(tangent_bits(angle))}],
        'stubs': [],
    }


AHEAD = (0.0, 0.0, 1.0)
NARROW = (300, 1, 60, 45)        # reach 3 per hearing scale, 1 m cut, 60 degrees high, 45 each side
ALL_AROUND = (300, 1, 90, 180)


def hearing_named_cases():
    h = hearing_case
    # Not audible, or no range left: nothing is read past the row, nothing is written.
    yield h('not_audible', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 10.0, audible=0)
    yield h('not_audible_upper_bits', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 10.0, audible=0x100)
    yield h('audible_upper_bits', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 10.0, audible=0x201)
    yield h('range_all_cut', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 5.0, row=(300, 5, 60, 45))
    yield h('range_below_cut', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 4.0, row=(300, 5, 60, 45))
    yield h('range_negative_no_row', ORIGIN, AHEAD, (0.0, 0.0, 2.0), -1.0)
    yield h('range_zero_no_row', ORIGIN, AHEAD, (0.0, 0.0, 2.0), -0.0)
    yield h('range_nan', ORIGIN, AHEAD, (0.0, 0.0, 2.0), NAN_A, row=NARROW)
    yield h('range_infinite', ORIGIN, AHEAD, (0.0, 50.0, -900.0), INF, row=NARROW)
    # The range covers the gap: heard directly.
    yield h('direct_no_row', EAR, AHEAD, at(1.0, 0.0, 2.0), 5.0)
    yield h('direct_with_sizes', ORIGIN, AHEAD, (0.0, 0.0, 9.0), 5.0, owner_size=2.0, source_size=2.5)
    yield h('direct_exact_gap', ORIGIN, AHEAD, (0.0, 0.0, 9.0), 5.0, owner_size=2.0, source_size=2.0)
    yield h('direct_behind_narrow', ORIGIN, AHEAD, (0.0, 0.0, -3.0), 6.0, row=NARROW)
    yield h('direct_negative_sizes', ORIGIN, AHEAD, (0.0, 0.0, 3.0), 6.0, owner_size=-1.0, source_size=-1.5)
    # Remembering: a closer remembered position keeps its place; the id is always written.
    yield h('remember_replaces_farther', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 5.0, remembers=1, remembered=(0.0, 0.0, 8.0))
    yield h('remember_keeps_closer', ORIGIN, AHEAD, (0.0, 0.0, 4.0), 5.0, remembers=1, remembered=(0.0, 0.0, 1.0))
    yield h('remember_equal_keeps', ORIGIN, AHEAD, (0.0, 0.0, 4.0), 5.0, remembers=1, remembered=(4.0, 0.0, 0.0))
    yield h('remember_with_sizes', ORIGIN, AHEAD, (0.0, 0.0, 6.0), 5.0, owner_size=1.0, source_size=1.0,
            remembers=1, remembered=(0.0, 0.0, 4.5))
    yield h('remember_nan_previous', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 5.0, remembers=1, remembered=(NAN_A, 0.0, 0.0))
    yield h('remember_byte_flag', ORIGIN, AHEAD, (0.0, 0.0, 2.0), 5.0, remembers=0x80, remembered=(0.0, 0.0, 9.0))
    # No row, out of range: the ear has no reach.
    yield h('no_row_out_of_range', ORIGIN, AHEAD, (0.0, 0.0, 9.0), 5.0)
    yield h('no_row_infinite_scale', ORIGIN, AHEAD, (0.0, 0.0, 9.0), 5.0, hearing_scale=INF)
    # All around and at every height: reach plus range against the gap.
    yield h('sphere_reached', ORIGIN, AHEAD, (0.0, 0.0, -8.5), 7.0, row=ALL_AROUND)
    yield h('sphere_exact_sum', ORIGIN, AHEAD, (0.0, 0.0, -9.0), 7.0, row=ALL_AROUND)
    yield h('sphere_missed', ORIGIN, AHEAD, (0.0, 0.0, -11.0), 7.0, row=ALL_AROUND)
    yield h('sphere_scaled', ORIGIN, AHEAD, (0.0, 0.0, -11.0), 7.0, row=ALL_AROUND, hearing_scale=2.0)
    yield h('sphere_full_bytes', ORIGIN, AHEAD, (0.0, 30.0, -11.0), 7.0, row=(65535, 0, 255, 255))
    # All around but not at every height: the ear's shape.
    yield h('all_around_low_ear_level', ORIGIN, AHEAD, (0.0, 0.0, -8.5), 7.0, row=(300, 1, 30, 180))
    yield h('all_around_low_ear_above', ORIGIN, AHEAD, (1.0, 9.0, -1.0), 7.0, row=(300, 1, 30, 200))
    yield h('all_around_flat_ear', ORIGIN, AHEAD, (0.0, 4.0, -9.0), 9.0, row=(500, 0, 0, 180))
    # A narrower ear: ahead within the width angle goes to the ear's shape.
    yield h('ahead_in_reach', ORIGIN, AHEAD, (0.5, 0.0, 8.0), 7.0, row=NARROW)
    yield h('ahead_out_of_reach', ORIGIN, AHEAD, (0.5, 0.0, 19.0), 7.0, row=NARROW)
    yield h('ahead_on_width_edge', ORIGIN, AHEAD, (9.0, 0.0, 9.0), 7.0, row=NARROW)
    yield h('ahead_above', ORIGIN, AHEAD, (0.0, 8.0, 6.0), 7.0, row=NARROW)
    yield h('ahead_turned_facing', EAR, (1.0, 0.0, 0.0), at(9.0, 0.0, 1.0), 7.0, row=NARROW)
    yield h('ahead_zero_width', ORIGIN, AHEAD, (0.0, 0.0, 9.0), 7.0, row=(300, 1, 60, 0))
    # Exactly on the width cosine (1, a zero width), above the ground: inside, the sound's own point is
    # asked about; the edge path would ask about a point with the ear's height added.
    yield h('ahead_exact_width_cosine', (0.0, 3.0, 0.0), AHEAD, (0.0, 3.0, 9.0), 7.2, row=(300, 1, 60, 0))
    # The width test divides by the gap, not the distance: with both sizes, a sound outside the
    # angle by direction counts as inside.
    yield h('cone_by_gap_sizes', ORIGIN, AHEAD, (8.0, 0.0, 6.0), 5.0, row=(610, 1, 60, 45), owner_size=2.0,
            source_size=2.0)
    # Outside the width angle: near the edge on the sound's side.
    # Off the edge line by exactly the remaining range: still asked about, with no range left.
    yield h('edge_exact_range', ORIGIN, AHEAD, (3.0, 0.0, 4.0), 4.0, row=(500, 1, 60, 0))
    yield h('beside_left_heard', ORIGIN, AHEAD, (8.0, 0.0, 2.0), 7.0, row=NARROW)
    yield h('beside_right_heard', ORIGIN, AHEAD, (-8.0, 0.0, 2.0), 7.0, row=NARROW)
    yield h('beside_far_from_edge', ORIGIN, AHEAD, (9.0, 0.0, -6.0), 4.0, row=NARROW)
    yield h('behind_far', ORIGIN, AHEAD, (0.0, 0.0, -9.0), 7.0, row=NARROW)
    yield h('behind_close_edge', ORIGIN, AHEAD, (1.0, 0.0, -8.5), 8.0, row=(300, 1, 60, 170))
    yield h('beside_height_added', (0.0, 3.0, 0.0), AHEAD, (8.0, 5.0, 2.0), 7.0, row=NARROW)
    yield h('beside_facing_up', ORIGIN, (0.0, 1.0, 0.0), (8.0, 0.0, 2.0), 7.0, row=NARROW)
    yield h('beside_facing_zero', ORIGIN, (0.0, 0.0, 0.0), (8.0, 0.0, 2.0), 7.0, row=NARROW)
    yield h('beside_facing_long', ORIGIN, (0.0, 0.0, 40.0), (8.0, 0.0, 2.0), 7.0, row=NARROW)
    yield h('beside_facing_infinite', ORIGIN, (0.0, 0.0, INF), (8.0, 0.0, 2.0), 7.0, row=NARROW)
    yield h('beside_facing_nan', ORIGIN, (NAN_B, 0.0, 1.0), (8.0, 0.0, 2.0), 7.0, row=NARROW)
    yield h('straight_above_narrow', ORIGIN, AHEAD, (0.0, 9.0, 0.0), 7.0, row=NARROW)
    yield h('gap_zero_sound', ORIGIN, AHEAD, (3.0, 0.0, 0.0), 2.0, row=NARROW, owner_size=3.0)
    yield h('gap_negative', ORIGIN, AHEAD, (3.0, 0.0, -1.0), 1.0, row=NARROW, owner_size=1.0, source_size=1.0)
    # Infinities, NaNs and register lanes that are not part of the question.
    yield h('sound_nan_x', ORIGIN, AHEAD, (NAN_A, 0.0, 9.0), 7.0, row=NARROW)
    yield h('sound_infinite_z', ORIGIN, AHEAD, (0.0, 0.0, INF), 7.0, row=NARROW)
    yield h('sound_signaling_nan_y', ORIGIN, AHEAD, (8.0, SNAN, 2.0), 7.0, row=NARROW)
    yield h('owner_size_nan', ORIGIN, AHEAD, (8.0, 0.0, 2.0), 7.0, row=NARROW, owner_size=NAN_B)
    yield h('source_size_infinite', ORIGIN, AHEAD, (8.0, 0.0, 2.0), 7.0, row=NARROW, source_size=-INF)
    yield h('hearing_scale_nan', ORIGIN, AHEAD, (8.0, 0.0, 2.0), 7.0, row=NARROW, hearing_scale=NAN_A)
    yield h('sound_w_unused', ORIGIN, AHEAD, (8.0, 0.0, 2.0), 7.0, row=NARROW, sound_w=NAN_A)
    yield h('size_register_upper_lanes', ORIGIN, AHEAD, (0.0, 0.0, 6.0), 6.0, remembers=1, remembered=(0.0, 0.0, 9.0),
            size_register=f32(0.5) + f32(NAN_A) + f32(-3.0) + f32(7.0))


def hearing_random_cases(count=500, seed=0x1d160):
    rng = random.Random(seed)
    for n in range(count):
        ear = tuple(rng.uniform(-40.0, 40.0) for _ in range(3))
        heading = rng.uniform(-math.pi, math.pi)
        facing = (math.sin(heading) * rng.choice([1.0, 0.3, 5.0]), rng.choice([0.0, 0.0, rng.uniform(-1.0, 1.0)]),
                  math.cos(heading))
        scale = rng.choice([2.0, 6.0, 15.0, 40.0])
        sound = tuple(e + rng.uniform(-scale, scale) for e in ear)
        sound_range = rng.choice([rng.uniform(0.0, scale), rng.uniform(scale * 0.3, scale * 1.2), rng.uniform(-2.0, 2.0)])
        row = rng.choice([None, (rng.randrange(0, 2000), rng.randrange(0, 4), rng.randrange(0, 120),
                                 rng.randrange(0, 200)),
                          (rng.randrange(0, 2000), 0, rng.choice([30, 60, 90]), rng.choice([30, 60, 90, 180]))])
        yield hearing_case(f'random_{n:03d}', ear, facing, sound, sound_range, row=row,
                           owner_size=rng.choice([0.0, rng.uniform(0.0, 2.0)]),
                           source_size=rng.choice([0.0, rng.uniform(0.0, 2.0)]),
                           hearing_scale=rng.choice([1.0, rng.uniform(0.0, 3.0)]),
                           remembers=rng.choice([0, 1]),
                           remembered=tuple(e + rng.uniform(-scale, scale) for e in ear))


GENERATORS = [named_cases, random_cases, hearing_named_cases, hearing_random_cases]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    count = 0
    for generate in GENERATORS:
        for c in generate():
            directory = out / c['address']
            directory.mkdir(parents=True, exist_ok=True)
            (directory / f"{c['id']}.json").write_text(json.dumps(c, indent=1) + '\n')
            count += 1
    print(f'{count} cases')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
