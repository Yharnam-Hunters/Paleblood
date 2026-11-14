#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the segregated regular heap (game/engine/segregated_regular_heap_*.cpp), for
tools/verify.py run. Each replacement's own generator (segregated_regular_heap_<name>_cases.py)
calls this one with its function.

usage: segregated_regular_heap_cases.py OUT_DIR allocate|allocate_aligned|allocate_top|
                                                allocate_aligned_top|free

Each case lays out a heap (`heap`: counters, class mask, the 64 class list sentinels at +0x60),
its hash table (`table`) and one descriptor per block (`desc`, 0x28 bytes apart) for a range of
blocks starting at BASE, closed by an end descriptor in use (not hashed) whose address is the end
of the range; the address chain is a ring through it. The heap never touches the memory it
manages, so the blocks are only addresses. Free blocks are in their size class's list, in block
order unless a case gives the order; blocks in use are in their hash bucket, in block order. The
descriptor allocator (its allocate and free) and the error report are scripted stubs; descriptors
the allocator hands out come from `spare`.
"""
import json
import os
import struct
import sys

ALLOCATE, ALIGNED, TOP, ALIGNED_TOP, FREE = '0x02489620', '0x024898c0', '0x02489d20', '0x02489fc0', '0x0248a400'
POOL_ALLOCATE, POOL_FREE, FATAL_ERROR = '0x02484c70', '0x02484de0', '0x024b55b0'
BASE = 0x10000000
DESC, CLASSES, BUCKETS = 0x28, 0x60, 0x44f
POOL = 0x460
COLLIDE = BUCKETS * 16          # blocks this far apart share a hash bucket


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def size_class(size):
    if size <= 0x200:
        return (size - 1) >> 4
    return size.bit_length() - 1 + 23


def case(address, cid, blocks, args, returns='i64', spares=(), frees=0, reports=0, orders=None,
         extra_available=0):
    """blocks: (size, 'f' free or 'u' in use) in address order.
    orders: {class: [block indices]} to give a class list's order.
    spares: per descriptor allocation, the spare index it returns, or None for no room."""
    memory = []

    def word(where, value):
        memory.append({'addr': f'buf:{where}', 'bytes': u64(value)})

    def ptr(where, target):
        memory.append({'addr': f'buf:{where}', 'pointer': target})

    n = len(blocks)
    starts = [BASE]
    for size, _ in blocks:
        starts.append(starts[-1] + size)
    d = [f'desc+{DESC * i}' for i in range(n + 1)]
    for i in range(n + 1):
        ptr(f'desc+{DESC * i}', d[(i + 1) % (n + 1)])
        ptr(f'desc+{DESC * i + 8}', d[i - 1])
        word(f'desc+{DESC * i + 0x10}', starts[i])
    word(f'desc+{DESC * n + 0x18}', 0)
    word(f'desc+{DESC * n + 0x20}', 0)
    # Class lists.
    lists = {}
    for i, (size, state) in enumerate(blocks):
        if state == 'f':
            lists.setdefault(size_class(size), []).append(i)
    lists.update(orders or {})
    mask = 0
    for c in range(64):
        sentinel = f'heap+{CLASSES + 16 * c}'
        members = lists.get(c, [])
        if members:
            mask |= 1 << c
        ring = [sentinel] + [f'desc+{DESC * i + 0x18}' for i in members]
        for j, node in enumerate(ring):
            name, o = node.split('+')
            ptr(f'{name}+{int(o)}', ring[(j + 1) % len(ring)])
            ptr(f'{name}+{int(o) + 8}', ring[j - 1])
    # Hash chains of the blocks in use, in block order.
    chains = {}
    for i, (size, state) in enumerate(blocks):
        if state == 'u':
            chains.setdefault(starts[i] % BUCKETS, []).append(i)
    for bucket, members in chains.items():
        ptr(f'table+{8 * bucket}', d[members[0]])
        for a, b in zip(members, members[1:] + [None]):
            if b is None:
                word(f'desc+{DESC * a + 0x18}', 0)
            else:
                ptr(f'desc+{DESC * a + 0x18}', d[b])
            word(f'desc+{DESC * a + 0x20}', 0)
    free_bytes = sum(size for size, state in blocks if state == 'f')
    word('heap+0', free_bytes + extra_available)
    word('heap+8', 0x5a5a5a5a)
    word('heap+16', mask)
    word('heap+24', sum(1 for _, state in blocks if state == 'u'))
    ptr('heap+72', 'table')
    stubs = []
    for s in spares:
        stubs.append({'address': POOL_ALLOCATE, 'argc': 1, 'ret': 0 if s is None else f'buf:spare+{DESC * s}'})
    stubs += [{'address': POOL_FREE, 'argc': 2}] * frees
    stubs += [{'address': FATAL_ERROR, 'argc': 3}] * reports
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns,
            'args': dict({'rdi': 'buf:heap'}, **args),
            'buffers': {'heap': {'size': POOL + 0x60}, 'table': {'size': 8 * BUCKETS},
                        'desc': {'size': DESC * (n + 1)}, 'spare': {'size': DESC * 3}},
            'memory': memory, 'stubs': stubs, 'imports': []}


def start_of(blocks, index):
    return BASE + sum(size for size, _ in blocks[:index])


def allocate_cases():
    def c(cid, blocks, request, **kw):
        return case(ALLOCATE, cid, blocks, {'rsi': request}, **kw)
    return [
        c('exact_class', [(0x40, 'f')], 0x40),
        c('split_small', [(0x100, 'f'), (0x30, 'u')], 0x30, spares=[0]),
        c('request_zero', [(0x40, 'f')], 0, spares=[0]),
        c('request_rounded', [(0x80, 'u'), (0x40, 'f')], 0x31),
        c('lowest_larger_class', [(0x1000, 'f'), (0x80, 'u'), (0x200, 'f'), (0x80, 'f')], 0x50, spares=[0]),
        c('first_in_list', [(0x60, 'f'), (0x20, 'u'), (0x60, 'f')], 0x60, orders={5: [2, 0]}),
        c('rest_joins_nonempty_class', [(0x100, 'f'), (0x10, 'u'), (0xc0, 'f')], 0x40, spares=[0]),
        c('range_own_class_walk', [(0x280, 'f'), (0x10, 'u'), (0x380, 'f'), (0x10, 'u'), (0x300, 'f')], 0x300,
          spares=[0]),
        c('range_own_class_exact', [(0x300, 'f'), (0x10, 'u')], 0x2f8),
        c('range_own_class_none_fits', [(0x280, 'f'), (0x10, 'u'), (0x2c0, 'f')], 0x300),
        c('range_no_class', [(0x100, 'f')], 0x300),
        c('range_larger_class', [(0x800, 'f'), (0x10, 'u'), (0x300, 'f')], 0x201, spares=[0]),
        c('range_list_empties', [(0x380, 'f'), (0x10, 'u')], 0x300, spares=[0]),
        c('large_request', [(0x18000, 'f'), (0x10, 'u')], 0x10000, spares=[0]),
        c('not_enough_available', [(0x100, 'f')], 0x110),
        c('available_exact', [(0x100, 'f')], 0x100),
        c('available_counts_more', [(0x100, 'f')], 0x110, extra_available=0x10),
        c('no_class_large_enough', [(0x40, 'f'), (0x10, 'u')], 0x50),
        c('no_descriptor', [(0x100, 'f'), (0x10, 'u')], 0x40, spares=[None]),
        c('no_descriptor_class_kept', [(0x100, 'f'), (0x10, 'u'), (0x100, 'f')], 0x40, spares=[None]),
        c('collides_in_table', [(COLLIDE, 'u'), (0x100, 'f')], 0x100),
    ]


def aligned_cases():
    def c(cid, blocks, request, alignment, **kw):
        return case(ALIGNED, cid, blocks, {'rsi': request, 'rdx': alignment}, **kw)
    return [
        c('not_power_of_two', [(0x100, 'f')], 0x40, 0x30),
        c('alignment_16_plain', [(0x100, 'f')], 0x40, 0x10, spares=[0]),
        c('alignment_8_plain', [(0x40, 'f')], 0x40, 8),
        c('alignment_0_plain', [(0x40, 'f')], 0x40, 0),
        c('aligned_exact', [(0x40, 'f'), (0x10, 'u')], 0x40, 0x40),
        c('aligned_rest', [(0x400, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[0]),
        c('unaligned_front_and_rest', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[0, 1]),
        c('unaligned_front_only', [(0x10, 'u'), (0x70, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[0]),
        c('worst_case_class_first', [(0x30, 'u'), (0x200, 'f'), (0x10, 'u'), (0x200, 'f')], 0x80, 0x100,
          spares=[0, 1], orders={31: [3, 1]}),
        c('worst_case_none_search', [(0x10, 'u'), (0x90, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[0, 1]),
        c('search_skips_unfit', [(0x30, 'u'), (0x900, 'f'), (0x10, 'u'), (0xc00, 'f'), (0x10, 'u')], 0x100, 0x800,
          spares=[0, 1]),
        c('search_next_class', [(0x30, 'u'), (0x200, 'f'), (0x10, 'u'), (0x1000, 'f'), (0x10, 'u')], 0x100, 0x800,
          spares=[0, 1]),
        c('search_aligned_block', [(0x800, 'u'), (0x200, 'f'), (0x10, 'u')], 0x100, 0x800, spares=[0]),
        c('search_none', [(0x30, 'u'), (0x8c0, 'f'), (0x10, 'u')], 0x100, 0x800),
        c('alignment_0x400_worst_case', [(0x30, 'u'), (0x800, 'f'), (0x10, 'u')], 0x100, 0x400, spares=[0, 1]),
        c('alignment_16_plain_own_class', [(0x40, 'f'), (0x10, 'u'), (0x100, 'f')], 0x40, 0x10),
        # 0x400 still takes the worst-case class (a 0x200 block in a lower class would fit too).
        c('alignment_0x400_worst_case_first', [(0x400, 'u'), (0x200, 'f'), (0x10, 'u'), (0x800, 'f'), (0x10, 'u')],
          0x100, 0x400, spares=[0, 1]),
        # The worst case's own range class may hold blocks too small for it: skipped.
        c('worst_case_class_itself_skipped', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u'), (0x800, 'f'), (0x10, 'u')],
          0x100, 0x400, spares=[0, 1]),
        c('no_classes', [(0x30, 'u')], 0x40, 0x40),
        c('not_enough_available', [(0x100, 'f')], 0x110, 0x40),
        c('request_zero', [(0x10, 'u'), (0x100, 'f')], 0, 0x40, spares=[0, 1]),
        c('no_descriptor_aligned', [(0x400, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[None]),
        c('no_descriptor_front', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[None]),
        c('no_descriptor_rest', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[0, None]),
    ]


def top_cases():
    def c(cid, blocks, request, **kw):
        return case(TOP, cid, blocks, {'rsi': request}, **kw)
    return [
        c('small_highest', [(0x20, 'f'), (0x10, 'u'), (0x100, 'f'), (0x10, 'u'), (0x20, 'f'), (0x10, 'u')], 0x20),
        c('small_highest_larger_class', [(0x20, 'f'), (0x10, 'u'), (0x100, 'f'), (0x10, 'u')], 0x18, spares=[0]),
        c('own_class_walk', [(0x200, 'f'), (0x10, 'u'), (0x40, 'f'), (0x10, 'u'), (0x40, 'f'), (0x10, 'u')], 0x40),
        c('own_class_higher_than_larger', [(0x200, 'f'), (0x10, 'u'), (0x40, 'f'), (0x10, 'u')], 0x40),
        c('range_own_too_small_higher', [(0x800, 'f'), (0x10, 'u'), (0x380, 'f'), (0x10, 'u'), (0x280, 'f'),
                                         (0x10, 'u')], 0x300, spares=[0]),
        c('range_own_none_fits', [(0x280, 'f'), (0x10, 'u')], 0x300),
        c('split_front_free', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, spares=[0]),
        c('split_front_joins_class', [(0x3c0, 'f'), (0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, spares=[0]),
        c('list_order_irrelevant', [(0x100, 'f'), (0x10, 'u'), (0x100, 'f'), (0x10, 'u')], 0x100,
          orders={15: [0, 2]}),
        c('split_front_sixteen', [(0x10, 'u'), (0x50, 'f'), (0x10, 'u')], 0x40, spares=[0]),
        c('no_descriptor', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, spares=[None]),
        c('request_zero', [(0x100, 'f')], 0),
        c('not_enough_available', [(0x100, 'f')], 0x110),
        c('nothing_free', [(0x100, 'u')], 0x40),
    ]


def aligned_top_cases():
    def c(cid, blocks, request, alignment, **kw):
        return case(ALIGNED_TOP, cid, blocks, {'rsi': request, 'rdx': alignment}, **kw)
    return [
        c('not_power_of_two', [(0x100, 'f')], 0x40, 0x60),
        c('alignment_16_top', [(0x100, 'f')], 0x40, 0x10, spares=[0]),
        c('alignment_0_top', [(0x100, 'f')], 0x40, 0, spares=[0]),
        c('front_zero_exact', [(0x40, 'f'), (0x10, 'u')], 0x40, 0x40),
        c('front_zero_rest', [(0x50, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[0]),
        c('front_only', [(0x10, 'u'), (0x70, 'f'), (0x40, 'u')], 0x40, 0x40, spares=[0]),
        c('front_sixteen', [(0x10, 'u'), (0x50, 'f'), (0x10, 'u')], 0x40, 0x20, spares=[0]),
        c('front_and_after', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, 0x100, spares=[0, 1]),
        c('highest_fitting', [(0x400, 'f'), (0x10, 'u'), (0x900, 'f'), (0x10, 'u'), (0x110, 'f'), (0x10, 'u')],
          0x100, 0x100, spares=[0, 1]),
        c('higher_unfit_skipped', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u'), (0x140, 'f'), (0x10, 'u')], 0x100, 0x100,
          spares=[0, 1]),
        c('none_fits', [(0x10, 'u'), (0x140, 'f'), (0x10, 'u')], 0x100, 0x100),
        c('no_classes', [(0x10, 'u'), (0x40, 'f')], 0x100, 0x100),
        c('not_enough_available', [(0x100, 'f')], 0x110, 0x40),
        c('no_descriptor_rest', [(0x50, 'f'), (0x10, 'u')], 0x40, 0x40, spares=[None]),
        c('no_descriptor_placed', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, 0x100, spares=[None]),
        c('no_descriptor_after', [(0x10, 'u'), (0x400, 'f'), (0x10, 'u')], 0x40, 0x100, spares=[0, None]),
    ]


def free_cases():
    def c(cid, blocks, index, **kw):
        kw.setdefault('frees', 2)
        return case(FREE, cid, blocks, {'rsi': start_of(blocks, index)}, returns='void', **kw)
    return [
        c('neighbours_in_use', [(0x40, 'u'), (0x40, 'u'), (0x40, 'u')], 1),
        c('next_free', [(0x40, 'u'), (0x40, 'u'), (0x40, 'f'), (0x40, 'u')], 1),
        c('next_free_class_stays', [(0x40, 'u'), (0x40, 'u'), (0x40, 'f'), (0x40, 'u'), (0x40, 'f'), (0x10, 'u')], 1),
        c('prev_free', [(0x40, 'u'), (0x40, 'f'), (0x40, 'u'), (0x40, 'u')], 2),
        c('prev_free_class_stays', [(0x40, 'f'), (0x10, 'u'), (0x40, 'f'), (0x40, 'u'), (0x40, 'u')], 3),
        c('both_free', [(0x40, 'u'), (0x100, 'f'), (0x40, 'u'), (0x300, 'f'), (0x40, 'u')], 2),
        c('both_free_same_class', [(0x40, 'u'), (0x40, 'f'), (0x40, 'u'), (0x40, 'f'), (0x40, 'u')], 2),
        c('first_block', [(0x40, 'u'), (0x40, 'f'), (0x40, 'u')], 0),
        c('last_block', [(0x40, 'u'), (0x40, 'f'), (0x40, 'u')], 2),
        c('merged_joins_range_class', [(0x10, 'u'), (0x200, 'f'), (0x100, 'u'), (0x300, 'f'), (0x10, 'u'),
                                       (0x400, 'f')], 2),
        c('bucket_second', [(COLLIDE, 'u'), (0x40, 'u'), (0x40, 'u')], 1),
        c('bucket_first', [(COLLIDE, 'u'), (0x40, 'u'), (0x40, 'u')], 0),
        c('null', [(0x40, 'u')], 0, frees=0) | {'args': {'rdi': 'buf:heap', 'rsi': 0}},
    ]


WHICH = {'allocate': allocate_cases, 'allocate_aligned': aligned_cases, 'allocate_top': top_cases,
         'allocate_aligned_top': aligned_top_cases, 'free': free_cases}


def main(argv):
    if len(argv) != 2 or argv[1] not in WHICH:
        sys.exit(__doc__)
    out, which = argv
    os.makedirs(out, exist_ok=True)
    cases = WHICH[which]()
    for c in cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
