#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_reallocate_moving.cpp, for tools/verify.py run.

usage: scaleform_heap_reallocate_moving_cases.py OUT_DIR

The engine (`eng`: used bytes at +0x238, allocation count at +0x250), the report (`info`) and the
page's descriptor (`pages`, its base in `mem`) are buffers; the old block lies in `mem` and the new
one is `new`. The engine's allocation (which reports a length), the bitset's length read, the byte
copy (its arguments carry the length), the bins' free (which describes the page: the two header
pointers into `hdr`, and stores the freed length) and the page release are scripted.

- the allocation fails (nothing else happens) or succeeds;
- the copy: the old block shorter, the reported length shorter, both equal, an old length of 0;
- the page's layout: bitset below the boundary (more room below), above it (equal room, more
  above), and a block below the page's start (a negative unit);
- the headers after the free: both staying, the last reaching zero, one header, none, a count
  that wraps;
- the lock flag: 0, 1, and values whose low byte is 0 or not 1.
"""
import json
import os
import struct
import sys

ADDRESS = '0x0233d140'
ALLOCATE, ALLOCATED_UNITS, BIN_FREE, RELEASE_PAGE = '0x0233c870', '0x027ec890', '0x0233ab20', '0x0233be70'
GUARD_GOT = 0x057e3be0
USED, COUNT = 0x238, 0x250


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, bytes_=0x40, old_units=3, reported=0x40, counts=(3, 6), locked=1, base=0x800, block=0x900,
         fails=False, freed=0x30):
    """counts: the lower and upper header's block counts after the bins' free describes them (None:
    the page lacks that header)."""
    memory = [{'addr': hex(GUARD_GOT), 'pointer': 'guard'},
              {'addr': f'buf:eng+{USED}', 'bytes': u64(0x1000)},
              {'addr': f'buf:eng+{COUNT}', 'bytes': u64(9)},
              {'addr': 'buf:pages+0x18', 'pointer': f'mem+{base}'},
              {'addr': 'buf:info+0', 'bytes': '77' * 0x18}]
    reported_write = [{'arg': 2, 'offset': 0x10, 'bytes': u64(reported)}]
    imports = []
    stubs = [{'address': ALLOCATE, 'argc': 4, 'ret': 0 if fails else 'buf:new', 'writes': reported_write}]
    if not fails:
        stubs.append({'address': ALLOCATED_UNITS, 'argc': 2, 'ret': old_units})
        imports.append({'name': 'memcpy', 'argc': 3, 'ret': 'buf:new'})
        writes = [{'arg': 4, 'offset': 0, 'bytes': u64(freed)}, {'arg': 3, 'offset': 0x30, 'pointer': 'pages+0'}]
        for i, count in enumerate(counts):
            if count is None:
                writes.append({'arg': 3, 'offset': 8 * i, 'bytes': u64(0)})
            else:
                writes.append({'arg': 3, 'offset': 8 * i, 'pointer': f'hdr+{16 * i}'})
                memory.append({'addr': f'buf:hdr+{16 * i}', 'bytes': u64(0x0005000300005fc0 | (count << 16))})
        stubs.append({'address': BIN_FREE, 'argc': 5, 'writes': writes})
        remaining = [c - 1 for c in counts if c is not None]
        if not remaining or remaining[-1] & 0xffff == 0:
            stubs.append({'address': RELEASE_PAGE, 'argc': 3})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:eng', 'rsi': 'buf:pages', 'rdx': f'buf:mem+{block}', 'rcx': bytes_,
                     'r8': 'buf:info', 'r9': locked},
            'buffers': {'eng': {'size': 0x260}, 'pages': {'size': 0x20}, 'mem': {'size': 0x2000},
                        'new': {'size': 0x100}, 'info': {'size': 0x18}, 'hdr': {'size': 0x20},
                        'guard': {'size': 8, 'bytes': '0123456789abcdef'}},
            'memory': memory, 'stubs': stubs, 'imports': imports}


def cases():
    return [
        case('allocation_fails', fails=True),
        case('allocation_fails_locked_byte', fails=True, locked=0x102),
        case('old_shorter', old_units=2, reported=0x60),
        case('reported_shorter', old_units=6, reported=0x30),
        case('equal_lengths', old_units=4, reported=0x40),
        case('old_length_zero', old_units=0, reported=0x40),
        case('reported_zero', old_units=5, reported=0),
        case('long_old_block', old_units=8, reported=0x100, bytes_=0x100),
        case('bitset_below', base=0x400, block=0x410),
        case('bitset_above', base=0xc08, block=0x1010),
        case('block_before_start', base=0x808, block=0x800),
        case('both_headers_stay', counts=(3, 6)),
        case('last_header_zero', counts=(3, 1)),
        case('lower_zero_upper_not', counts=(1, 4)),
        case('lower_only_zero', counts=(1, None)),
        case('upper_only_stays', counts=(None, 2)),
        case('no_headers', counts=(None, None)),
        case('count_wraps', counts=(3, 0)),
        case('freed_more_than_used', freed=0x2000),
        case('unlocked', locked=0, counts=(1, 1)),
        case('locked_low_byte_zero', locked=0x100, counts=(1, 1)),
        case('locked_low_byte_two', locked=0x7fffff02, counts=(1, 1)),
        case('locked_negative', locked=-1, counts=(2, 2)),
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
