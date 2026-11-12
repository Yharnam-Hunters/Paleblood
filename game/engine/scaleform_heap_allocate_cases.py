#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_allocate.cpp, for tools/verify.py run.

usage: scaleform_heap_allocate_cases.py OUT_DIR

The engine (`eng`: alignment at +0x10, used bytes at +0x238, allocation count at +0x250) and the
report (`info`) are buffers; everything the allocation calls is a scripted stub: the bins'
allocation (which describes the page: its two header pointers and its descriptor, into `hdr` and
`pages`), the two page-adding functions (which store the retry flag), the large-block and the
aligned allocations, and the page manager's lock (the page manager is `pm`, its mutex at +8).
"""
import json
import os
import struct
import sys

ADDRESS = '0x0233c870'
BIN_ALLOCATE, ADD_PAGE, ADD_PAGE_LOCKED = '0x0233a7f0', '0x0233bbe0', '0x0233bd20'
ALLOCATE_LARGE, ALLOCATE_ALIGNED = '0x0233c660', '0x0233ca70'
GUARD_GOT, PAGE_MANAGER = 0x057e3be0, 0x05940360


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def bin_hit(below=True, above=True):
    writes = [{'arg': 2, 'offset': 0, **({'pointer': 'hdr+0'} if below else {'bytes': u64(0)})},
              {'arg': 2, 'offset': 8, **({'pointer': 'hdr+16'} if above else {'bytes': u64(0)})},
              {'arg': 2, 'offset': 0x30, 'pointer': 'pages+0x20'}]
    return {'address': BIN_ALLOCATE, 'argc': 3, 'ret': 'buf:block', 'writes': writes}


BIN_MISS = {'address': BIN_ALLOCATE, 'argc': 3, 'ret': 0}


def add_page(retry, locked=False):
    return {'address': ADD_PAGE_LOCKED if locked else ADD_PAGE, 'argc': 2,
            'writes': [{'arg': 1, 'offset': 0, 'bytes': '01' if retry else '00'}]}


def large(ret, retry=None):
    stub = {'address': ALLOCATE_LARGE, 'argc': 5, 'ret': 'buf:block' if ret else 0}
    if retry is not None:
        stub['writes'] = [{'arg': 3, 'offset': 0, 'bytes': '01' if retry else '00'}]
    return stub


LOCK = {'name': 'pthread_mutex_lock', 'argc': 1, 'ret': 0}
UNLOCK = {'name': 'pthread_mutex_unlock', 'argc': 1, 'ret': 0}


def case(cid, request, locked, stubs, alignment=0x10, imports=()):
    memory = [{'addr': hex(GUARD_GOT), 'pointer': 'guard'},
              {'addr': hex(PAGE_MANAGER), 'pointer': 'pm'},
              {'addr': 'buf:eng+16', 'bytes': u64(alignment)},
              {'addr': 'buf:eng+568', 'bytes': u64(0x1230)},
              {'addr': 'buf:eng+592', 'bytes': u64(7)},
              {'addr': 'buf:hdr+0', 'bytes': u64(0x00050003_00045fc0)},
              {'addr': 'buf:hdr+16', 'bytes': u64(0x00050003_ffff5fc0)},
              {'addr': 'buf:info+8', 'bytes': u64(0x5a5a5a5a5a5a5a5a)}]
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:eng', 'rsi': request, 'rdx': 'buf:info', 'rcx': locked},
            'buffers': {'eng': {'size': 0x260}, 'info': {'size': 0x18}, 'hdr': {'size': 0x20},
                        'pages': {'size': 0x40}, 'block': {'size': 0x10}, 'pm': {'size': 0x30},
                        'guard': {'size': 8, 'bytes': '0123456789abcdef'}},
            'memory': memory, 'stubs': list(stubs), 'imports': list(imports)}


def cases():
    aligned = {'address': ALLOCATE_ALIGNED, 'argc': 5, 'ret': 'buf:block'}
    return [
        case('small_hit_both_headers', 0x18, 0, [bin_hit()]),
        case('small_hit_header_below_only', 0x20, 0, [bin_hit(above=False)]),
        case('small_hit_header_above_only', 0x31, 0, [bin_hit(below=False)]),
        case('small_hit_no_headers', 0x40, 0, [bin_hit(False, False)]),
        case('small_request_zero', 0, 0, [bin_hit()]),
        case('small_largest', 0x200, 0, [bin_hit()]),
        case('small_miss_page_added', 0x30, 0, [BIN_MISS, add_page(True), bin_hit()]),
        case('small_miss_twice', 0x30, 0, [BIN_MISS, add_page(True), BIN_MISS, add_page(True), bin_hit()]),
        case('small_miss_no_retry', 0x30, 0, [BIN_MISS, add_page(False)]),
        case('small_locked_page_added', 0x30, 1, [BIN_MISS, add_page(True, True), bin_hit()]),
        case('small_locked_no_retry', 0x30, 1, [BIN_MISS, add_page(False, True)]),
        case('small_locked_flag_byte', 0x30, 0x100, [BIN_MISS, add_page(False)]),
        case('large_unlocked', 0x201, 0, [large(True)], imports=[LOCK, UNLOCK]),
        case('large_unlocked_retry', 0x400, 0, [large(False, True), large(True)], imports=[LOCK, UNLOCK]),
        case('large_unlocked_fails', 0x400, 0, [large(False, True), large(False, False)],
             imports=[LOCK, UNLOCK]),
        case('large_locked', 0x400, 1, [large(True)]),
        case('large_locked_retry', 0x400, 1, [large(False, True), large(True)]),
        case('large_locked_fails', 0x400, 1, [large(False, False)]),
        case('large_alignment_8', 0x1000, 0, [large(True)], alignment=8, imports=[LOCK, UNLOCK]),
        case('aligned_heap', 0x40, 0, [aligned], alignment=0x20),
        case('aligned_heap_locked', 0x400, 1, [aligned], alignment=0x11),
        case('aligned_heap_large_alignment', 0x10, 0, [aligned], alignment=0x10000),
    ]


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    out = argv[0]
    os.makedirs(out, exist_ok=True)
    all_cases = cases()
    for c in all_cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(all_cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
