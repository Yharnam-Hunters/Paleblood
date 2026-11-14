#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the allocator that owns a pointer (game/engine/allocator_of.cpp).

usage: allocator_of_cases.py OUT_ROOT

Writes OUT_ROOT/engine_allocator_of_0247b720/edge/*.json. Each case gives the thread a pointer
(`tls`, the pointer itself at gs:[0]) with the thread's temporary heap slot below it, and a heap
registry (the stubbed 0x0247bbf0) with a list of heap ranges and a read-write lock whose read
(0x0247f640), created slot and scePthreadRwlockUnlock are scripted. Cases:
- the thread heap: the pointer at its begin, inside, at its last byte, at its end, below it; no
  heap; a heap whose end is before its begin; a heap above bit 63 and heaps across it (compared
  unsigned);
- the registry: no ranges, one range (begin, last byte, end, below), overlapping ranges (the last
  one wins), a range with a null allocator, high addresses and ranges across bit 63 compared
  unsigned, a null pointer in a
  range starting at 0, ranges that hold nothing (the fallback allocator);
- the lock: not created (reported), an invalid lock id from the unlock (reported), another unlock
  error (not reported), on the found and the fallback paths.
"""
import json
import os
import struct
import sys

FUNCTION = 'engine_allocator_of_0247b720'
REGISTRY, READ_LOCK, FALLBACK, FATAL_ERROR = '0x0247bbf0', '0x0247f640', '0x0247fa50', '0x024b55b0'
CREATED = 0x00500200        # a spare address for the lock's created slot
HEAP_OFFSET_GLOBAL = '0x057e4568'
THREAD = 0x100              # the thread pointer's offset in `tls`
HEAP_SLOT = -0x80
RANGES_AT, LOCK_AT = 0x468, 0x470
FALLBACK_ALLOCATOR = 0x7777000
EINVAL, EPERM = 0x80020016, 0x80020001


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, data, heap=None, ranges=(), created=True, unlock=0):
    """heap: None or (begin, end) of the thread's temporary heap; ranges: (allocator, begin, end)."""
    b = {'tls': THREAD + 8, 'registry': 0x480, 'list': 0x18, 'lock_vt': 0x18, 'ranges': 0x18 * max(1, len(ranges)),
         'heap': 0x40}
    m = [{'addr': HEAP_OFFSET_GLOBAL, 'bytes': u64(HEAP_SLOT)},
         {'addr': f'buf:tls+{THREAD}', 'pointer': f'tls+{THREAD}'},
         {'addr': f'buf:registry+{RANGES_AT}', 'pointer': 'list'},
         {'addr': f'buf:registry+{LOCK_AT}', 'pointer': 'lock_vt'},
         {'addr': f'buf:registry+{LOCK_AT + 8}', 'bytes': u64(0x4c4c)},
         {'addr': 'buf:lock_vt+16', 'guest': f'0x{CREATED:08x}'},
         {'addr': 'buf:list+0', 'bytes': u64(0x1111)},
         {'addr': 'buf:list+8', 'pointer': 'ranges'},
         {'addr': 'buf:list+16', 'pointer': f'ranges+{0x18 * len(ranges)}'}]
    if heap is not None:
        m += [{'addr': f'buf:tls+{THREAD + HEAP_SLOT}', 'pointer': 'heap'},
              {'addr': 'buf:heap+0', 'bytes': u64(heap[0])}, {'addr': 'buf:heap+8', 'bytes': u64(heap[1])}]
    else:
        m.append({'addr': f'buf:tls+{THREAD + HEAP_SLOT}', 'bytes': u64(0)})
    for i, (allocator, begin, end) in enumerate(ranges):
        m.append({'addr': f'buf:ranges+{0x18 * i}', 'bytes': u64(allocator) + u64(begin) + u64(end)})
    stubs = [{'address': REGISTRY, 'argc': 0, 'ret': 'buf:registry'}]
    imports = []
    in_heap = heap is not None and heap[0] <= data < heap[1]
    if not in_heap:
        found = any(begin <= data < end for _, begin, end in ranges)
        stubs += [{'address': READ_LOCK, 'argc': 2, 'ret': 0}, {'address': f'0x{CREATED:08x}', 'argc': 1, 'ret': int(created)}]
        if not created:
            stubs.append({'address': FATAL_ERROR, 'argc': 3})
        imports.append({'name': 'scePthreadRwlockUnlock', 'argc': 1, 'ret': unlock})
        if unlock == EINVAL:
            stubs.append({'address': FATAL_ERROR, 'argc': 3})
        if not found:
            stubs.append({'address': FALLBACK, 'argc': 0, 'ret': hex(FALLBACK_ALLOCATOR)})
    return {'schema': 1, 'address': '0x0247b720', 'id': cid, 'returns': 'i64', 'args': {'rdi': hex(data)},
            'gs': f'buf:tls+{THREAD}', 'buffers': {k: {'size': v} for k, v in b.items()}, 'memory': m,
            'stubs': stubs, 'imports': imports}


HEAP = (0x20000, 0x30000)
ONE = [(0xa000, 0x40000, 0x50000)]
OVERLAP = [(0xa000, 0x40000, 0x80000), (0xb000, 0x50000, 0x60000), (0xc000, 0x58000, 0x70000)]
HIGH = [(0xd000, 0x8000000000000000, 0xffffffffffff0000)]

CASES = [
    case('heap_begin', 0x20000, heap=HEAP),
    case('heap_inside', 0x2abcd, heap=HEAP),
    case('heap_last_byte', 0x2ffff, heap=HEAP),
    case('heap_end', 0x30000, heap=HEAP, ranges=ONE),
    case('heap_below', 0x1ffff, heap=HEAP, ranges=ONE),
    case('heap_inverted', 0x28000, heap=(0x30000, 0x20000), ranges=[(0xa000, 0x20000, 0x30000)]),
    case('heap_high', 0x9000000000000000, heap=(0x8000000000000000, 0xa000000000000000)),
    case('heap_high_above', 0xb000000000000000, heap=(0x8000000000000000, 0xa000000000000000), ranges=HIGH),
    case('heap_across_bit63', 0x8000000000000001, heap=(0x7000000000000000, 0x9000000000000000)),
    case('heap_end_past_bit63', 0x20, heap=(0x10, 0x9000000000000000)),
    case('no_heap_one_range', 0x45000, ranges=ONE),
    case('no_heap_heap_range', 0x20000, ranges=ONE),
    case('no_ranges', 0x45000),
    case('no_ranges_with_heap', 0x45000, heap=HEAP),
    case('range_begin', 0x40000, ranges=ONE),
    case('range_last_byte', 0x4ffff, ranges=ONE),
    case('range_end', 0x50000, ranges=ONE),
    case('range_below', 0x3ffff, ranges=ONE),
    case('overlap_last_wins', 0x59000, ranges=OVERLAP),
    case('overlap_middle', 0x51000, ranges=OVERLAP),
    case('overlap_first_only', 0x41000, ranges=OVERLAP),
    case('overlap_last_end', 0x70000, ranges=OVERLAP),
    case('overlap_none', 0x80000, ranges=OVERLAP),
    case('null_allocator', 0x45000, ranges=[(0xa000, 0x40000, 0x50000), (0, 0x44000, 0x46000)]),
    case('high_unsigned', 0x9000000000000000, ranges=HIGH),
    case('high_below', 0x7fffffffffffffff, ranges=HIGH),
    case('range_across_bit63', 0x8000000000000001, ranges=[(0xf000, 0x7000000000000000, 0x9000000000000000)]),
    case('range_end_past_bit63', 0x20, ranges=[(0xf000, 0x10, 0x9000000000000000)]),
    case('null_pointer_held', 0, ranges=[(0xe000, 0, 1)]),
    case('null_pointer_not_held', 0, ranges=ONE),
    case('inverted_range', 0x45000, ranges=[(0xa000, 0x50000, 0x40000)]),
    case('not_created_found', 0x45000, ranges=ONE, created=False),
    case('not_created_fallback', 0x90000, ranges=ONE, created=False),
    case('unlock_invalid_found', 0x45000, ranges=ONE, unlock=EINVAL),
    case('unlock_invalid_fallback', 0x90000, ranges=ONE, unlock=EINVAL),
    case('unlock_other_error', 0x45000, ranges=ONE, unlock=EPERM),
    case('not_created_and_invalid', 0x45000, ranges=ONE, created=False, unlock=EINVAL),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    d = os.path.join(sys.argv[1], FUNCTION, 'edge')
    os.makedirs(d, exist_ok=True)
    for c in CASES:
        with open(os.path.join(d, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {d}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
