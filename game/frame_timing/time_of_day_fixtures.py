# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared synthetic edge cases for the time-of-day readers in time_of_day.cpp, written by
time_of_day_us_cases.py (0x02e61140), time_of_day_ticks_cases.py (0x01088550) and
time_seed_words_cases.py (0x02e61280). gettimeofday is scripted: ordinary
times, a microsecond field outside 0..999999, negative and huge seconds (the 64-bit multiply
wraps), a failed call (ignored), and for the seed words the counter around its 32-bit wrap.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {
    'frame_timing_time_of_day_us': '0x02e61140',
    'frame_timing_time_of_day_ticks': '0x01088550',
    'frame_timing_time_seed_words': '0x02e61280',
}
SEED_COUNTER = '0x0570d83c'
TIMES = {
    'ordinary': (1_762_790_400, 123_456),
    'zero': (0, 0),
    'last_microsecond': (1_762_790_400, 999_999),
    'microseconds_out_of_range': (5, 3_000_000),
    'negative_microseconds': (5, -1),
    'negative_seconds': (-1, 0),
    'seconds_low_word_wraps': (0x1_0000_0001, 77),
    'multiply_wraps_64_bits': (0x7fff_ffff_ffff_ffff, 0x7fff_ffff_ffff_ffff),
    'all_bits': (-1, -1),
}


def gettimeofday(seconds, microseconds, result=0):
    return {'name': 'gettimeofday', 'argc': 2, 'ret': result,
            'writes': [{'arg': 0, 'bytes': struct.pack('<qq', seconds, microseconds).hex(), 'size': 16}]}


def case(function, cid, time, result=0, owner_tick=None, counter=None):
    c = {'schema': 1, 'address': ADDRESSES[function], 'id': cid, 'args': {}, 'buffers': {}, 'memory': [],
         'imports': [gettimeofday(*time, result)]}
    if function == 'frame_timing_time_seed_words':
        c['returns'] = 'i128'
        c['memory'].append({'addr': SEED_COUNTER, 'bytes': struct.pack('<I', counter).hex()})
    else:
        c['returns'] = 'i64'
    if function == 'frame_timing_time_of_day_ticks':
        c['buffers']['owner'] = {'size': 0x1e8, 'bytes': ('5a' * 0x1d8) + struct.pack('<Q', owner_tick).hex()}
        c['args']['rdi'] = 'buf:owner'
    return c


def cases(function):
    out = []
    ticks = [1, 1000, 16_667, 1 << 63, (1 << 64) - 1]
    counters = [0, 1, 0x7fff_ffff, 0xffff_fffe, 0xffff_ffff, 0x1234_5678]
    for name, time in TIMES.items():
        if function == 'frame_timing_time_of_day_ticks':
            for tick in ticks:
                out.append(case(function, f'{name}_tick_{tick:x}', time, owner_tick=tick))
            continue
        if function == 'frame_timing_time_seed_words':
            for counter in counters:
                out.append(case(function, f'{name}_counter_{counter:x}', time, counter=counter))
            continue
        out.append(case(function, name, time))
    tail = dict(owner_tick=1000) if function == 'frame_timing_time_of_day_ticks' else {}
    if function == 'frame_timing_time_seed_words':
        tail = dict(counter=9)
    out.append(case(function, 'failed_call_ignored', (12, 34), result=-1, **tail))
    return out


def main(function, usage):
    """Write FUNCTION's cases to the directory named on the command line."""
    if len(sys.argv) != 2:
        print(usage, file=sys.stderr)
        return 2
    items = cases(function)
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in items:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(items)} synthetic cases written to {sys.argv[1]}')
    return 0
