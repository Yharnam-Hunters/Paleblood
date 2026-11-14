#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_free.cpp, for tools/verify.py run.

usage: scaleform_heap_free_cases.py OUT_DIR

The engine (`eng`: used bytes at +0x238, allocation count at +0x250) is a buffer; the bins' free is
scripted (it describes the page: the two header pointers into `hdr`, and stores the freed length),
and so is the page release.
"""
import json
import os
import struct
import sys

ADDRESS = '0x0233c0a0'
BIN_FREE, RELEASE_PAGE = '0x0233ab20', '0x0233be70'


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, counts, release, freed=0x40, flag=1, used=0x1000):
    """counts: the lower and upper header's block counts, None for a header the page lacks."""
    memory = [{'addr': 'buf:eng+568', 'bytes': u64(used)}, {'addr': 'buf:eng+592', 'bytes': u64(9)}]
    writes = [{'arg': 4, 'offset': 0, 'bytes': u64(freed)}, {'arg': 3, 'offset': 0x30, 'pointer': 'pages+32'}]
    for i, count in enumerate(counts):
        if count is None:
            writes.append({'arg': 3, 'offset': 8 * i, 'bytes': u64(0)})
        else:
            writes.append({'arg': 3, 'offset': 8 * i, 'pointer': f'hdr+{16 * i}'})
            memory.append({'addr': f'buf:hdr+{16 * i}', 'bytes': u64(0x0005000300005fc0 | (count << 16))})
    stubs = [{'address': BIN_FREE, 'argc': 5, 'writes': writes}]
    if release:
        stubs.append({'address': RELEASE_PAGE, 'argc': 3})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:eng', 'rsi': 'buf:pages+32', 'rdx': 'buf:block', 'rcx': flag},
            'buffers': {'eng': {'size': 0x260}, 'hdr': {'size': 0x20}, 'pages': {'size': 0x40},
                        'block': {'size': 0x10}},
            'memory': memory, 'stubs': stubs, 'imports': []}


def cases():
    return [
        case('both_headers_stay', (3, 6), False),
        case('both_headers_last_zero', (3, 1), True),
        case('lower_zero_upper_not', (1, 4), False),
        case('lower_only_stays', (2, None), False),
        case('lower_only_zero', (1, None), True),
        case('upper_only_zero', (None, 1), True),
        case('upper_only_stays', (None, 2), False),
        case('no_headers', (None, None), True),
        case('count_wraps', (3, 0), False),
        case('flag_zero', (1, 1), True, flag=0),
        case('flag_byte', (1, 1), True, flag=0x200),
        case('freed_more_than_used', (2, 2), False, freed=0x2000),
    ]


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    out = argv[0]
    os.makedirs(out, exist_ok=True)
    all_cases = cases()
    for one in all_cases:
        with open(os.path.join(out, f"{one['id']}.json"), 'w') as f:
            json.dump(one, f, indent=1)
    print(f'{len(all_cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
