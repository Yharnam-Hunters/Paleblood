#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_thread_random (0x02c6d580).

usage: thread_random_cases.py OUT_DIR

The thread pointer (gs:[0]) points into a fake thread block, and the thread-local offset
(0x057e44a0) is preset to place the state below it, as a TLS block is. Covers an unseeded state
(seeded from the 32-bit time: seconds times a million plus microseconds, truncated), a seeded
one (no call), other flag bits on either side, and times whose 32-bit and 64-bit sums differ.
The generator seeding function (0x024f8110) is stubbed; its arguments are compared. Generated
fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02c6d580'
TLS_OFFSET_SLOT = '0x057e44a0'
SEED_FN = '0x024f8110'
THREAD_BLOCK = 0x100
SEEDED = 0x01


def case(cid, flags, time=(1_762_790_400, 654_321), offset=-0x40):
    state = THREAD_BLOCK + offset
    block = bytearray(b'\x9d' * (THREAD_BLOCK + 0x10))
    block[state + 0x14] = flags
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64', 'args': {},
         'gs': f'buf:tls+{THREAD_BLOCK}',
         'buffers': {'tls': {'size': len(block), 'bytes': block.hex()}},
         'memory': [{'addr': f'buf:tls+{THREAD_BLOCK}', 'pointer': f'tls+{THREAD_BLOCK}'},
                    {'addr': TLS_OFFSET_SLOT, 'bytes': struct.pack('<q', offset).hex()}],
         'imports': [], 'stubs': []}
    if not flags & SEEDED:
        c['imports'].append({'name': 'gettimeofday', 'argc': 2, 'ret': 0,
                             'writes': [{'arg': 0, 'size': 16, 'bytes': struct.pack('<qq', *time).hex()}]})
        c['stubs'].append({'address': SEED_FN, 'argc': 1, 'argu32': [1]})
    return c


CASES = [
    case('unseeded', 0),
    case('unseeded_other_flag_bits', 0xfe),
    case('unseeded_zero_time', 0, time=(0, 0)),
    case('unseeded_seconds_wrap_32_bits', 0, time=(0x1_2345_6789, 999_999)),
    case('unseeded_negative_time', 0, time=(-5, -7)),
    case('unseeded_microseconds_upper_bits', 0, time=(3, 0x7fff_ffff_0000_0001)),
    case('unseeded_offset_far_below', 0, offset=-0xf0),
    case('seeded', SEEDED),
    case('seeded_all_flag_bits', 0xff),
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
