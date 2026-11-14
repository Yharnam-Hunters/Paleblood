#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for render_fractal_noise_02a85e30 (game/render/model_wind_noise.cpp), for
tools/verify.py run. The result is compared bit for bit.

usage: model_wind_noise_cases.py OUT_DIR

- real: the base noise (0x02a85950) runs as the original. Zero octaves (no sample); the wind's
  own calls (gust: time, time + 20, time + 50; jitter: time, 6 time + 400, time + 150; step 0.1,
  persistence 0.3, five and four octaves) at times from negative to large; one to twelve
  octaves; a step of 2 with persistence 0.5; zero, one and negative steps and persistences;
  negative zero, subnormal, huge and infinite coordinates and steps; quiet NaNs with different
  payloads in a coordinate, the step and the persistence (which operand's payload survives
  shows the operand order).
- scripted: the base noise stubbed with chosen samples (the first two scaled coordinates
  compared): sums whose rounding depends on the order of the additions,
  infinities of both signs, a NaN sample, negative zero samples, NaN coordinates scaled by a NaN
  scale (the scaled coordinates show which payload survives).

A negative octave count is not covered: like the original's count-down loop it runs about four
billion octaves. Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02a85e30'
BASE_NOISE = '0x02a85950'
NAN_A, NAN_B, NAN_C = 0x7fc12345, 0x7fd54321, 0xffe00001


def f32(value):
    if isinstance(value, int):
        return value
    return struct.unpack('<I', struct.pack('<f', value))[0]


def xmm(value):
    return struct.pack('<I', f32(value)).hex()


def case(cid, point, step, persistence, octaves, samples=None):
    x, y, z = point
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'f32',
         'args': {'xmm0': xmm(x), 'xmm1': xmm(y), 'xmm2': xmm(z), 'xmm3': xmm(step), 'xmm4': xmm(persistence),
                  'rdi': 'buf:noise', 'rsi': octaves & 0xffffffff},
         'buffers': {'noise': {'size': 8, 'bytes': '5c' * 8}},
         'memory': [], 'imports': [], 'stubs': []}
    for sample in samples or []:
        # The state pointer is not compared: the original does not keep it for the second and
        # later samples (the base noise never reads it).
        c['stubs'].append({'address': BASE_NOISE, 'argc': 0, 'argf32': [0, 1], 'ret': f'{f32(sample):#x}'})
    return c


def gust(time):
    return (time, time + 20.0, time + 50.0)


def jitter(time):
    return (time, time * 6.0 + 400.0, time + 150.0)


INF = float('inf')
CASES = [
    case('no_octaves', gust(3.5), 0.1, 0.3, 0),
    case('no_octaves_nan_point', (NAN_A, NAN_B, NAN_C), NAN_A, NAN_B, 0),
]
for t in (0.0, 0.25, 0.5, 1.25, 7.9, 13.7, 100.3, 1234.56, -7.25, -0.6):
    CASES.append(case(f'gust_time_{t:g}', gust(t), 0.1, 0.3, 5))
    CASES.append(case(f'jitter_time_{t:g}', jitter(t), 0.1, 0.3, 4))
for n in range(1, 13):
    CASES.append(case(f'octaves_{n}', (2.3, -4.7, 9.1), 2.0, 0.5, n))
CASES += [
    case('step_zero', (5.5, 6.25, -3.75), 0.0, 0.5, 3),
    case('step_one', (5.5, 6.25, -3.75), 1.0, 0.5, 3),
    case('step_negative', (5.5, 6.25, -3.75), -1.5, 0.5, 4),
    case('persistence_zero', (5.5, 6.25, -3.75), 2.0, 0.0, 3),
    case('persistence_one', (5.5, 6.25, -3.75), 2.0, 1.0, 3),
    case('persistence_negative', (5.5, 6.25, -3.75), 2.0, -0.75, 4),
    case('persistence_above_one', (5.5, 6.25, -3.75), 3.0, 1.75, 6),
    case('negative_zero_point', (-0.0, -0.0, -0.0), 2.0, 0.5, 3),
    case('subnormal_point', (1e-40, -1e-41, 1e-39), 2.0, 0.5, 3),
    case('subnormal_step', (5.5, 6.25, -3.75), 1e-40, 0.5, 3),
    case('huge_point', (3e9, -2e10, 1e38), 2.0, 0.5, 3),
    case('step_overflows', (5.5, 6.25, -3.75), 1e30, 0.5, 3),
    case('infinite_point', (INF, -INF, 1.0), 2.0, 0.5, 2),
    case('infinite_step', (5.5, 6.25, -3.75), INF, 0.5, 3),
    case('infinite_persistence', (5.5, 6.25, -3.75), 2.0, INF, 3),
    case('nan_point_x', (NAN_A, 6.25, -3.75), 2.0, 0.5, 3),
    case('nan_point_z', (5.5, 6.25, NAN_C), 2.0, 0.5, 3),
    case('nan_point_and_step', (NAN_A, 6.25, -3.75), NAN_B, 0.5, 3),
    case('nan_point_and_persistence', (NAN_A, 6.25, -3.75), 2.0, NAN_B, 3),
    case('nan_step_and_persistence', (5.5, 6.25, -3.75), NAN_A, NAN_B, 3),
    case('nan_step_negative_payload', (5.5, 6.25, -3.75), NAN_C, 0.5, 2),
    # Scripted samples: the additions' order decides the rounding.
    case('scripted_rounding_order', (1.0, 2.0, 3.0), 2.0, 1.0, 4, [1.0e8, 3.0, -1.0e8, 1.0]),
    case('scripted_cancellation', (1.0, 2.0, 3.0), 2.0, 0.5, 3, [3.0e7, 1.0, -6.0e7]),
    case('scripted_infinities', (1.0, 2.0, 3.0), 2.0, 0.5, 3, [INF, 1.0, -INF]),
    case('scripted_nan_sample', (1.0, 2.0, 3.0), 2.0, 0.5, 3, [0.25, NAN_A, NAN_B]),
    case('scripted_negative_zero', (1.0, 2.0, 3.0), 2.0, 0.5, 2, [-0.0, -0.0]),
    case('scripted_nan_amplitude', (1.0, 2.0, 3.0), 2.0, NAN_A, 3, [0.5, NAN_B, 1.0]),
    case('scripted_nan_point_and_step', (NAN_A, NAN_C, 3.0), NAN_B, 0.5, 3, [0.5, 0.25, 0.125]),
    case('scripted_wind_gust', gust(13.7), 0.1, 0.3, 5, [0.6, -0.4, 0.9, -0.1, 0.3]),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in CASES:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(CASES)} synthetic cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
