#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the segregated bi-heap's free and bottom allocation
(game/engine/segregated_bi_heap_free.cpp, segregated_bi_heap_allocate.cpp), for tools/verify.py run.

usage: segregated_bi_heap_cases.py OUT_DIR [free|allocate]

Each case lays out a heap's state (`heap`, with the terminator descriptor at +0x68) and one
descriptor per block (`desc`, 0x28 bytes apart) for a range of blocks starting at BASE. The heap
never touches the memory it manages, so the blocks are only addresses. Free blocks form the free
list in address order through the terminator; blocks in use are chained, bottom ones in address
order and top ones from the highest, unless a case gives its own chain order. The descriptor
allocator (its allocate and free), the heap's integrity check and the error report are scripted
stubs; descriptors the allocator hands out come from `spare`.
"""
import json
import os
import struct
import sys

FREE, ALLOCATE_BOTTOM = '0x02488900', '0x02488030'
POOL_ALLOCATE, POOL_FREE = '0x02484c70', '0x02484de0'
INTEGRITY_CHECK, FATAL_ERROR = '0x02488cf0', '0x024b55b0'
BASE = 0x10000000
DESC = 0x28
TERMINATOR = 0x68


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


class Layout:
    """blocks: (size, 'free' | 'bottom' | 'top'), from BASE up."""

    def __init__(self, blocks, base=BASE, bottom_order=None, top_order=None, count=5,
                 available=0x40000, heap_limit=None):
        self.memory = []
        self.addresses = []
        at = base
        for size, _ in blocks:
            self.addresses.append(at)
            at += size
        self.end = at
        n = len(blocks)
        names = [f'desc+{DESC * i}' for i in range(n)]
        term = f'heap+{TERMINATOR}'
        chain_of = names + [term]
        for i in range(n):
            self.word(names[i], 0, self.addresses[i])
            self.ptr(names[i], 8, chain_of[i - 1] if i > 0 else term)
            self.ptr(names[i], 16, chain_of[i + 1])
        self.word(term, 0, self.end)
        self.ptr(term, 8, names[-1] if n else term)
        self.ptr(term, 16, names[0] if n else term)
        free = [names[i] for i, (_, kind) in enumerate(blocks) if kind == 'free']
        ring = [term] + free
        for j, node in enumerate(ring):
            self.ptr(node, 0x18, ring[j - 1])
            self.ptr(node, 0x20, ring[(j + 1) % len(ring)])
        for kind, order, slot in (('bottom', bottom_order, 0x90), ('top', top_order, 0x98)):
            members = [i for i, (_, k) in enumerate(blocks) if k == kind]
            if order is None:
                order = members if kind == 'bottom' else members[::-1]
            for j, i in enumerate(order):
                self.word(names[i], 0x18, 0)
                if j + 1 < len(order):
                    self.ptr(names[i], 0x20, names[order[j + 1]])
                else:
                    self.word(names[i], 0x20, 0)
            if order:
                self.ptr('heap+0', slot, names[order[0]])
        self.word('heap+0', 0xa0, base)
        self.word('heap+0', 0xa8, self.end if heap_limit is None else heap_limit)
        self.word('heap+0', 0xb0, count)
        self.word('heap+0', 0xb8, available)
        self.desc_size = max(DESC * n, 8)

    def word(self, node, field, value):
        name, off = node.split('+')
        self.memory.append({'addr': f'buf:{name}+{int(off) + field}', 'bytes': u64(value)})

    def ptr(self, node, field, target):
        name, off = node.split('+')
        self.memory.append({'addr': f'buf:{name}+{int(off) + field}', 'pointer': target})


def case(address, cid, layout, args, returns, spares=(), integrity=None, reports=0, frees=8):
    """spares: what each descriptor allocation returns, a `spare` index or None (no room)."""
    stubs = [{'address': POOL_ALLOCATE, 'argc': 1, 'ret': f'buf:spare+{DESC * s}' if s is not None else 0}
             for s in spares]
    stubs += [{'address': POOL_FREE, 'argc': 2}] * frees
    if integrity is not None:
        stubs.append({'address': INTEGRITY_CHECK, 'argc': 1, 'ret': integrity})
    stubs += [{'address': FATAL_ERROR, 'argc': 3}] * reports
    # Spare descriptors hold junk, which every allocation must overwrite or leave unread.
    memory = layout.memory + [{'addr': f'buf:spare+{8 * k}', 'bytes': u64(0x5a5a5a5a00000000 + k)}
                              for k in range(4 * DESC // 8)]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': {'heap': {'size': 0xc0}, 'desc': {'size': layout.desc_size},
                        'spare': {'size': 4 * DESC}},
            'memory': memory, 'stubs': stubs, 'imports': []}


def free_case(cid, blocks, index=None, address=None, **kw):
    """Frees block `index`'s address (or `address`)."""
    opts = {k: kw.pop(k) for k in ('integrity', 'reports') if k in kw}
    layout = Layout(blocks, **kw)
    if address is None:
        address = layout.addresses[index]
    return case(FREE, cid, layout, {'rdi': 'buf:heap', 'rsi': address}, 'void', **opts)


def allocate_case(cid, blocks, size, alignment, spares=(0, 1), **kw):
    opts = {k: kw.pop(k) for k in ('reports',) if k in kw}
    layout = Layout(blocks, **kw)
    return case(ALLOCATE_BOTTOM, cid, layout, {'rdi': 'buf:heap', 'rsi': size, 'rdx': alignment},
                'i64', spares=spares, **opts)


F, B, T = 'free', 'bottom', 'top'


def free_cases():
    return [
        free_case('between_used', [(0x100, B), (0x200, B), (0x100, B)], 1),
        free_case('chain_head', [(0x100, B), (0x200, B), (0x100, B)], 0),
        free_case('chain_tail', [(0x100, B), (0x200, B), (0x100, B)], 2),
        free_case('top_chain', [(0x100, B), (0x200, T), (0x100, T)], 1),
        free_case('top_chain_head', [(0x100, B), (0x200, T), (0x100, T)], 2),
        free_case('chain_order_given', [(0x100, B), (0x200, B), (0x100, B)], 0, bottom_order=[2, 0, 1]),
        free_case('only_block', [(0x400, B)], 0),
        free_case('merge_after', [(0x100, B), (0x200, B), (0x300, F), (0x100, B)], 1),
        free_case('merge_after_twice', [(0x100, B), (0x200, B), (0x300, F), (0x80, F), (0x100, B)], 1),
        free_case('merge_before', [(0x100, B), (0x300, F), (0x200, B), (0x100, B)], 2),
        free_case('merge_before_twice', [(0x100, B), (0x80, F), (0x300, F), (0x200, B), (0x100, B)], 3),
        free_case('merge_both', [(0x100, B), (0x300, F), (0x200, B), (0x80, F), (0x100, B)], 2),
        free_case('merge_up_to_terminator', [(0x300, F), (0x200, B), (0x80, F)], 1),
        free_case('first_block_by_terminator', [(0x200, B), (0x80, B), (0x100, F)], 0),
        free_case('last_block_by_terminator', [(0x100, F), (0x80, B), (0x200, B)], 2),
        free_case('far_free_neighbours', [(0x100, F), (0x10, B), (0x20, T), (0x200, B), (0x30, B),
                                          (0x40, T), (0x100, F), (0x10, B)], 3),
        free_case('no_free_blocks', [(0x100, T), (0x200, B), (0x300, T)], 1),
        free_case('null', [(0x100, B), (0x200, B)], address=0),
        free_case('not_found_consistent', [(0x100, B), (0x200, F), (0x100, B)], 1, integrity=1, reports=1),
        free_case('not_found_inconsistent', [(0x100, B), (0x200, F), (0x100, B)], 1, integrity=0, reports=2),
        free_case('inside_a_block', [(0x100, B), (0x200, B), (0x100, B)], address=BASE + 0x180,
                  integrity=1, reports=1),
        free_case('below_range', [(0x100, B), (0x200, B)], address=BASE - 0x10, integrity=1, reports=2),
        free_case('at_limit', [(0x100, B), (0x200, B)], address=BASE + 0x300, integrity=1, reports=2),
        free_case('outside_but_found', [(0x100, B), (0x200, B), (0x100, B)], 2, heap_limit=BASE + 0x300,
                  reports=1),
        free_case('counters_wrap', [(0x100, B), (0x200, B), (0x100, B)], 1, count=0, available=-0x100),
    ]


def allocate_cases():
    return [
        allocate_case('exact_fit', [(0x100, B), (0x200, F), (0x100, B)], 0x200, 0x10),
        allocate_case('rounded_up', [(0x100, B), (0x200, F), (0x100, B)], 0x1f1, 0x10),
        allocate_case('split_rest', [(0x100, B), (0x400, F), (0x100, B)], 0x100, 0x10),
        allocate_case('first_fit_skips_small', [(0x80, F), (0x100, B), (0x400, F), (0x100, B),
                                                (0x800, F), (0x10, B)], 0x200, 0x10),
        allocate_case('aligned_front_and_rest', [(0x130, B), (0x800, F), (0x100, B)], 0x100, 0x100),
        allocate_case('aligned_front_exact_end', [(0x130, B), (0x1d0, F), (0x100, B)], 0x100, 0x100),
        allocate_case('alignment_too_far', [(0x130, B), (0x100, F), (0x10, B), (0x2000, F)], 0x10, 0x1000),
        allocate_case('aligned_start_past_end', [(0x130, B), (0x40, F), (0x100, B)], 0, 0x100),
        allocate_case('aligned_start_at_end', [(0x130, B), (0xd0, F), (0x100, B)], 0, 0x100),
        allocate_case('size_zero', [(0x100, B), (0x200, F), (0x100, B)], 0, 0x10),
        allocate_case('no_rest_descriptor', [(0x100, B), (0x400, F), (0x100, B)], 0x100, 0x10, spares=(None,)),
        allocate_case('no_rest_descriptor_aligned', [(0x130, B), (0x800, F), (0x100, B)], 0x100, 0x100,
                      spares=(None,)),
        allocate_case('no_front_descriptor', [(0x130, B), (0x800, F), (0x100, B)], 0x100, 0x100,
                      spares=(0, None)),
        allocate_case('first_block_by_terminator', [(0x400, F), (0x100, B)], 0x100, 0x10),
        allocate_case('last_block_by_terminator', [(0x100, B), (0x400, F)], 0x100, 0x10),
        allocate_case('only_free_block', [(0x1000, F)], 0x100, 0x40),
        allocate_case('nothing_fits', [(0x100, B), (0x80, F), (0x100, B), (0xf0, F), (0x100, B)], 0x100, 0x10),
        allocate_case('empty_free_list', [(0x100, B), (0x100, T)], 0x10, 0x10),
        allocate_case('onto_bottom_chain', [(0x100, B), (0x400, F), (0x100, B)], 0x100, 0x10,
                      bottom_order=[2, 0]),
        allocate_case('alignment_not_power_of_two', [(0x110, B), (0x400, F), (0x100, B)], 0x40, 0x30),
        # Misaligned by 0x20 for an alignment of 0x30: the heap's test sees no front part, so the
        # allocation stays misaligned and the alignment report fires.
        allocate_case('alignment_not_power_of_two_misaligned', [(0x120, B), (0x400, F), (0x100, B)], 0x40,
                      0x30, reports=1),
        allocate_case('alignment_zero', [(0x110, B), (0x400, F), (0x100, B)], 0x40, 0),
        allocate_case('alignment_one', [(0x110, B), (0x400, F), (0x100, B)], 0x40, 1),
        allocate_case('counters_wrap', [(0x100, B), (0x400, F), (0x100, B)], 0x100, 0x10, count=-1, available=0),
        allocate_case('huge_size', [(0x100, B), (0x400, F), (0x100, B)], 0xfffffffffffffff8, 0x10),
    ]


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    out = sys.argv[1]
    which = sys.argv[2] if len(sys.argv) == 3 else 'free'
    cases = free_cases() if which == 'free' else allocate_cases()
    os.makedirs(out, exist_ok=True)
    for c in cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main()
