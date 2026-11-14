#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the shader upload manager's blocks (game/render/shader_upload_blocks.cpp), for
tools/verify.py run. Synthetic layouts; helper effects are scripted and are not game recordings.

usage: shader_upload_blocks_cases.py OUT_DIR acquire|allocate|reclaim

The manager (`mgr`) holds the sentinel range at +0x68, the pending lists at +0x98..+0xb0, the
counters at +0xc8/+0xd0 and its lock at +0xd8, whose vtable's lock and unlock are stubs at fake
game addresses. Range records are buffers `rN` (state, start, previous, following, free previous,
next) tiled in address order and closed by the sentinel, whose start is the end of memory. The
node pool's allocate and free are stubs that hand out spare records `nN` (or null).
- acquire: the allocator is a stub; sizes with high bits, a null and a found block.
- allocate: empty free list; exact fit; room left behind (with and without a record); misaligned
  start (padding and rest records, either missing); a too-small first range skipped; a range
  that ends before its aligned start (room 0, a zero size takes it); sizes 0, 1, 0x10, 0x11 and
  one that wraps when rounded; alignments 0, 1, 0x18, 0x100 and 0x1000; the free list in another
  order than memory; a manager at an odd address (the pool rounds it up).
- reclaim: no pending blocks; a released block alone, between handed-out blocks, next to free
  ranges above, below or both (two free ranges above), next to the sentinel on either side; kept
  and released blocks mixed in one list (kept first, last, all released); states held through a
  pointer (4 and 2), a state with high bits; blocks in all four lists (the drain order shows in
  the freed records); two adjacent released blocks; the compare/exchange retried on a changed head
  when taking and when putting back (stubbed); a manager at an odd address.
"""
import json
import os
import struct
import sys

ADDRESSES = {'acquire': '0x02aa9d50', 'allocate': '0x02aa9db0', 'reclaim': '0x02aaa860'}
RETURNS = {'acquire': 'i64', 'allocate': 'i64', 'reclaim': 'void'}
LOCK, UNLOCK = '0x00500200', '0x00500220'
ALLOCATE_RANGE, NODE_ALLOCATE, NODE_FREE = '0x02aa9db0', '0x02a3b8b0', '0x02a3b9b0'
EXCHANGE = '0x0247ebc0'
GUARD = '0x057e3be0'
SENTINEL, PENDING, IN_USE, FREE_BYTES, LOCK_OBJECT = 0x68, 0x98, 0xc8, 0xd0, 0xd8
USED, DATA, PREVIOUS, FOLLOWING, FREE_PREVIOUS, NEXT = 0, 8, 0x10, 0x18, 0x20, 0x28
RECORD = 0x30
CONTEXT = 0xb470
BASE = 0x2_0000_0000
MASK64 = (1 << 64) - 1


def qword(value):
    return struct.pack('<Q', value & MASK64).hex()


class World:
    """A manager and its ranges. ranges: (name, start, state word) in address order."""

    def __init__(self, cid, function, ranges, end, free=None, pending=None, shift=0, spares=2):
        self.cid, self.function, self.shift = cid, function, shift
        self.buffers = {'mgr': {'size': 0xe8, 'bytes': '6b' * 0xe8},
                        'lock_vtable': {'size': 0x40, 'bytes': 'e7' * 0x40}}
        self.memory = [{'addr': f'buf:mgr+{shift + LOCK_OBJECT:#x}', 'pointer': 'lock_vtable+0x0'},
                       {'addr': 'buf:lock_vtable+0x18', 'guest': LOCK},
                       {'addr': 'buf:lock_vtable+0x28', 'guest': UNLOCK},
                       {'addr': f'buf:mgr+{shift + IN_USE:#x}', 'bytes': qword(5)},
                       {'addr': f'buf:mgr+{shift + FREE_BYTES:#x}', 'bytes': qword(0x123400)}]
        for i in range(spares):
            self.buffers[f'n{i}'] = {'size': RECORD, 'bytes': '5a' * RECORD}
        names = [name for name, _, _ in ranges]
        for name, start, state in ranges:
            self.buffers[name] = {'size': RECORD, 'bytes': '3c' * RECORD}
            self.put(name, USED, state)
            self.put(name, DATA, start)
        self.put('S', USED, 0)
        self.put('S', DATA, end)
        ring = ['S'] + names
        for i, name in enumerate(ring):
            self.link(name, FOLLOWING, ring[(i + 1) % len(ring)])
            self.link(name, PREVIOUS, ring[i - 1])
        if free is None:
            free = [name for name, _, state in ranges if state == 0]
        ring = ['S'] + list(free)
        for i, name in enumerate(ring):
            self.link(name, NEXT, ring[(i + 1) % len(ring)])
            self.link(name, FREE_PREVIOUS, ring[i - 1])
        for index in range(4):
            chain = (pending or {}).get(index, [])
            self.memory.append({'addr': f'buf:mgr+{self.shift + PENDING + 8 * index:#x}',
                                'pointer' if chain else 'bytes': self.ref(chain[0]) if chain else qword(0)})
            for j, name in enumerate(chain):
                if j + 1 < len(chain):
                    self.link(name, NEXT, chain[j + 1])
                else:
                    self.put(name, NEXT, 0)
        self.stubs = []

    def ref(self, name, offset=0):
        if name == 'S':
            return f'mgr+{self.shift + SENTINEL + offset:#x}'
        return f'{name}+{offset:#x}'

    def put(self, name, offset, value):
        self.memory.append({'addr': 'buf:' + self.ref(name, offset), 'bytes': qword(value)})

    def link(self, name, offset, target):
        self.memory.append({'addr': 'buf:' + self.ref(name, offset), 'pointer': self.ref(target)})

    def case(self, args):
        return {'schema': 1, 'address': ADDRESSES[self.function], 'id': self.cid,
                'returns': RETURNS[self.function], 'args': args, 'buffers': self.buffers,
                'memory': self.memory, 'stubs': self.stubs}


def lock_stubs():
    return [{'address': LOCK, 'argc': 2, 'argi': [0], 'argu32': [1]}]


def unlock_stubs():
    return [{'address': UNLOCK, 'argc': 1}]


# acquire ------------------------------------------------------------------------------------------
def acquire_cases():
    for bytes_, found in ((0x10000, True), (0x10000, False), (0, True), (0xFFFFFFFF, True),
                          (0x1_0000_0010, True), (MASK64, False)):
        w = World(f'acquire_{bytes_:x}_found_{int(found)}', 'acquire', [('r0', BASE, 2)], BASE + 0x10000)
        w.stubs = lock_stubs() + [{'address': ALLOCATE_RANGE, 'argc': 3, 'ret': 'buf:r0' if found else 0}] + \
            unlock_stubs()
        yield w.case({'rdi': 'buf:mgr', 'rsi': hex(bytes_)})
    w = World('acquire_odd_manager', 'acquire', [], BASE, shift=3)
    w.stubs = lock_stubs() + [{'address': ALLOCATE_RANGE, 'argc': 3, 'ret': '0x1234'}] + unlock_stubs()
    yield w.case({'rdi': 'buf:mgr+0x3', 'rsi': '0x10000'})


# allocate -----------------------------------------------------------------------------------------
def allocate(cid, ranges, end, bytes_, alignment=0x10, nodes=(), free=None, shift=0):
    w = World(cid, 'allocate', ranges, end, free=free, shift=shift)
    w.stubs = [{'address': NODE_ALLOCATE, 'argc': 1, 'ret': f'buf:{n}' if n else 0} for n in nodes]
    return w.case({'rdi': f'buf:mgr+{shift:#x}', 'rsi': hex(bytes_), 'rdx': hex(alignment)})


def allocate_cases():
    yield allocate('empty_free_list', [('r0', BASE, 2)], BASE + 0x100, 0x10)
    yield allocate('none_large_enough', [('r0', BASE, 0), ('r1', BASE + 0x40, 2)], BASE + 0x100, 0x50)
    yield allocate('exact_fit', [('r0', BASE, 2), ('r1', BASE + 0x40, 0), ('r2', BASE + 0x80, 2)],
                   BASE + 0x100, 0x40)
    yield allocate('exact_fit_rounded', [('r0', BASE, 0)], BASE + 0x40, 0x31)
    for nodes in (('n0',), (None,)):
        tag = 'record' if nodes[0] else 'no_record'
        yield allocate(f'rest_{tag}', [('r0', BASE, 2), ('r1', BASE + 0x40, 0), ('r2', BASE + 0x100, 2)],
                       BASE + 0x200, 0x20, nodes=nodes)
    for nodes in (('n0', 'n1'), ('n0', None), (None,)):
        tag = '_'.join('r' if n else 'x' for n in nodes)
        yield allocate(f'misaligned_{tag}', [('r0', BASE, 2), ('r1', BASE + 0x48, 0), ('r2', BASE + 0x200, 2)],
                       BASE + 0x300, 0x20, alignment=0x100, nodes=nodes)
    yield allocate('misaligned_exact_room', [('r1', BASE + 0x8, 0)], BASE + 0x40, 0x30, nodes=('n0', 'n1'))
    yield allocate('first_too_small_skipped',
                   [('r0', BASE, 0), ('r1', BASE + 0x20, 2), ('r2', BASE + 0x100, 0)], BASE + 0x400,
                   0x40, nodes=('n0',))
    yield allocate('free_list_order_not_address',
                   [('r0', BASE, 0), ('r1', BASE + 0x100, 2), ('r2', BASE + 0x200, 0)], BASE + 0x400,
                   0x40, nodes=('n0',), free=['r2', 'r0'])
    yield allocate('aligned_past_end_zero_size', [('r0', BASE + 0x8, 0), ('r1', BASE + 0xc, 2)],
                   BASE + 0x100, 0, alignment=0x10)
    yield allocate('aligned_past_end_skipped', [('r0', BASE + 0x8, 0), ('r1', BASE + 0xc, 2)],
                   BASE + 0x100, 1, alignment=0x10)
    for bytes_ in (0, 1, 0x10, 0x11, MASK64, MASK64 - 0x10):
        yield allocate(f'size_{bytes_:x}', [('r0', BASE, 0)], BASE + 0x1000, bytes_, nodes=('n0',))
    for alignment in (0, 1, 0x18, 0x100, 0x1000):
        yield allocate(f'alignment_{alignment:x}', [('r0', BASE + 0x28, 0)], BASE + 0x3000, 0x40,
                       alignment=alignment, nodes=('n0', 'n1'))
    yield allocate('alignment_18_aligned_start', [('r0', BASE + 0x30, 0)], BASE + 0x3000, 0x40,
                   alignment=0x18, nodes=('n0', 'n1'))
    yield allocate('odd_manager', [('r0', BASE + 0x8, 0)], BASE + 0x100, 0x20, nodes=('n0', 'n1'), shift=3)


# reclaim ------------------------------------------------------------------------------------------
def reclaim(cid, ranges, end, pending, free=None, shift=0, frees=0, exchange=None, states=None):
    w = World(cid, 'reclaim', ranges, end, free=free, pending=pending, shift=shift, spares=0)
    w.buffers['guard'] = {'size': 8, 'bytes': '1122334455667788'}
    w.buffers['context'] = {'size': CONTEXT, 'bytes': '00' * CONTEXT}
    w.buffers['command_slot'] = {'size': 8}
    w.memory += [{'addr': GUARD, 'pointer': 'guard+0x0'},
                 {'addr': 'buf:command_slot+0x0', 'pointer': 'context+0x0'},
                 {'addr': 'buf:context+0xb468', 'bytes': 'a4d3c2b1f0e1d2c3'}]
    for name, value in (states or {}).items():
        w.buffers[name] = {'size': 8, 'bytes': struct.pack('<I', value).hex() + '9a9a9a9a'}
    w.stubs = lock_stubs() + (exchange or []) + [{'address': NODE_FREE, 'argc': 2} for _ in range(frees)] + \
        unlock_stubs()
    return w.case({'rdi': f'buf:mgr+{shift:#x}', 'rsi': 'buf:command_slot'})


def reclaim_cases():
    H = 2  # a handed-out block
    R = 4  # released by its owner
    yield reclaim('nothing_pending', [('r0', BASE, H)], BASE + 0x100, {})
    yield reclaim('released_between_handed_out', [('r0', BASE, H), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, H)],
                  BASE + 0x100, {2: ['r1']})
    yield reclaim('kept_only', [('r0', BASE, H), ('r1', BASE + 0x40, H)], BASE + 0x100, {0: ['r0', 'r1']})
    yield reclaim('free_above', [('r0', BASE, H), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, 0),
                                 ('r3', BASE + 0xc0, H)], BASE + 0x100, {2: ['r1']}, frees=1)
    yield reclaim('two_free_above', [('r1', BASE, R), ('r2', BASE + 0x40, 0), ('r3', BASE + 0x80, 0),
                                     ('r4', BASE + 0xc0, H)], BASE + 0x100, {3: ['r1']}, frees=2)
    yield reclaim('free_below', [('r0', BASE, H), ('r1', BASE + 0x40, 0), ('r2', BASE + 0x80, R),
                                 ('r3', BASE + 0xc0, H), ('r4', BASE + 0xd0, 0)], BASE + 0x100, {0: ['r2']},
                  frees=1)
    yield reclaim('free_both_sides', [('r0', BASE, 0), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, 0),
                                      ('r3', BASE + 0xc0, H)], BASE + 0x100, {1: ['r1']}, frees=2)
    yield reclaim('only_range', [('r0', BASE, R)], BASE + 0x100, {2: ['r0']})
    yield reclaim('sentinel_below_free_above', [('r0', BASE, R), ('r1', BASE + 0x40, 0)], BASE + 0x100,
                  {2: ['r0']}, frees=1)
    yield reclaim('free_below_sentinel_above', [('r0', BASE, 0), ('r1', BASE + 0x40, R)], BASE + 0x100,
                  {2: ['r1']}, frees=1)
    mixed = [('r0', BASE, H), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, H), ('r3', BASE + 0xc0, R),
             ('r4', BASE + 0x100, H)]
    yield reclaim('kept_first_and_last', mixed, BASE + 0x200, {2: ['r0', 'r1', 'r2', 'r3', 'r4']})
    yield reclaim('released_first_and_last', mixed, BASE + 0x200, {3: ['r1', 'r2', 'r3']})
    yield reclaim('all_released', mixed, BASE + 0x200, {0: ['r3', 'r1']})
    yield reclaim('adjacent_released', [('r0', BASE, H), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, R),
                                        ('r3', BASE + 0xc0, 0)], BASE + 0x100, {2: ['r1', 'r2']}, frees=2)
    yield reclaim('adjacent_released_reverse', [('r0', BASE, H), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, R),
                                                ('r3', BASE + 0xc0, 0)], BASE + 0x100, {2: ['r2', 'r1']},
                  frees=2)
    yield reclaim('state_high_bits', [('r0', BASE, H), ('r1', BASE + 0x40, 0x1_0000_0004), ('r2', BASE + 0x80, H),
                                      ('r3', BASE + 0xc0, 0x4_0000_0002)], BASE + 0x100, {2: ['r1', 'r3']})
    case = reclaim('state_through_pointer', [('r0', BASE, H), ('r1', BASE + 0x40, H), ('r2', BASE + 0x80, 0),
                                             ('r3', BASE + 0xc0, H)], BASE + 0x100, {2: ['r1', 'r3']},
                   states={'state_r1': R, 'state_r3': H}, frees=1)
    case['memory'] += [{'addr': 'buf:r1+0x0', 'pointer': 'state_r1+0x1'},
                       {'addr': 'buf:r3+0x0', 'pointer': 'state_r3+0x1'}]
    yield case
    four = [(f'r{i}', BASE + 0x40 * i, R if i % 2 else (0 if i in (0, 8) else H)) for i in range(9)]
    yield reclaim('all_four_lists', four, BASE + 0x400, {0: ['r1'], 1: ['r3'], 2: ['r5'], 3: ['r7']}, frees=2)
    yield reclaim('all_four_lists_kept', four, BASE + 0x400, {0: ['r1', 'r2'], 1: ['r4', 'r3'], 2: ['r6'],
                                                               3: ['r7', 'r2']}, frees=2)
    yield reclaim('odd_manager', [('r0', BASE, 0), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, 0)],
                  BASE + 0x100, {2: ['r1']}, shift=3, frees=2)

    # The compare/exchange stubbed: a changed head is read again, when taking and putting back.
    changed = [{'address': EXCHANGE, 'argc': 3, 'ret': '0x99', 'writes': [{'arg': 0, 'pointer': 'r2'}]},
               {'address': EXCHANGE, 'argc': 3, 'ret': 'buf:r2'}]
    rest = [{'address': EXCHANGE, 'argc': 3, 'ret': 0} for _ in range(3)]
    case = reclaim('take_retried_on_changed_head', [('r0', BASE, H), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, R),
                                                    ('r3', BASE + 0xc0, H)], BASE + 0x100, {2: ['r1']},
                   exchange=changed + rest)
    case['memory'].append({'addr': 'buf:r2+0x28', 'bytes': qword(0)})
    yield case
    put_back = [{'address': EXCHANGE, 'argc': 3, 'ret': 'buf:r0'},
                {'address': EXCHANGE, 'argc': 3, 'ret': '0x77', 'writes': [{'arg': 0, 'pointer': 'r3'}]},
                {'address': EXCHANGE, 'argc': 3, 'ret': 'buf:r3'}] + rest
    yield reclaim('put_back_retried_on_changed_head', [('r0', BASE, H), ('r1', BASE + 0x40, R), ('r2', BASE + 0x80, H),
                                                       ('r3', BASE + 0xc0, H)], BASE + 0x100, {2: ['r0', 'r1']},
                  exchange=put_back)


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = list({'acquire': acquire_cases, 'allocate': allocate_cases, 'reclaim': reclaim_cases}[sys.argv[2]]())
    for entry in generated:
        with open(os.path.join(out, entry['id'] + '.json'), 'w') as f:
            json.dump(entry, f, indent=2)
            f.write('\n')
    print(f'{len(generated)} synthetic edge cases written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
