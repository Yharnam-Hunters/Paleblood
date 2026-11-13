#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the camera math helpers (game/camera/math_helpers.cpp), for tools/verify.py run.

usage: math_helpers_cases.py OUT_DIR quaternion|transform

- quaternion: angles at 0 and -0, every octant boundary and both sides of it, a sweep and 300 random angles over
  several turns, large angles (the octant conversion's integer overflow at and past 2^31),
  the largest float, infinities, quiet, signaling and negative NaNs with payloads, denormals;
  axes that are unit, scaled, zero, negative zero, infinite or NaN (with a junk w lane), the
  angle's unused upper xmm0 lanes filled with junk, and the output written over the axis.
- transform: identity and turned quaternions, non-unit and NaN quaternions, scales with 0, -0,
  denormals, infinities and NaN payloads, positions with NaN and junk w lanes, a deterministic
  random set, and the output written over the source.
"""
import json
import math
import os
import random
import struct
import sys

ADDRESSES = {'quaternion': '0x00848e80', 'transform': '0x0083d4d0'}
GUARD_GOT = '0x057e3be0'
JUNK_LANES = struct.pack('<3I', 0x7fa11111, 0xdeadbeef, 0xff812345)

POSITIVE_ZERO, NEGATIVE_ZERO = 0x00000000, 0x80000000
INFINITY, NEGATIVE_INFINITY = 0x7f800000, 0xff800000
QUIET_NAN, NEGATIVE_QUIET_NAN = 0x7fc00000, 0xffc00000
QUIET_NAN_PAYLOAD, SIGNALING_NAN = 0x7fc12345, 0x7f812345
NEGATIVE_SIGNALING_NAN, NEGATIVE_NAN_PAYLOAD = 0xff8abcde, 0xffd54321
SMALLEST_DENORMAL, LARGEST_DENORMAL = 0x00000001, 0x007fffff
NEGATIVE_DENORMAL, LARGEST_FLOAT = 0x80400000, 0x7f7fffff
SPECIAL_WORDS = (POSITIVE_ZERO, NEGATIVE_ZERO, INFINITY, NEGATIVE_INFINITY, QUIET_NAN, NEGATIVE_QUIET_NAN,
                 QUIET_NAN_PAYLOAD, SIGNALING_NAN, NEGATIVE_SIGNALING_NAN, NEGATIVE_NAN_PAYLOAD,
                 SMALLEST_DENORMAL, LARGEST_DENORMAL, NEGATIVE_DENORMAL, LARGEST_FLOAT, LARGEST_FLOAT | 0x80000000)


def word(value):
    """A float's bits, or an int taken as the bits themselves."""
    if isinstance(value, int):
        return struct.pack('<I', value & 0xffffffff)
    return struct.pack('<f', value)


def vec(*lanes):
    return b''.join(word(v) for v in lanes)


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def quaternion_case(cid, angle, axis=(0.0, 1.0, 0.0, 0.0), alias=False):
    c = base('quaternion', cid)
    c['args'] = {'rdi': 'buf:axis' if alias else 'buf:out', 'rsi': 'buf:axis',
                 'xmm0': (word(angle) + JUNK_LANES).hex()}
    c['buffers'] = {'axis': {'size': 0x10, 'bytes': vec(*axis).hex()}}
    if not alias:
        c['buffers']['out'] = {'size': 0x10, 'bytes': 'a5' * 0x10}
    return c


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def quaternion_cases():
    out = []
    angles = {}
    for word_value in SPECIAL_WORDS:
        angles[f'bits_{word_value:08x}'] = word_value
    # Octant boundaries of the half angle: multiples of pi/4, so angle multiples of pi/2.
    for k in range(-17, 18):
        boundary = f32(k * math.pi / 2)
        bits = struct.unpack('<I', struct.pack('<f', boundary))[0]
        angles[f'boundary_{k}'] = boundary
        angles[f'boundary_{k}_below'] = (bits - 1) if boundary > 0 else (bits + 1) if boundary < 0 else NEGATIVE_DENORMAL
        angles[f'boundary_{k}_above'] = (bits + 1) if boundary > 0 else (bits - 1) if boundary < 0 else SMALLEST_DENORMAL
    for i in range(-160, 161):
        angles[f'sweep_{i}'] = i * 0.1
    # |angle/2| * 4/pi around 2^31 (integer overflow of the octant) and beyond.
    for name, value in (('big_1e6', 1.0e6), ('big_1e8', 1.0e8), ('big_3e9', 3.0e9), ('big_3_4e9', 3.4e9),
                        ('big_3_3726e9', 3.3726e9), ('big_1e10', 1.0e10), ('big_1e20', 1.0e20),
                        ('neg_big_1e10', -1.0e10), ('neg_big_3_4e9', -3.4e9), ('tiny_1e-30', 1.0e-30),
                        ('tiny_neg', -1.0e-38)):
        angles[name] = value
    rng = random.Random(0x00848e80)
    for i in range(40):
        angles[f'random_{i}'] = rng.uniform(-1000.0, 1000.0)
    for i in range(300):
        angles[f'random_turn_{i}'] = rng.uniform(-13.0, 13.0)
    for name, angle in angles.items():
        out.append(quaternion_case(f'angle_{name}', angle))

    axes = {'unit_x': (1.0, 0.0, 0.0, 0.0), 'unit_z_w_junk': (0.0, 0.0, 1.0, QUIET_NAN_PAYLOAD),
            'scaled': (3.0, -4.0, 12.5, 7.0), 'zero': (0.0, 0.0, 0.0, 0.0),
            'negative_zero': (NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO),
            'infinite': (INFINITY, NEGATIVE_INFINITY, 1.0, 0.0),
            'nan_payloads': (QUIET_NAN_PAYLOAD, SIGNALING_NAN, NEGATIVE_NAN_PAYLOAD, NEGATIVE_SIGNALING_NAN),
            'denormal': (SMALLEST_DENORMAL, LARGEST_DENORMAL, NEGATIVE_DENORMAL, 0.0),
            'huge': (LARGEST_FLOAT, LARGEST_FLOAT | 0x80000000, 1.0e30, 0.0)}
    for axis_name, axis in axes.items():
        for angle_name, angle in (('zero', 0.0), ('negative_zero', NEGATIVE_ZERO), ('one', 1.0), ('pi', f32(math.pi)),
                                  ('minus_three', -3.0), ('nan', QUIET_NAN_PAYLOAD), ('infinity', INFINITY),
                                  ('negative_nan', NEGATIVE_SIGNALING_NAN)):
            out.append(quaternion_case(f'axis_{axis_name}_angle_{angle_name}', angle, axis))
    out.append(quaternion_case('output_over_axis', 1.25, (0.6, 0.0, 0.8, 5.0), alias=True))
    out.append(quaternion_case('output_over_axis_nan', QUIET_NAN, (QUIET_NAN_PAYLOAD, 1.0, -2.0, 3.0), alias=True))
    return out


def transform_case(cid, position, quaternion, scale, alias=False):
    c = base('transform', cid)
    source = vec(*position) + vec(*quaternion) + vec(*scale)
    c['buffers'] = {'guard': {'size': 0x10, 'bytes': '5a' * 0x10}}
    c['memory'] = [{'addr': GUARD_GOT, 'pointer': 'guard'}]
    if alias:
        c['buffers']['source'] = {'size': 0x40, 'bytes': (source + vec(9.0, 9.0, 9.0, 9.0)).hex()}
        c['args'] = {'rdi': 'buf:source', 'rsi': 'buf:source'}
    else:
        c['buffers']['source'] = {'size': 0x30, 'bytes': source.hex()}
        c['buffers']['out'] = {'size': 0x40, 'bytes': 'a5' * 0x40}
        c['args'] = {'rdi': 'buf:source', 'rsi': 'buf:out'}
    return c


def unit_quaternion(rng):
    lanes = [rng.uniform(-1.0, 1.0) for _ in range(4)]
    norm = math.sqrt(sum(v * v for v in lanes)) or 1.0
    return tuple(v / norm for v in lanes)


def transform_cases():
    out = []
    identity = (0.0, 0.0, 0.0, 1.0)
    half = f32(math.sqrt(0.5))
    position = (10.0, -2.5, 300.0, 1.0)
    unit_scale = (1.0, 1.0, 1.0, 1.0)
    quaternions = {'identity': identity, 'quarter_turn_y': (0.0, half, 0.0, half),
                   'half_turn_x': (1.0, 0.0, 0.0, 0.0), 'mixed': (0.5, -0.5, 0.5, 0.5),
                   'not_unit': (1.0, 2.0, 3.0, 4.0), 'zero': (0.0, 0.0, 0.0, 0.0),
                   'negative_zero': (NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO),
                   'nan_payloads': (QUIET_NAN_PAYLOAD, SIGNALING_NAN, NEGATIVE_NAN_PAYLOAD, 0.5),
                   'nan_w': (0.0, 0.0, 0.0, NEGATIVE_SIGNALING_NAN), 'infinite': (INFINITY, 0.0, 0.0, 1.0),
                   'denormal': (SMALLEST_DENORMAL, LARGEST_DENORMAL, 0.0, 1.0)}
    scales = {'unit': unit_scale, 'uneven': (2.0, 0.5, -3.0, 7.0), 'zero': (0.0, 0.0, 0.0, 0.0),
              'negative_zero': (NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO),
              'infinite_x': (INFINITY, 1.0, 1.0, 1.0), 'infinite_z': (1.0, 1.0, NEGATIVE_INFINITY, 1.0),
              'nan_y': (1.0, QUIET_NAN_PAYLOAD, 1.0, 1.0), 'nan_w_only': (1.0, 1.0, 1.0, SIGNALING_NAN),
              'nan_all': (NEGATIVE_NAN_PAYLOAD, SIGNALING_NAN, QUIET_NAN_PAYLOAD, NEGATIVE_SIGNALING_NAN),
              'denormal': (SMALLEST_DENORMAL, NEGATIVE_DENORMAL, LARGEST_DENORMAL, 0.0),
              'huge': (LARGEST_FLOAT, 1.0e30, -1.0e30, 1.0)}
    for qname, q in quaternions.items():
        for sname, s in scales.items():
            out.append(transform_case(f'quaternion_{qname}_scale_{sname}', position, q, s))
    positions = {'zero_w': (1.0, 2.0, 3.0, 0.0), 'nan_payloads': (QUIET_NAN_PAYLOAD, SIGNALING_NAN, -0.0, 5.0),
                 'infinite': (INFINITY, NEGATIVE_INFINITY, 0.0, NEGATIVE_NAN_PAYLOAD),
                 'negative_zero': (NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO),
                 'denormal': (SMALLEST_DENORMAL, NEGATIVE_DENORMAL, LARGEST_DENORMAL, SMALLEST_DENORMAL)}
    for pname, p in positions.items():
        out.append(transform_case(f'position_{pname}', p, quaternions['mixed'], scales['uneven']))
    rng = random.Random(0x0083d4d0)
    for i in range(40):
        p = tuple(rng.uniform(-1000.0, 1000.0) for _ in range(3)) + (rng.uniform(-2.0, 2.0),)
        s = tuple(rng.uniform(-4.0, 4.0) for _ in range(4))
        out.append(transform_case(f'random_{i}', p, unit_quaternion(rng), s))
    out.append(transform_case('output_over_source', position, quaternions['mixed'], scales['uneven'], alias=True))
    out.append(transform_case('output_over_source_nan', positions['nan_payloads'], quaternions['nan_payloads'],
                              scales['nan_all'], alias=True))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = quaternion_cases() if sys.argv[2] == 'quaternion' else transform_cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
