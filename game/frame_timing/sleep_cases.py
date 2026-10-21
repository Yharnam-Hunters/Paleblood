#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_sleep_ms (0x0111a880).

usage: sleep_cases.py OUT_DIR

The import is scripted, so these cases never sleep. They cover the 32-bit millisecond-to-
microsecond multiplication, its first wrap, and the ignored kernel result. These are generated
fixtures, not inputs recorded in a genuine game run.
"""
import json
import os
import sys

ADDRESS = '0x0111a880'
UINT32_MAX = (1 << 32) - 1
MICROSECONDS_PER_MILLISECOND = 1000


def case(cid, milliseconds, import_result=0):
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': milliseconds}, 'buffers': {}, 'memory': [],
        'imports': [{'name': 'sceKernelUsleep', 'argc': 1, 'ret': import_result}],
    }


CASES = [
    case('zero', 0),
    case('one_millisecond', 1),
    case('frame_delay', 16),
    case('one_second', 1000),
    case('last_nonwrapping_delay', UINT32_MAX // MICROSECONDS_PER_MILLISECOND),
    case('first_wrapping_delay', UINT32_MAX // MICROSECONDS_PER_MILLISECOND + 1),
    case('high_bit_delay', 1 << 31),
    case('maximum_delay', UINT32_MAX),
    case('ignores_negative_kernel_result', 1, 0x80020016),
    case('ignores_positive_kernel_result', 1, 7),
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
