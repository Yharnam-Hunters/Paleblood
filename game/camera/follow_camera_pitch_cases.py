#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic camera-angle cases, including exact float call arguments and NaN payloads.

No case is a recording from a game run. Imports return scripted bit patterns so the getter's
call arguments, fallback selection and result transformation are checked independently of libm.
"""
import json
from pathlib import Path
import struct
import sys


def raw(value):
    """Encode a float, or an explicit IEEE-754 bit pattern, without quieting supplied NaNs."""
    return struct.pack('<f', value) if isinstance(value, float) else struct.pack('<I', value)


def case(address, cid, forward, pitch=0x3f123456, yaw=0xbf654321, result=0x3f765432):
    """Surround the camera view with canaries and script an optional atan2 call."""
    data = bytearray(b'\xa5' * 0x170)
    data[0x40:0x50] = b''.join(raw(value) for value in forward)
    data[0x150:0x154] = raw(pitch)
    data[0x154:0x158] = raw(yaw)
    return {
        'schema': 1, 'address': address, 'id': cid, 'returns': 'f32',
        'args': {'rdi': 'buf:camera+0x10'},
        'buffers': {'camera': {'size': len(data), 'bytes': data.hex()}},
        'imports': [{'name': 'atan2f', 'argc': 0, 'argf32': [0, 1], 'ret': hex(result)}],
    }


# W is unrelated to either angle. Signed zero, subnormal, overflow and distinct NaN payloads
# distinguish exact comparisons and the instruction order in the horizontal length.
DIRECTIONS = [
    ('zero', (0.0, 0.0, 0.0, 41.0)),
    ('negative_zeros', (-0.0, -0.0, -0.0, -41.0)),
    ('signed_zero_x', (-0.0, 0.0, 0.0, 41.0)),
    ('signed_zero_z', (0.0, -0.0, -0.0, 41.0)),
    ('horizontal_x', (3.0, 0.0, 0.0, 41.0)),
    ('horizontal_z', (0.0, 0.0, 4.0, 41.0)),
    ('vertical', (0.0, 9.0, 0.0, 41.0)),
    ('negative_vertical', (-0.0, -9.0, -0.0, 41.0)),
    ('three_four_five', (3.0, 9.0, 4.0, 41.0)),
    ('negative_x', (-3.0, 9.0, 4.0, 41.0)),
    ('negative_z', (3.0, -9.0, -4.0, 41.0)),
    ('negative_horizontal', (-3.0, -9.0, -4.0, 41.0)),
    ('x_subnormal', (0x00000001, 0.0, 0.0, 41.0)),
    ('z_subnormal', (0.0, -0.0, 0x80000001, 41.0)),
    ('y_subnormal', (0.0, 0x00000001, 0.0, 41.0)),
    ('overflow_length', (0x7f7fffff, 9.0, 0x7f7fffff, 41.0)),
    ('infinite_x', (0x7f800000, -9.0, 4.0, 41.0)),
    ('infinite_z', (3.0, 9.0, 0xff800000, 41.0)),
    ('infinite_y', (3.0, 0x7f800000, 4.0, 41.0)),
    ('nan_x', (0x7fc12345, 0.0, 0.0, 41.0)),
    ('nan_z', (0.0, -0.0, 0xffc54321, 41.0)),
    ('nan_y', (0.0, 0xffc23456, 0.0, 41.0)),
    ('nan_horizontal_payloads', (0x7fc12345, 9.0, 0xffc54321, 41.0)),
    ('signaling_nan_x', (0x7f812345, 0.0, 0.0, 41.0)),
    ('signaling_nan_z', (0.0, -0.0, 0xff854321, 41.0)),
    ('ignored_w_nan', (3.0, 9.0, 4.0, 0x7fc34567)),
]


ADDRESS = '0x0183fad0'
CASES = [case(ADDRESS, cid, direction) for cid, direction in DIRECTIONS]
CASES += [
    case(ADDRESS, 'square_underflow_tie', (0x1a000000, 0.0, 0.0, 1.0)),
    case(ADDRESS, 'square_above_underflow_tie', (0x1a000001, 0.0, 0.0, 1.0)),
    case(ADDRESS, 'fallback_nan_payload', (0.0, 0.0, 0.0, 1.0), pitch=0xff812345),
    case(ADDRESS, 'fallback_negative_zero', (0.0, 0.0, 0.0, 1.0), pitch=0x80000000),
    case(ADDRESS, 'returned_negative_zero', (3.0, 9.0, 4.0, 1.0), result=0x80000000),
    case(ADDRESS, 'returned_nan_payload', (3.0, 9.0, 4.0, 1.0), result=0xff812345),
]


def write_cases(cases):
    """Write fixtures to the explicit private output directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(sys.argv[0]).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in cases:
        (output / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(cases)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(write_cases(CASES))
