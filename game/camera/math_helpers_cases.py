#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the camera math helpers (game/camera/math_helpers.cpp), for tools/verify.py run.

usage: math_helpers_cases.py OUT_DIR quaternion|transform|rotation_rows|rows3_multiply|affine_finish

- quaternion: angles at 0 and -0, every octant boundary and both sides of it, a sweep and 300 random angles over
  several turns, large angles (the octant conversion's integer overflow at and past 2^31),
  the largest float, infinities, quiet, signaling and negative NaNs with payloads, denormals;
  axes that are unit, scaled, zero, negative zero, infinite or NaN (with a junk w lane), the
  angle's unused upper xmm0 lanes filled with junk, and the output written over the axis.
- transform: identity and turned quaternions, non-unit and NaN quaternions, scales with 0, -0,
  denormals, infinities and NaN payloads, positions with NaN and junk w lanes, a deterministic
  random set, and the output written over the source.
- rotation_rows, rows3_multiply, affine_finish: the three helpers the transform calls, on their
  own: unit, turned, non-unit, zero, infinite, denormal and NaN-payload inputs (NaNs in both
  operands of a lane, to check which payload wins), deterministic random sets, and the output
  written over each input.
"""
import json
import math
import os
import random
import struct
import sys

ADDRESSES = {'quaternion': '0x00848e80', 'transform': '0x0083d4d0', 'rotation_rows': '0x00829b00',
             'rows3_multiply': '0x0084b9d0', 'affine_finish': '0x0083b7e0'}
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


SPECIAL_QUATERNIONS = {
    'identity': (0.0, 0.0, 0.0, 1.0), 'half_turn_x': (1.0, 0.0, 0.0, 0.0), 'mixed': (0.5, -0.5, 0.5, 0.5),
    'not_unit': (1.0, 2.0, 3.0, 4.0), 'zero': (0.0, 0.0, 0.0, 0.0),
    'negative_zero': (NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO, NEGATIVE_ZERO),
    'nan_x': (QUIET_NAN_PAYLOAD, 0.5, 0.5, 0.5), 'nan_y': (0.5, SIGNALING_NAN, 0.5, 0.5),
    'nan_z': (0.5, 0.5, NEGATIVE_NAN_PAYLOAD, 0.5), 'nan_w': (0.0, 0.0, 0.0, NEGATIVE_SIGNALING_NAN),
    'nan_all': (QUIET_NAN_PAYLOAD, SIGNALING_NAN, NEGATIVE_NAN_PAYLOAD, NEGATIVE_SIGNALING_NAN),
    'nan_xy': (NEGATIVE_QUIET_NAN, QUIET_NAN_PAYLOAD, 1.0, 0.0), 'nan_zw': (1.0, 0.0, SIGNALING_NAN, QUIET_NAN),
    'infinite': (INFINITY, NEGATIVE_INFINITY, 1.0, 0.0), 'infinite_w': (0.0, 0.0, 0.0, INFINITY),
    'denormal': (SMALLEST_DENORMAL, LARGEST_DENORMAL, NEGATIVE_DENORMAL, 1.0),
    'huge': (LARGEST_FLOAT, 1.0e30, -1.0e30, 1.0),
    # Signed zeros in the products, so a lane added to 0 where the original keeps it shows.
    'signed_zeros_a': (0.0, -1.0, 0.0, -1.0), 'signed_zeros_b': (NEGATIVE_ZERO, 0.5, NEGATIVE_ZERO, -0.5),
    'signed_zeros_c': (0.0, 0.0, -1.0, NEGATIVE_ZERO), 'signed_zeros_d': (-1.0, 0.0, 0.0, NEGATIVE_ZERO)}


def shifted(c, buffer, offset, size):
    """The output written inside an input, `offset` bytes in (the buffer grown to `size`)."""
    c['buffers'][buffer]['size'] = size
    c['args']['rsi' if c['address'] == ADDRESSES['affine_finish'] else 'rdi'] = f'buf:{buffer}+{offset:#x}'
    del c['buffers']['out']
    return c


def rows_case(variant, cid, inputs, alias=None):
    """inputs: name -> bytes, in argument order after the output; alias: the input the output overwrites."""
    c = base(variant, cid)
    regs = ['rsi', 'rdx'] if variant != 'affine_finish' else ['rdi']
    out_size = 0x40 if variant == 'affine_finish' else 0x30
    for name, data in inputs.items():
        c['buffers'][name] = {'size': max(len(data), out_size), 'bytes': data.hex()}
    out = alias or 'out'
    if not alias:
        c['buffers']['out'] = {'size': out_size, 'bytes': 'a5' * out_size}
    for reg, name in zip(regs, inputs):
        c['args'][reg] = f'buf:{name}'
    c['args']['rsi' if variant == 'affine_finish' else 'rdi'] = f'buf:{out}'
    return c


def rotation_rows_cases():
    out = []
    for name, q in SPECIAL_QUATERNIONS.items():
        out.append(rows_case('rotation_rows', f'quaternion_{name}', {'quaternion': vec(*q)}))
    rng = random.Random(0x00829b00)
    for i in range(60):
        out.append(rows_case('rotation_rows', f'random_unit_{i}', {'quaternion': vec(*unit_quaternion(rng))}))
    for i in range(20):
        q = tuple(rng.uniform(-50.0, 50.0) for _ in range(4))
        out.append(rows_case('rotation_rows', f'random_wide_{i}', {'quaternion': vec(*q)}))
    out.append(rows_case('rotation_rows', 'output_over_quaternion', {'quaternion': vec(0.5, -0.5, 0.5, 0.5)},
                         alias='quaternion'))
    out.append(rows_case('rotation_rows', 'output_over_quaternion_nan',
                         {'quaternion': vec(QUIET_NAN_PAYLOAD, 1.0, SIGNALING_NAN, 0.25)}, alias='quaternion'))
    return out


def rows(*vectors):
    return b''.join(vec(*v) for v in vectors)


def rows3_multiply_cases():
    out = []
    identity = rows((1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 1.0, 0.0))
    turned = rows((0.0, 0.0, -1.0, 0.5), (0.0, 1.0, 0.0, -0.25), (1.0, 0.0, 0.0, 2.0))
    scaled = rows((2.0, 0.0, 0.0, 0.0), (0.0, -3.0, 0.0, 0.0), (0.0, 0.0, 0.5, 7.0))
    nan_left = rows((QUIET_NAN_PAYLOAD, 1.0, 2.0, SIGNALING_NAN), (NEGATIVE_NAN_PAYLOAD, 3.0, QUIET_NAN, 4.0),
                    (5.0, NEGATIVE_SIGNALING_NAN, 6.0, 7.0))
    nan_right = rows((SIGNALING_NAN, 1.0, NEGATIVE_QUIET_NAN, 0.0), (2.0, QUIET_NAN_PAYLOAD, 3.0, 0.0),
                     (NEGATIVE_NAN_PAYLOAD, NEGATIVE_SIGNALING_NAN, 4.0, 9.0))
    special = rows((INFINITY, NEGATIVE_ZERO, SMALLEST_DENORMAL, LARGEST_FLOAT), (0.0, NEGATIVE_INFINITY, 1.0, -1.0),
                   (LARGEST_DENORMAL, 1.0e30, NEGATIVE_DENORMAL, 0.0))
    pairs = {'identity_identity': (identity, identity), 'turned_scaled': (turned, scaled),
             'scaled_turned': (scaled, turned), 'nan_left': (nan_left, turned), 'nan_right': (turned, nan_right),
             'nan_both': (nan_left, nan_right), 'special_left': (special, turned), 'special_right': (scaled, special),
             'special_both': (special, special), 'zero_infinite': (rows((0.0,) * 4, (0.0,) * 4, (0.0,) * 4), special)}
    for name, (left, right) in pairs.items():
        out.append(rows_case('rows3_multiply', name, {'left': left, 'right': right}))
    rng = random.Random(0x0084b9d0)
    for i in range(40):
        left = b''.join(vec(*(rng.uniform(-100.0, 100.0) for _ in range(4))) for _ in range(3))
        right = b''.join(vec(*(rng.uniform(-100.0, 100.0) for _ in range(4))) for _ in range(3))
        out.append(rows_case('rows3_multiply', f'random_{i}', {'left': left, 'right': right}))
    out.append(rows_case('rows3_multiply', 'output_over_left', {'left': turned, 'right': scaled}, alias='left'))
    out.append(rows_case('rows3_multiply', 'output_over_right', {'left': turned, 'right': scaled}, alias='right'))
    out.append(rows_case('rows3_multiply', 'output_over_left_nan', {'left': nan_left, 'right': nan_right},
                         alias='left'))
    out.append(rows_case('rows3_multiply', 'output_over_right_nan', {'left': nan_left, 'right': nan_right},
                         alias='right'))
    for offset in (0x10, 0x20):
        out.append(shifted(rows_case('rows3_multiply', f'output_inside_right_{offset:x}',
                                     {'left': turned, 'right': scaled}), 'right', offset, 0x30 + offset))
        out.append(shifted(rows_case('rows3_multiply', f'output_inside_left_{offset:x}',
                                     {'left': turned, 'right': scaled}), 'left', offset, 0x30 + offset))
    return out


def affine_finish_cases():
    out = []
    matrices = {
        'ordinary': rows((1.0, 0.0, 0.0, 5.0), (0.0, 1.0, 0.0, 6.0), (0.0, 0.0, 1.0, 7.0), (10.0, 20.0, 30.0, 8.0)),
        'w_already_set': rows((1.0, 2.0, 3.0, 0.0), (4.0, 5.0, 6.0, 0.0), (7.0, 8.0, 9.0, 0.0), (1.0, 1.0, 1.0, 1.0)),
        'nan_w': rows((1.0, 2.0, 3.0, QUIET_NAN_PAYLOAD), (4.0, 5.0, 6.0, SIGNALING_NAN),
                      (7.0, 8.0, 9.0, NEGATIVE_NAN_PAYLOAD), (1.0, 1.0, 1.0, NEGATIVE_SIGNALING_NAN)),
        'nan_xyz': rows((QUIET_NAN_PAYLOAD, SIGNALING_NAN, NEGATIVE_QUIET_NAN, 1.0),
                        (NEGATIVE_SIGNALING_NAN, 0.0, 1.0, 2.0), (INFINITY, NEGATIVE_INFINITY, NEGATIVE_ZERO, 3.0),
                        (SMALLEST_DENORMAL, LARGEST_FLOAT, NEGATIVE_DENORMAL, 4.0)),
        'negative_zero_w': rows((1.0, 0.0, 0.0, NEGATIVE_ZERO), (0.0, 1.0, 0.0, NEGATIVE_ZERO),
                                (0.0, 0.0, 1.0, NEGATIVE_ZERO), (0.0, 0.0, 0.0, NEGATIVE_ZERO))}
    for name, m in matrices.items():
        out.append(rows_case('affine_finish', name, {'source': m}))
    out.append(rows_case('affine_finish', 'output_over_source', {'source': matrices['ordinary']}, alias='source'))
    out.append(rows_case('affine_finish', 'output_over_source_nan', {'source': matrices['nan_w']}, alias='source'))
    for offset in (0x10, 0x30):
        out.append(shifted(rows_case('affine_finish', f'output_inside_source_{offset:x}',
                                     {'source': matrices['ordinary']}), 'source', offset, 0x40 + offset))
    return out


MAKERS = {'quaternion': lambda: quaternion_cases(), 'transform': lambda: transform_cases(),
          'rotation_rows': lambda: rotation_rows_cases(), 'rows3_multiply': lambda: rows3_multiply_cases(),
          'affine_finish': lambda: affine_finish_cases()}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = MAKERS[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
