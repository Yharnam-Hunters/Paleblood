# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared synthetic edge cases for the stopwatch in stopwatch.cpp, written by
stopwatch_start_cases.py (0x01448580) and stopwatch_stop_cases.py (0x01448620).

gettimeofday is scripted and the game's start time (0x05940660) is preset: ordinary elapsed
times, a time before the start (the unsigned difference wraps to a huge value), elapsed values
that round differently in float than in double, and the start field at zero of either sign,
positive, negative, infinite, denormal and NaN (the start only runs from exactly zero, the stop
only from a positive start). The totals are given NaN payloads and infinities so a swapped add
shows, and the counters sit at their 32-bit wrap. Generated fixtures, not inputs recorded in a
game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'start': '0x01448580', 'stop': '0x01448620'}
GAME_START = '0x05940660'
SIZE = 0x60
NAN_A, NAN_B = 0x7fc12345, 0xffa54321
POSITIVE_ZERO, NEGATIVE_ZERO = 0x00000000, 0x80000000


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def stopwatch(start, total=f32(1.5), total_2=f32(-2.25), counts=(7, 9)):
    data = bytearray(b'\x3c' * SIZE)
    struct.pack_into('<II', data, 0x38, start, total)
    struct.pack_into('<I', data, 0x44, counts[0])
    struct.pack_into('<I', data, 0x4c, counts[1])
    struct.pack_into('<I', data, 0x54, total_2)
    return data


def case(kind, cid, start, now_us=5_000_000_123, game_start_us=1_000_000_000, calls=True, **fields):
    c = {'schema': 1, 'address': ADDRESSES[kind], 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:watch'},
         'buffers': {'watch': {'size': SIZE, 'bytes': stopwatch(start, **fields).hex()}},
         'memory': [{'addr': GAME_START, 'bytes': struct.pack('<Q', game_start_us).hex()}], 'imports': []}
    if calls:
        seconds, microseconds = divmod(now_us, 1_000_000)
        if now_us < 0:
            seconds, microseconds = -1, now_us + 1_000_000
        c['imports'].append({'name': 'gettimeofday', 'argc': 2, 'ret': 0,
                             'writes': [{'arg': 0, 'size': 16, 'bytes': struct.pack('<qq', seconds, microseconds).hex()}]})
    return c


def start_cases():
    k = 'start'
    return [
        case(k, 'stopped_positive_zero', POSITIVE_ZERO),
        case(k, 'stopped_negative_zero', NEGATIVE_ZERO),
        case(k, 'same_time_as_game_start', POSITIVE_ZERO, now_us=1_000_000_000),
        case(k, 'before_game_start_wraps', POSITIVE_ZERO, now_us=999_999_999),
        case(k, 'far_before_game_start', POSITIVE_ZERO, now_us=0, game_start_us=(1 << 64) - 1),
        case(k, 'float_rounding_of_long_run', POSITIVE_ZERO, now_us=1_000_000_000 + 123_456_789_012),
        case(k, 'one_microsecond', POSITIVE_ZERO, now_us=1_000_000_001),
        case(k, 'counters_wrap', POSITIVE_ZERO, counts=(0xffffffff, 0x7fffffff)),
        case(k, 'running_positive', f32(3.0), calls=False),
        case(k, 'running_negative', f32(-3.0), calls=False),
        case(k, 'running_denormal', 1, calls=False),
        case(k, 'running_infinity', f32(float('inf')), calls=False),
        case(k, 'running_nan', NAN_A, calls=False),
    ]


def stop_cases():
    k = 'stop'
    return [
        case(k, 'running_ordinary', f32(2.5)),
        case(k, 'running_denormal_start', 1),
        case(k, 'running_large_start', f32(1e30)),
        case(k, 'running_infinite_start', f32(float('inf'))),
        case(k, 'start_after_now', f32(9000.0)),
        case(k, 'before_game_start_wraps', f32(1.0), now_us=999_999_999),
        case(k, 'totals_nan_payloads', f32(2.5), total=NAN_A, total_2=NAN_B),
        case(k, 'totals_infinite', f32(2.5), total=f32(float('inf')), total_2=f32(float('-inf'))),
        case(k, 'totals_negative_zero', f32(4000.123), total=NEGATIVE_ZERO, total_2=NEGATIVE_ZERO),
        case(k, 'float_rounding_of_long_run', f32(0.75), now_us=1_000_000_000 + 123_456_789_012),
        case(k, 'stopped_positive_zero', POSITIVE_ZERO, calls=False),
        case(k, 'stopped_negative_zero', NEGATIVE_ZERO, calls=False),
        case(k, 'negative_start', f32(-1.0), calls=False),
        case(k, 'negative_denormal_start', 0x80000001, calls=False),
        case(k, 'nan_start', NAN_A, calls=False),
        case(k, 'negative_nan_start', NAN_B, calls=False),
    ]


def main(kind, usage):
    """Write the start or stop cases to the directory named on the command line."""
    if len(sys.argv) != 2:
        print(usage, file=sys.stderr)
        return 2
    items = start_cases() if kind == 'start' else stop_cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in items:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(items)} synthetic cases written to {sys.argv[1]}')
    return 0
