#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the segregated bi-heap (game/engine/segregated_bi_heap_*.cpp), for
tools/verify.py run.

usage: segregated_bi_heap_cases.py OUT_DIR [free|allocate|top|free_top|check|initialize|finalize]

Each case lays out a heap's state (`heap`, with the terminator descriptor at +0x68) and one
descriptor per block (`desc`, 0x28 bytes apart) for a range of blocks starting at BASE. The heap
never touches the memory it manages, so the blocks are only addresses. Free blocks form the free
list in address order through the terminator; blocks in use are chained, bottom ones in address
order and top ones from the highest, unless a case gives its own chain order. The descriptor
allocator (its allocate and free), the heap's integrity check and the error report are scripted
stubs; descriptors the allocator hands out come from `spare`. The integrity check's cases set the
heap up (the byte at +0x67) and break one thing at a time with `extra` writes over the layout.
The set-up and tear-down cases lay out the heap themselves, at an offset into its buffer so that
the descriptor allocator's padding varies.
"""
import json
import os
import struct
import sys

FREE, ALLOCATE_BOTTOM = '0x02488900', '0x02488030'
FREE_TOP, ALLOCATE_TOP, CHECK = '0x02488f40', '0x024882f0', '0x02488cf0'
INITIALIZE, FINALIZE, RELEASE_CHUNKS = '0x02487e50', '0x02488650', '0x02484ae0'
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
                 available=0x40000, heap_limit=None, initialized=False):
        self.memory = []
        if initialized:
            self.memory.append({'addr': 'buf:heap+103', 'bytes': '01'})
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


def case(address, cid, layout, args, returns, spares=(), integrity=None, reports=0, frees=8, extra=()):
    """spares: what each descriptor allocation returns, a `spare` index or None (no room)."""
    stubs = [{'address': POOL_ALLOCATE, 'argc': 1, 'ret': f'buf:spare+{DESC * s}' if s is not None else 0}
             for s in spares]
    stubs += [{'address': POOL_FREE, 'argc': 2}] * frees
    if integrity is not None:
        stubs.append({'address': INTEGRITY_CHECK, 'argc': 1, 'ret': integrity})
    stubs += [{'address': FATAL_ERROR, 'argc': 3}] * reports
    # Spare descriptors hold junk, which every allocation must overwrite or leave unread.
    memory = layout.memory + [{'addr': f'buf:spare+{8 * k}', 'bytes': u64(0x5a5a5a5a00000000 + k)}
                              for k in range(4 * DESC // 8)] + list(extra)
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': {'heap': {'size': 0xc0}, 'desc': {'size': layout.desc_size},
                        'spare': {'size': 4 * DESC}},
            'memory': memory, 'stubs': stubs, 'imports': []}


def free_case(cid, blocks, index=None, address=None, function=FREE, **kw):
    """Frees block `index`'s address (or `address`)."""
    opts = {k: kw.pop(k) for k in ('integrity', 'reports') if k in kw}
    layout = Layout(blocks, **kw)
    if address is None:
        address = layout.addresses[index]
    return case(function, cid, layout, {'rdi': 'buf:heap', 'rsi': address}, 'void', **opts)


def allocate_case(cid, blocks, size, alignment, spares=(0, 1), function=ALLOCATE_BOTTOM, **kw):
    opts = {k: kw.pop(k) for k in ('reports',) if k in kw}
    layout = Layout(blocks, **kw)
    return case(function, cid, layout, {'rdi': 'buf:heap', 'rsi': size, 'rdx': alignment},
                'i64', spares=spares, **opts)


def check_case(cid, blocks, extra=(), initialized=True, **kw):
    """extra: (node, field, value) writes over the layout; a value 'desc+N' / 'heap+N' is a pointer."""
    layout = Layout(blocks, initialized=initialized, **kw)
    writes = []
    for node, field, value in extra:
        name, off = node.split('+')
        where = f'buf:{name}+{int(off) + field}'
        if isinstance(value, str):
            writes.append({'addr': where, 'pointer': value})
        else:
            writes.append({'addr': where, 'bytes': u64(value)})
    return case(CHECK, cid, layout, {'rdi': 'buf:heap'}, 'i8', frees=0, extra=writes)


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


def top_cases():
    def c(cid, blocks, size, alignment, **kw):
        return allocate_case(cid, blocks, size, alignment, function=ALLOCATE_TOP, **kw)
    return [
        c('exact_fit', [(0x100, B), (0x200, F), (0x100, B)], 0x200, 0x10),
        c('rounded_up', [(0x100, B), (0x200, F), (0x100, B)], 0x1f1, 0x10),
        c('split_top', [(0x100, B), (0x400, F), (0x100, B)], 0x100, 0x10),
        c('last_fit_skips_small', [(0x800, F), (0x100, B), (0x400, F), (0x100, B), (0x80, F), (0x10, B)],
          0x200, 0x10),
        c('takes_the_highest', [(0x800, F), (0x100, B), (0x800, F), (0x100, B)], 0x100, 0x10),
        c('gap_above', [(0x100, B), (0x830, F), (0x100, B)], 0x100, 0x100),
        c('gap_above_no_second_descriptor', [(0x100, B), (0x830, F), (0x100, B)], 0x100, 0x100,
          spares=(0, None)),
        c('aligned_front_only', [(0x130, B), (0x1d0, F), (0x100, B)], 0x100, 0x100),
        c('front_and_gap', [(0x130, B), (0x1e0, F), (0x110, B)], 0x100, 0x100),
        c('no_descriptor', [(0x100, B), (0x400, F), (0x100, B)], 0x100, 0x10, spares=(None,)),
        c('no_descriptor_aligned', [(0x130, B), (0x800, F), (0x100, B)], 0x100, 0x100, spares=(None,)),
        c('top_leaves_empty_block', [(0x100, B), (0x210, F), (0x100, B)], 0x200, 0x100),
        c('first_block_by_terminator', [(0x400, F), (0x100, B)], 0x100, 0x10),
        c('last_block_by_terminator', [(0x100, B), (0x400, F)], 0x100, 0x10),
        c('only_free_block', [(0x1000, F)], 0x100, 0x40),
        c('only_free_block_misaligned_end', [(0x1010, F)], 0x100, 0x40),
        c('nothing_fits', [(0x100, B), (0x80, F), (0x100, B), (0xf0, F), (0x100, B)], 0x100, 0x10),
        c('empty_free_list', [(0x100, B), (0x100, T)], 0x10, 0x10),
        c('alignment_too_far', [(0x2000, F), (0x130, B), (0x100, F), (0x10, B)], 0x10, 0x1000),
        c('aligned_start_past_end', [(0x130, B), (0x40, F), (0x100, B)], 0, 0x100),
        c('size_zero', [(0x100, B), (0x200, F), (0x100, B)], 0, 0x10),
        c('aligned_start_at_end', [(0x130, B), (0xd0, F), (0x100, B)], 0, 0x100),
        c('onto_top_chain', [(0x100, T), (0x400, F), (0x100, T)], 0x100, 0x10, top_order=[2, 0]),
        # Not a power of two: the masks are not what an alignment's would be.
        c('alignment_not_power_of_two', [(0x110, B), (0x400, F), (0x100, B)], 0x40, 0x30),
        c('alignment_not_power_of_two_gap', [(0x110, B), (0x420, F), (0x100, B)], 0x40, 0x30, reports=2),
        c('alignment_zero', [(0x110, B), (0x400, F), (0x100, B)], 0x40, 0),
        c('alignment_one', [(0x110, B), (0x400, F), (0x100, B)], 0x40, 1),
        c('counters_wrap', [(0x100, B), (0x400, F), (0x100, B)], 0x100, 0x10, count=-1, available=0),
        c('huge_size', [(0x100, B), (0x400, F), (0x100, B)], 0xfffffffffffffff8, 0x10),
    ]


def free_top_cases():
    def c(cid, blocks, index=None, **kw):
        return free_case(cid, blocks, index, function=FREE_TOP, **kw)
    return [
        c('only_top', [(0x100, B), (0x200, T), (0x100, B)], 1),
        c('chain_head', [(0x100, T), (0x200, T), (0x100, T)], 2),
        c('chain_tail', [(0x100, T), (0x200, T), (0x100, T)], 0),
        c('chain_middle', [(0x100, T), (0x200, T), (0x100, T)], 1),
        c('chain_order_given', [(0x100, T), (0x200, T), (0x100, T)], 0, top_order=[0, 2, 1]),
        c('merge_both', [(0x100, B), (0x300, F), (0x200, T), (0x80, F), (0x100, B)], 2),
        c('merge_twice_each_side', [(0x80, F), (0x300, F), (0x200, T), (0x80, F), (0x40, F), (0x100, T)], 2),
        c('merge_up_to_terminator', [(0x300, F), (0x200, T), (0x80, F)], 1),
        c('far_free_neighbours', [(0x100, F), (0x10, B), (0x20, T), (0x200, T), (0x30, B),
                                  (0x40, T), (0x100, F), (0x10, B)], 3),
        c('bottom_block_not_found', [(0x100, B), (0x200, B), (0x100, T)], 1, integrity=1, reports=1),
        c('bottom_block_inconsistent', [(0x100, B), (0x200, B), (0x100, T)], 1, integrity=0, reports=2),
        c('null', [(0x100, T), (0x200, T)], address=0),
        c('inside_a_block', [(0x100, T), (0x200, T)], address=BASE + 0x180, integrity=1, reports=1),
        c('below_range', [(0x100, T), (0x200, T)], address=BASE - 0x10, integrity=1, reports=2),
        c('at_limit', [(0x100, T), (0x200, T)], address=BASE + 0x300, integrity=0, reports=3),
        c('outside_but_found', [(0x100, B), (0x200, T), (0x100, T)], 2, heap_limit=BASE + 0x300, reports=1),
        c('counters_wrap', [(0x100, T), (0x200, T), (0x100, T)], 1, count=0, available=-0x100),
    ]


def check_cases():
    d = lambda i: f'desc+{DESC * i}'  # noqa: E731
    term = f'heap+{TERMINATOR}'
    mixed = [(0x100, B), (0x200, F), (0x80, T), (0x100, F), (0x40, B), (0x300, F), (0x10, T)]
    return [
        check_case('not_set_up', mixed, initialized=False, extra=[('heap+0', 0xa8, 0)]),
        check_case('mixed', mixed),
        check_case('all_free', [(0x1000, F)]),
        check_case('all_in_use', [(0x100, B), (0x100, T), (0x100, B)]),
        check_case('long_chains', [(0x10, B)] * 5 + [(0x10, T)] * 6 + [(0x20, F)]),
        check_case('zero_size_block', [(0x100, B), (0, B), (0x100, F)]),
        check_case('empty_range', mixed, extra=[('heap+0', 0xa8, BASE)]),
        check_case('empty_range_otherwise_sound', [(0, B)]),
        check_case('range_reversed', mixed, extra=[('heap+0', 0xa0, BASE + 0x2000)]),
        check_case('lowest_zero', mixed, extra=[('heap+0', 0xa0, 0)]),
        check_case('limit_zero', mixed, extra=[('heap+0', 0xa8, 0)]),
        check_case('terminator_geo_prev_null', mixed, extra=[(term, 8, 0)]),
        check_case('terminator_geo_next_null', mixed, extra=[(term, 16, 0)]),
        check_case('terminator_list_prev_null', mixed, extra=[(term, 0x18, 0)]),
        check_case('terminator_list_next_null', mixed, extra=[(term, 0x20, 0)]),
        check_case('terminator_not_at_limit', mixed, extra=[(term, 0, BASE + 0x10000)]),
        check_case('bottom_chain_loops', [(0x10, B)] * 4, extra=[(d(3), 0x20, d(1))]),
        check_case('bottom_chain_loops_on_itself', [(0x10, B)] * 2, extra=[(d(1), 0x20, d(1))]),
        check_case('top_chain_loops', [(0x10, T)] * 5, extra=[(d(0), 0x20, d(4))]),
        check_case('address_chain_loops', [(0x10, B)] * 5, extra=[(d(4), 16, d(2))]),
        check_case('address_chain_back_loops', [(0x10, B)] * 5, extra=[(d(0), 8, d(3))]),
        check_case('free_list_loops', [(0x10, F), (0x10, B)] * 3, extra=[(d(4), 0x20, d(0))]),
        check_case('free_list_back_loops', [(0x10, F), (0x10, B)] * 3, extra=[(d(0), 0x18, d(4))]),
        check_case('bottom_block_marked_free', mixed, extra=[(d(4), 0x18, term)]),
        check_case('top_block_marked_free', mixed, extra=[(d(6), 0x18, d(5))]),
        check_case('next_does_not_link_back', mixed, extra=[(d(3), 8, d(1))]),
        check_case('prev_does_not_link_back', mixed, extra=[(d(1), 16, d(3))]),
        check_case('address_falls', mixed, extra=[(d(3), 0, BASE + 0x200)]),
        check_case('address_past_limit', mixed, extra=[(d(6), 0, BASE + 0x1000)]),
        check_case('first_address_any', mixed, extra=[(d(0), 0, 0)]),
        check_case('free_next_does_not_link_back', mixed, extra=[(d(5), 0x18, d(1))]),
        check_case('free_prev_does_not_link_back', mixed, extra=[(d(1), 0x20, d(5))]),
        check_case('free_list_out_of_order', [(0x100, F), (0x10, B), (0x100, F), (0x10, B), (0x100, F)],
                   extra=[(d(0), 0x20, d(4)), (d(4), 0x18, d(0)), (d(4), 0x20, d(2)), (d(2), 0x18, d(4)),
                          (d(2), 0x20, term), (term, 0x18, d(2))]),
        check_case('free_zero_size_block', [(0x100, B), (0, F), (0x100, F)]),
        check_case('free_block_at_limit', [(0x100, B), (0, F)]),
        # The first block's back links are checked only by the prev tests: point them at an
        # outside descriptor (in `spare`) whose own back link reaches the terminator.
        check_case('first_geo_prev_outside', mixed,
                   extra=[(d(0), 8, 'spare+0'), ('spare+0', 0, BASE - 0x10), ('spare+0', 8, term),
                          ('spare+0', 16, d(1))]),
        check_case('first_free_prev_outside', mixed,
                   extra=[(d(1), 0x18, 'spare+0'), ('spare+0', 0, BASE), ('spare+0', 0x18, term),
                          ('spare+0', 0x20, term)]),
        check_case('first_free_prev_outside_same_address', mixed,
                   extra=[(d(1), 0x18, 'spare+0'), ('spare+0', 0, BASE + 0x100), ('spare+0', 0x18, term),
                          ('spare+0', 0x20, d(1))]),
        check_case('first_free_prev_outside_below', mixed,
                   extra=[(d(1), 0x18, 'spare+0'), ('spare+0', 0, BASE + 0xf0), ('spare+0', 0x18, term),
                          ('spare+0', 0x20, d(1))]),
    ]


def set_up_case(address, cid, offset, memory_at, size, spare=True, set_up=True, tree=False):
    """The heap at `offset` into its buffer; for set-up, memory at `memory_at` of `size` bytes; for
    tear-down, a heap that is (or is not) set up, with a chunk tree or none."""
    h = offset
    memory = [{'addr': f'buf:heap+{k}', 'bytes': '6b' * 8} for k in range(0, 0xd0, 8)]
    stubs = []
    if address == INITIALIZE:
        args = {'rdi': f'buf:heap+{h}', 'rsi': 'buf:backing', 'rdx': memory_at, 'rcx': size}
        stubs.append({'address': '0x02484c70', 'argc': 1, 'ret': 'buf:spare' if spare else 0})
        returns = 'i8'
    else:
        args = {'rdi': f'buf:heap+{h}'}
        memory.append({'addr': f'buf:heap+{h + 0x67}', 'bytes': '01' if set_up else '00'})
        pool = h + ((-h) & 7)
        if tree:
            memory.append({'addr': f'buf:heap+{pool + 0x58}', 'pointer': 'spare'})
        else:
            memory.append({'addr': f'buf:heap+{pool + 0x58}', 'bytes': u64(0)})
        if set_up and tree:
            stubs.append({'address': RELEASE_CHUNKS, 'argc': 2})
        returns = 'i8'
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': {'heap': {'size': 0xd0}, 'backing': {'size': 8}, 'spare': {'size': DESC, 'bytes': '5a' * DESC}},
            'memory': memory, 'stubs': stubs, 'imports': []}


def initialize_cases():
    c = lambda cid, off, at, size, **kw: set_up_case(INITIALIZE, cid, off, at, size, **kw)  # noqa: E731
    return [
        c('aligned', 0, BASE, 0x10000),
        c('heap_offset_1', 1, BASE, 0x10000),
        c('heap_offset_4', 4, BASE, 0x10000),
        c('heap_offset_7', 7, BASE, 0x10000),
        c('heap_offset_8', 8, BASE, 0x10000),
        c('memory_misaligned', 0, BASE + 3, 0x10000),
        c('memory_misaligned_size_odd', 0, BASE + 0xf, 0x1234),
        c('size_under_16', 0, BASE, 0xf),
        c('size_zero', 0, BASE, 0),
        c('size_below_padding', 0, BASE + 1, 4),
        c('memory_low_word_wraps', 0, 0x1fffffff1, 0x100),
        c('null_memory', 0, 0, 0x1000),
        c('no_descriptor', 0, BASE, 0x10000, spare=False),
        c('no_descriptor_offset_5', 5, BASE + 8, 0x10000, spare=False),
    ]


def finalize_cases():
    c = lambda cid, off, **kw: set_up_case(FINALIZE, cid, off, 0, 0, **kw)  # noqa: E731
    return [
        c('set_up_no_chunks', 0),
        c('set_up_with_chunks', 0, tree=True),
        c('set_up_with_chunks_offset_3', 3, tree=True),
        c('set_up_offset_7', 7),
        c('not_set_up', 0, set_up=False),
        c('not_set_up_with_chunks', 2, set_up=False, tree=True),
    ]


WHICH = {'free': free_cases, 'allocate': allocate_cases, 'top': top_cases, 'free_top': free_top_cases,
         'check': check_cases, 'initialize': initialize_cases, 'finalize': finalize_cases}


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] not in WHICH):
        sys.exit(__doc__)
    out = sys.argv[1]
    which = sys.argv[2] if len(sys.argv) == 3 else 'free'
    cases = WHICH[which]()
    os.makedirs(out, exist_ok=True)
    for c in cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main()
