#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the heap manager's area registration (game/engine/heap_manager_register_area.cpp),
for tools/verify.py run.

usage: heap_manager_register_area_cases.py OUT_DIR

The manager is a buffer with the area array's pointer and the read-write lock (its vtable a buffer
whose third slot names a stub). The array's areas sit in `old` (its capacity given per case), and
its heap is a buffer with the free-byte count and the light mutex (vtable slots naming lock and
unlock stubs). The heaps the areas name are plain numbers. Stubbed: the write lock (0x0247f3d0),
the regular heap's allocate (returning `new` or null) and free, the light mutex's lock and unlock,
the lock's is-initialized slot, the system's unlock and the fatal error (which returns). A failed
allocation runs the report path with the thread's transaction flag (below the thread pointer)
clear: the jump does not return in the game, and nothing follows it in the original.
"""
import json
import os
import struct
import sys

ADDRESS = '0x0247c000'
WRITE_LOCK, ALLOCATE, FREE, FATAL = '0x0247f3d0', '0x02485730', '0x024858a0', '0x024b55b0'
SCE_UNLOCK = '0x02fc0ca8'
RW_IS_INITIALIZED, MUTEX_LOCK, MUTEX_UNLOCK = '0x00500200', '0x00500210', '0x00500220'
ACTIVE_OFFSET, JUMP_OFFSET = '0x057e4318', '0x057e4320'
EINVAL = 0x80020016
AREA = 0x18
HEAP_A, HEAP_B, HEAP_C, HEAP_NEW = 0x1000, 0x2000, 0x3000, 0x7000
TLS_BELOW = 0x40  # the thread pointer sits this far into `tls`; the flag and jump buffer below it


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def stub(address, argc, ret=0):
    return {'address': address, 'argc': argc, 'ret': ret if isinstance(ret, str) else hex(ret)}


def case(cid, areas, start, size, heap=HEAP_NEW, capacity=None, allocation='ok', available=0x100000,
         unlock=(1, 0), contradiction=0):
    """areas: (heap, start, end) in array order. capacity: slots (None: one more than the areas; 0
    with no areas: a null array). allocation: 'ok' or 'null' when the array is full."""
    count = len(areas)
    if capacity is None:
        capacity = count + 1
    full = count == capacity
    buffers = {'manager': {'size': 0x488, 'bytes': 'c3' * 0x488},
               'rw_vtable': {'size': 0x20, 'bytes': 'c3' * 0x20},
               'array': {'size': 0x28, 'bytes': 'a5' * 0x28},
               'heap': {'size': 0x448, 'bytes': '5a' * 0x448},
               'mutex_vtable': {'size': 0x30, 'bytes': 'c3' * 0x30}}
    memory = [{'addr': 'buf:manager+0x468', 'pointer': 'array'},
              {'addr': 'buf:manager+0x470', 'pointer': 'rw_vtable'},
              {'addr': 'buf:manager+0x478', 'bytes': q(0x1122334455667788)},
              {'addr': 'buf:rw_vtable+0x10', 'guest': RW_IS_INITIALIZED},
              {'addr': 'buf:array+0x20', 'pointer': 'heap'},
              {'addr': 'buf:heap+0x18', 'bytes': q(available)},
              {'addr': 'buf:heap+0x440', 'pointer': 'mutex_vtable'},
              {'addr': 'buf:mutex_vtable+0x18', 'guest': MUTEX_LOCK},
              {'addr': 'buf:mutex_vtable+0x28', 'guest': MUTEX_UNLOCK}]
    if capacity:
        buffers['old'] = {'size': capacity * AREA, 'bytes': ''.join(q(h) + q(s) + q(e) for h, s, e in areas)}
        memory += [{'addr': 'buf:array+0x8', 'pointer': 'old'},
                   {'addr': 'buf:array+0x10', 'pointer': f'old+{count * AREA:#x}'},
                   {'addr': 'buf:array+0x18', 'pointer': f'old+{capacity * AREA:#x}'}]
    else:
        memory += [{'addr': f'buf:array+{o:#x}', 'bytes': q(0)} for o in (8, 0x10, 0x18)]
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:manager', 'rsi': heap, 'rdx': start, 'rcx': size},
         'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': []}
    if heap == 0:
        return c
    c['stubs'] = [stub(WRITE_LOCK, 2)] + [stub(FATAL, 3)] * contradiction
    c['_full'] = full
    c['_allocation'] = allocation
    c['_count'] = count
    c['_unlock'] = unlock
    return c


def finish(c, inserted):
    """Adds the insert's and the unlock's stubs (in call order) and drops the helper keys."""
    full, allocation, count = c.pop('_full'), c.pop('_allocation'), c.pop('_count')
    unlock = c.pop('_unlock')
    s = c['stubs']
    if inserted and full:
        new_capacity = max(count + count // 2, count + 1)
        c['buffers']['new'] = {'size': new_capacity * AREA, 'bytes': 'e7' * (new_capacity * AREA)}
        s += [stub(MUTEX_LOCK, 2), stub(ALLOCATE, 2, 'buf:new' if allocation == 'ok' else 0), stub(MUTEX_UNLOCK, 1)]
        if allocation != 'ok':
            c['buffers']['tls'] = {'size': TLS_BELOW + 8}
            c['gs'] = f'buf:tls+{TLS_BELOW:#x}'
            c['memory'] += [{'addr': f'buf:tls+{TLS_BELOW:#x}', 'pointer': f'tls+{TLS_BELOW:#x}'},
                            {'addr': ACTIVE_OFFSET, 'bytes': q(-0x10)}, {'addr': JUMP_OFFSET, 'bytes': q(-0x40)},
                            {'addr': f'buf:tls+{TLS_BELOW - 0x10:#x}', 'bytes': q(0)}]
            s.append(stub(FATAL, 3))
        if count:
            s += [stub(MUTEX_LOCK, 2), stub(FREE, 2), stub(MUTEX_UNLOCK, 1)]
    initialized, result = unlock
    s.append(stub(RW_IS_INITIALIZED, 1, initialized))
    if not initialized & 0xff:
        s.append(stub(FATAL, 3))
    s.append(stub(SCE_UNLOCK, 1, result))
    if result & 0xffffffff == EINVAL:
        s.append(stub(FATAL, 3))
    return c


A, B, C = (HEAP_A, 0x10000, 0x20000), (HEAP_B, 0x30000, 0x40000), (HEAP_C, 0x50000, 0x60000)


def cases():
    out = []

    def add(cid, areas, start, size, inserted=True, **kw):
        out.append(finish(case(cid, areas, start, size, **kw), inserted))

    out.append(case('no_heap', [A], 0x80000, 0x1000, heap=0))
    # Where the new area goes (room in the array).
    add('empty_array', [], 0x10000, 0x1000)
    add('after_all', [A, B], 0x80000, 0x1000)
    add('before_all', [A, B], 0x1000, 0x1000)
    add('between', [A, B, C], 0x44000, 0x1000)
    add('between_first_two', [A, B, C], 0x24000, 0x1000)
    add('touching_end_of_previous', [A, B], 0x20000, 0x1000)
    add('same_start_as_area', [A, B], 0x30000, 0x1000)
    add('inside_area_fits', [A, B], 0x31000, 0x1000)
    add('inside_area_ends_with_it', [A, B], 0x31000, 0xf000)
    add('inside_area_overruns', [A, B], 0x31000, 0x20000, contradiction=1)
    add('end_wraps', [A, B], 0x31000, (1 << 64) - 0x30f00)
    add('size_zero', [A, B], 0x45000, 0)
    # The same heap where the walk stops: nothing added.
    add('same_heap_before', [A, (HEAP_NEW, 0x30000, 0x40000)], 0x40000, 0x1000, inserted=False)
    add('same_heap_inside', [A, (HEAP_NEW, 0x30000, 0x40000)], 0x31000, 0x1000, inserted=False)
    add('same_heap_overruns', [A, (HEAP_NEW, 0x30000, 0x40000)], 0x31000, 0x20000, inserted=False, contradiction=1)
    add('same_heap_further_down', [(HEAP_NEW, 0x10000, 0x20000), B], 0x80000, 0x1000)
    add('same_heap_skipped_past', [A, (HEAP_NEW, 0x30000, 0x40000)], 0x24000, 0x1000)
    # A full array grows by half, at least by one.
    add('grow_null_array', [], 0x10000, 0x1000, capacity=0)
    add('grow_one', [A], 0x80000, 0x1000, capacity=1)
    add('grow_two_insert_front', [A, B], 0x1000, 0x1000, capacity=2)
    add('grow_three_insert_middle', [A, B, C], 0x44000, 0x1000, capacity=3)
    add('grow_four_append', [A, B, C, (HEAP_A, 0x70000, 0x71000)], 0x80000, 0x1000, capacity=4)
    add('grow_same_heap_refused', [A, (HEAP_NEW, 0x30000, 0x40000)], 0x40000, 0x10, inserted=False, capacity=2)
    add('grow_heap_too_small', [], 0x10000, 0x1000, capacity=0, available=0x17, allocation='null')
    add('grow_heap_exactly_enough', [A], 0x80000, 0x1000, capacity=1, available=0x30)
    add('grow_fails_outside_transaction', [], 0x10000, 0x1000, capacity=0, allocation='null')
    # The inlined unlock's reports.
    add('lock_not_created', [A], 0x80000, 0x1000, unlock=(0, 0))
    add('lock_virtual_upper_bits', [A], 0x80000, 0x1000, unlock=(0x100, 0))
    add('lock_invalid', [A], 0x80000, 0x1000, unlock=(1, EINVAL))
    add('lock_invalid_high_bits', [A], 0x80000, 0x1000, unlock=(1, 0xdeadbeef00000000 | EINVAL))
    add('lock_other_error', [A], 0x80000, 0x1000, unlock=(1, 0x80020001))
    add('lock_both_reports', [A], 0x80000, 0x1000, unlock=(0, EINVAL))
    add('lock_reports_when_refused', [(HEAP_NEW, 0x30000, 0x40000)], 0x40000, 0x10, inserted=False, unlock=(0, EINVAL))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    all_cases = cases()
    for c in all_cases:
        for k in [k for k in c if k.startswith('_')]:
            del c[k]
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(all_cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
