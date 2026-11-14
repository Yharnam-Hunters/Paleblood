#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_stopwatch_frame_end_014486d0 (game/frame_timing/stopwatch.cpp).

usage: stopwatch_frame_end_cases.py OUT_DIR

The stopwatch (0x60 bytes) is filled with a pattern and its fields set per case:
- the frame count below, at and past the averaging point (28, 29, 30, 100), at 0 and -1, and at
  the 32-bit wrap (0x7fffffff becomes the most negative count and averages at once; the most
  negative count does not);
- the totals averaged as given: ordinary, negative, infinite, denormal and NaN (with payloads);
  the start count as signed (0, 7, -1, the most negative and the most positive);
- the frame's start count against its peak, compared signed (above, equal, below, negative on
  either side);
- the frame's seconds against their peak: above, below, equal, zeros of either sign (the old peak
  stays unless strictly above), NaN on either side (the old peak stays), infinities.
"""
import json
import os
import struct
import sys

ADDRESSES = {'frame_end': '0x014486d0'}
FUNCTION = 'frame_timing_stopwatch_frame_end_014486d0'
SIZE = 0x60
NAN_A, NAN_B = 0x7fc12345, 0xffa54321
INF, NEG_INF, DENORMAL = 0x7f800000, 0xff800000, 0x00000003
POSITIVE_ZERO, NEGATIVE_ZERO = 0x00000000, 0x80000000


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def stopwatch(total=f32(1.5), average=f32(-7.0), starts=12, average_starts=f32(3.25), frame_starts=4,
              peak_starts=6, frame_seconds=f32(0.5), peak_seconds=f32(0.75), frames=3):
    data = bytearray(b'\x3c' * SIZE)
    struct.pack_into('<IIIIIIIII', data, 0x3c, total, average, starts & 0xffffffff, average_starts,
                     frame_starts & 0xffffffff, peak_starts & 0xffffffff, frame_seconds, peak_seconds,
                     frames & 0xffffffff)
    return data


def case(cid, **fields):
    return {'schema': 1, 'address': ADDRESSES['frame_end'], 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:watch'},
            'buffers': {'watch': {'size': SIZE, 'bytes': stopwatch(**fields).hex()}},
            'memory': [], 'imports': [], 'stubs': []}


def cases():
    out = []
    for frames in (0, 1, 28, 29, 30, 31, 100, -1, 0x7fffffff, -0x80000000, -30):
        out.append(case(f'frames_{frames & 0xffffffff:x}', frames=frames))
    for name, total in (('negative', f32(-2.5)), ('infinite', INF), ('negative_infinite', NEG_INF),
                        ('denormal', DENORMAL), ('nan', NAN_A), ('nan_negative', NAN_B),
                        ('zero', POSITIVE_ZERO), ('negative_zero', NEGATIVE_ZERO), ('large', f32(3.0e38))):
        out.append(case(f'average_total_{name}', frames=29, total=total))
    for starts in (0, 7, -1, -0x80000000, 0x7fffffff, 1000001):
        out.append(case(f'average_starts_{starts & 0xffffffff:x}', frames=29, starts=starts))
    out.append(case('wrap_average_total', frames=0x7fffffff, total=f32(1.0), starts=-3))
    for name, frame, peak in (('above', 9, 6), ('equal', 6, 6), ('below', 2, 6), ('negative_frame', -1, 6),
                              ('most_negative_frame', -0x80000000, 0), ('negative_peak', 5, -1),
                              ('both_negative', -5, -2), ('zero_both', 0, 0), ('most_positive', 0x7fffffff, 1)):
        out.append(case(f'peak_starts_{name}', frame_starts=frame, peak_starts=peak))
    for name, frame, peak in (('above', f32(2.0), f32(1.0)), ('below', f32(1.0), f32(2.0)),
                              ('equal', f32(1.0), f32(1.0)), ('zero_over_negative_zero', POSITIVE_ZERO, NEGATIVE_ZERO),
                              ('negative_zero_over_zero', NEGATIVE_ZERO, POSITIVE_ZERO),
                              ('nan_frame', NAN_A, f32(1.0)), ('nan_peak', f32(1.0), NAN_B), ('nan_both', NAN_A, NAN_B),
                              ('infinite_frame', INF, f32(1.0)), ('negative_infinite_peak', f32(-1.0), NEG_INF),
                              ('negative_both', f32(-3.0), f32(-4.0))):
        out.append(case(f'peak_seconds_{name}', frame_seconds=frame, peak_seconds=peak))
    out.append(case('everything_at_29', frames=29, total=NAN_B, starts=-7, frame_starts=11, peak_starts=11,
                    frame_seconds=NAN_A, peak_seconds=INF))
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    cs = cases()
    for c in cs:
        with open(os.path.join(out_dir, c['id'] + '.json'), 'w') as f:
            json.dump(c, f, indent=1)
            f.write('\n')
    print(f'{len(cs)} cases written to {out_dir}')


if __name__ == '__main__':
    main()
