#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for hearing.cpp (invented data, not game recordings): whether a sphere
around a sound point reaches the ear.

usage: hearing_cases.py OUT_DIR

Writes one directory per function address. The ear's height angle comes from an invented
NpcThinkParam row (or none); tanf is scripted with the float32 tangent of the angle the
function passes, so a replacement that asks for another angle, or skips or adds the call, fails.
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


GENERATORS = [named_cases, random_cases]


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
