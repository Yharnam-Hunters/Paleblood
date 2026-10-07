#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for render_yebis_get_recursive_sample_parameters (0x00fbc3e0), for verify.py run.

usage: yebis_sample_parameters_cases.py OUT_DIR

Float arguments in xmm0 (length) and xmm1 (texel spacing). _FLog and powf are scripted with
float results (bit patterns) chosen to drive each path; spare _Assert entries cover the checks a
case trips. Covers both modes, a base of 2 (the check the frame-rate patches remove), odd/even
bases per mode, a negative base, texel spacing below 1, NaN and the clamp branch, a length too
short for any pass, the pass cap, a last pass of no samples, missing outputs and another mode.
"""
import json
import math
import os
import struct
import sys


def f32(x):
    return struct.pack('<f', x).hex()


def bits(x):
    return '0x' + struct.pack('>f', x).hex()


def case(cid, base, mode, length, texel, max_passes=0, outputs=(True, True, True), flog=None, pw=None):
    num = length if isinstance(length, float) else 64.0
    flog = flog if flog is not None else [math.log(max(num, 1.0) + 1.0), math.log(max(abs(base), 2))]
    pw = pw if pw is not None else [float(abs(base)) ** 2, float(abs(base))]
    args = {'rdi': hex(base & 0xffffffff), 'rsi': hex(mode), 'rdx': hex(max_passes & 0xffffffff),
            'rcx': 'buf:passes' if outputs[0] else '0x0', 'r8': 'buf:final' if outputs[1] else '0x0',
            'r9': 'buf:scale' if outputs[2] else '0x0', 'xmm0': f32(length) if isinstance(length, float) else length,
            'xmm1': f32(texel) if isinstance(texel, float) else texel}
    imports = [{'name': '_Assert', 'argc': 2} for _ in range(8)]
    imports += [{'name': '_FLog', 'argc': 1, 'argf32': [0], 'ret': bits(v)} for v in flog]
    imports += [{'name': 'powf', 'argc': 0, 'argf32': [0, 1], 'ret': bits(v)} for v in pw]
    return {'schema': 1, 'address': '0x00fbc3e0', 'id': cid, 'returns': 'void', 'args': args,
            'buffers': {'passes': {'size': 4}, 'final': {'size': 4}, 'scale': {'size': 4}},
            'memory': [], 'stubs': [], 'imports': imports}


CASES = [
    case('mode1_base5', 5, 1, 64.0, 1.5),
    case('mode2_base4', 4, 2, 64.0, 1.5),
    case('mode1_base7_long', 7, 1, 900.0, 2.0),
    case('base2_mode1', 2, 1, 64.0, 1.5),
    case('base2_mode2', 2, 2, 64.0, 1.5),
    case('mode1_even_base', 6, 1, 64.0, 1.5),
    case('mode2_odd_base', 5, 2, 64.0, 1.5),
    case('mode2_base3', 3, 2, 64.0, 1.5),
    case('negative_base', -3, 1, 64.0, 1.5),
    case('other_mode', 5, 3, 64.0, 1.5),
    case('texel_below_one', 5, 1, 64.0, 0.5),
    case('texel_nan', 5, 1, 64.0, '0000c07f'),
    case('texel_clamped', 5, 1, 8.0, 4.0),
    case('texel_clamped_to_one', 5, 1, 4.0, 4.0),
    case('too_short', 5, 1, 0.05, 1.5),
    case('capped_passes', 5, 1, 900.0, 1.5, max_passes=2, flog=[7.0, 1.0]),
    case('cap_not_reached', 5, 1, 64.0, 1.5, max_passes=9),
    case('last_pass_empty', 5, 1, 64.0, 1.5, pw=[1.0e30, 25.0]),
    case('no_outputs', 5, 1, 64.0, 1.5, outputs=(False, False, False)),
    case('length_nan', 5, 1, '0000c07f', 1.5),
    # maxss keeps its second operand when the first is NaN: a NaN log ratio gives one pass.
    case('log_ratio_nan', 5, 1, 64.0, 1.5, flog=[float('nan'), 1.6]),
    # Another mode with an odd last pass (powf steered so the last pass holds 3 samples).
    case('other_mode_odd_last', 5, 3, 64.0, 1.5, pw=[(5 * (64.0 / 1.5 + 1.0)) / 2.5, 5.0]),
    # base * texel rounds to exactly the length while length / base does not round back to
    # texel: the clamp's >= decides the spacing.
    case('clamp_boundary', 7, 1, struct.unpack('<f', struct.pack('<f', 8.488849639892578))[0],
         struct.unpack('<f', struct.pack('<f', 1.2126927375793457))[0]),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
