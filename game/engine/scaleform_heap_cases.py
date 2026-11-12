#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the Scaleform heap's small-block engine (game/engine/scaleform_heap_bin_*.cpp),
for tools/verify.py run. Each replacement's own generator (scaleform_heap_<name>_cases.py) calls
this one with its function.

usage: scaleform_heap_cases.py OUT_DIR bin_allocate|bin_allocate_aligned|bin_free|bin_reallocate

Each case lays out one small-block page in `mem` (0x2000 bytes, so the page's machine page
boundary is at mem+0x1000): its two headers, its bitset and its blocks, described in units from the
page's start in two halves (below and above the headers and bitset; units a half's blocks leave
over form one allocated block). Allocated blocks are encoded in the bitset as the game's bitset
function writes them; free blocks carry their links (with the length nibbles), their tail copy and,
from three units on, their page; the units inside a free block keep stale codes. The engine (`eng`) holds the bins at +0x18, each a ring in the
order given (address order by default). The page's descriptor is in `pages`, reached through an
entry of the global page table. The engine's own helpers (best fit, aligned best fit, merge, the
bitset's functions) run as original code.
"""
import json
import os
import struct
import sys

BIN_ALLOCATE, BIN_ALIGNED, BIN_FREE, BIN_REALLOCATE = '0x0233a7f0', '0x0233ae40', '0x0233ab20', '0x0233b2f0'
PAGE_TABLE = 0x05994470
SIGNATURE = 0x5fc0
MEM = 0x2000
BOUNDARY = 0x1000
BINS = 0x18
TABLE, INDEX = 5, 3          # the page's page table entry and its index there
OTHER_INDEX = 1              # a second descriptor, for headers that name another page


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def mark(bitset, unit, units):
    """The bitset function's encoding of an allocated block (bitset: list of 32-bit words)."""
    def put(u, code):
        shift = (2 * u) & 0x1e
        bitset[u >> 4] = (bitset[u >> 4] & ~(3 << shift) & 0xffffffff) | ((code << shift) & 0xffffffff)
    if units - 3 < 3 and units >= 3:
        put(unit, 3)
        put(unit + 1, units - 3)
    elif units == 2:
        put(unit, 2)
    elif units <= 1:
        put(unit, 1)
        return
    elif units <= 0x25:
        put(unit, 3)
        put(unit + 1, 3)
        put(unit + 2, (units - 6) >> 4)
        put(unit + 3, ((units - 6) >> 2) & 3)
        put(unit + 4, (units - 6) & 3)
    else:
        put(unit, 3)
        put(unit + 1, 3)
        put(unit + 2, 3)
        bitset[(2 * unit + 0x25) >> 5] = units
    put(unit + units - 1, 1)


def stale(bitset, unit, units):
    """Codes left inside a free block by older allocations (only a free block's ends are zero)."""
    for u in range(unit + 1, unit + units - 1):
        bitset[u >> 4] |= 2 << ((2 * u) & 0x1e)


class Page:
    """base: the page's base offset in mem (below 0x1000)."""

    def __init__(self, base=0x800):
        self.base = base
        self.start = (base + 15) & ~15
        self.end = (base + 0x1000) & ~15
        below, above = BOUNDARY - self.start, self.end - BOUNDARY
        self.header_below = BOUNDARY - 16 if below > 16 else None
        self.header_above = BOUNDARY if above > 16 else None
        self.bitset = BOUNDARY - 0x50 if below > above else BOUNDARY | 0x10
        reserved_low = min(x for x in (self.header_below, self.header_above, self.bitset) if x is not None)
        reserved_high = max(x + (0x40 if x == self.bitset else 16)
                            for x in (self.header_below, self.header_above, self.bitset) if x is not None)
        self.low_units = (reserved_low - self.start) // 16
        self.high_unit = (reserved_high - self.start) // 16
        self.total_units = (self.end - self.start) // 16

    def address(self, unit):
        return self.start + 16 * unit


def case(address, cid, args, low=(), high=(), base=0x800, orders=None, returns='i64', page_pointer=True,
         header_counts=(2, 5), decoy=None, location_index=INDEX, extra_buffers=None, extra_memory=(),
         reserve=True):
    """low/high: blocks (units, 'f' free or 'u' allocated) from the start of each half.
    orders: {bin: [block keys]} (a key is ('l'|'h', position)) for a bin's ring order, head first.
    decoy: a header at mem+0 naming the other descriptor (whose base is elsewhere).
    reserve: mark the headers' and the bitset's units allocated in the bitset (else they read free).
    Returns (case, blocks), blocks: key -> (offset in mem, units)."""
    page = Page(base)
    memory = []
    words = {}

    def word(off, value):
        words[off] = value

    blocks = {}
    bitset = [0] * 16
    for half, items, first, limit in (('l', low, 0, page.low_units), ('h', high, page.high_unit, page.total_units)):
        unit = first
        for i, (units, state) in enumerate(items):
            blocks[(half, i)] = (page.address(unit), units, state)
            if state == 'u':
                mark(bitset, unit, units)
            else:
                stale(bitset, unit, units)
            unit += units
        assert unit <= limit, (cid, half, unit, limit)
        if unit < limit:
            mark(bitset, unit, limit - unit)
    if reserve:
        mark(bitset, page.low_units, page.high_unit - page.low_units)
    for i, w in enumerate(bitset):
        if w:
            memory.append({'addr': f'buf:mem+{page.bitset + 4 * i}', 'bytes': struct.pack('<I', w).hex()})
    # Bins.
    lists = {}
    for key, (off, units, state) in blocks.items():
        if state == 'f':
            lists.setdefault(min(units - 1, 63), []).append(key)
    for b in lists:
        lists[b].sort(key=lambda k: blocks[k][0])
    lists.update(orders or {})
    mask = 0
    links = {}
    for b, members in lists.items():
        if not members:
            continue
        mask |= 1 << b
        memory.append({'addr': f'buf:eng+{BINS + 8 + 8 * b}', 'pointer': f'mem+{blocks[members[0]][0]}'})
        for j, key in enumerate(members):
            links[key] = (blocks[members[(j + 1) % len(members)]][0], blocks[members[j - 1]][0])
    word_bins = mask
    for key, (off, units, state) in blocks.items():
        if state != 'f':
            continue
        nxt, prv = links[key]
        tail = off + 16 * units - 16
        for where in {off, tail}:
            memory.append({'addr': f'buf:mem+{where}', 'pointer': f'mem+{nxt + (units & 15)}'})
            memory.append({'addr': f'buf:mem+{where + 8}', 'pointer': f'mem+{prv + ((units >> 4) & 15)}'})
        if units >= 3:
            if page_pointer:
                memory.append({'addr': f'buf:mem+{off + 16}', 'pointer': f'pages+{0x20 * INDEX}'})
            else:
                word(off + 16, 0)
    memory.append({'addr': f'buf:eng+{BINS}', 'bytes': u64(word_bins)})
    location = (TABLE | (location_index << 7)) << 32
    for header, count in zip((page.header_below, page.header_above), header_counts):
        if header is not None:
            word(header, SIGNATURE | (count << 16) | location)
    if decoy:
        word(0, SIGNATURE | ((TABLE | (OTHER_INDEX << 7)) << 32))
    for off, value in words.items():
        memory.append({'addr': f'buf:mem+{off}', 'bytes': u64(value)})
    # The page table entry and the descriptors.
    memory += [{'addr': hex(PAGE_TABLE + 16 * TABLE), 'pointer': 'pages'},
               {'addr': hex(PAGE_TABLE + 16 * TABLE + 8), 'bytes': u64(0xff)},
               {'addr': f'buf:pages+{0x20 * INDEX + 0x10}', 'pointer': 'heap'},
               {'addr': f'buf:pages+{0x20 * INDEX + 0x18}', 'pointer': f'mem+{base}'},
               {'addr': f'buf:pages+{0x20 * OTHER_INDEX + 0x18}', 'pointer': 'heap'}]
    memory += list(extra_memory)
    buffers = {'mem': {'size': MEM}, 'eng': {'size': 0x260}, 'pages': {'size': 0x100}, 'heap': {'size': 0x100},
               'view': {'size': 0x38}, 'out': {'size': 8}}
    buffers.update(extra_buffers or {})
    out = {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args, 'buffers': buffers,
           'memory': memory, 'stubs': [], 'imports': []}
    return out, {k: (v[0], v[1]) for k, v in blocks.items()}, page


BINS_ARG = f'buf:eng+{BINS}'
PAGE_ARG = f'buf:pages+{0x20 * INDEX}'


def bin_allocate_cases():
    def c(cid, request, **kw):
        return case(BIN_ALLOCATE, cid, {'rdi': BINS_ARG, 'rsi': request, 'rdx': 'buf:view'}, **kw)[0]
    return [
        c('exact_two_units_lookup_below', 0x20, low=[(2, 'f'), (1, 'u')]),
        c('exact_one_unit', 0x10, low=[(1, 'u'), (1, 'f'), (1, 'u')]),
        c('exact_high_half_lookup_above', 0x20, high=[(1, 'u'), (2, 'f'), (1, 'u')]),
        c('page_pointer', 0x40, low=[(4, 'f'), (1, 'u')]),
        c('page_pointer_null_lookup', 0x40, low=[(4, 'f'), (1, 'u')], page_pointer=False),
        c('rest_six_units', 0x40, low=[(10, 'f'), (1, 'u')]),
        c('rest_joins_bin', 0x40, low=[(2, 'f'), (1, 'u'), (6, 'f'), (1, 'u')]),
        c('rest_one_unit', 0x30, low=[(4, 'f'), (1, 'u')]),
        c('rest_two_units', 0x20, low=[(4, 'f'), (1, 'u')]),
        c('rest_to_last_bin', 0x20, low=[(100, 'f'), (1, 'u')]),
        c('rest_high_nibble', 0x10, low=[(40, 'f'), (1, 'u')]),
        c('best_fit_larger_bin', 0x30, low=[(2, 'f'), (1, 'u'), (8, 'f'), (1, 'u'), (20, 'f'), (1, 'u')]),
        c('head_of_bin_taken', 0x30, low=[(3, 'f'), (1, 'u'), (3, 'f'), (1, 'u'), (3, 'f'), (1, 'u')],
          orders={2: [('l', 4), ('l', 0), ('l', 2)]}),
        c('nothing_free', 0x30, low=[(3, 'u')]),
        c('nothing_large_enough', 0x80, low=[(3, 'f'), (1, 'u')]),
        c('largest_request', 0x200, low=[(40, 'f'), (1, 'u')]),
        c('request_six_units', 0x60, low=[(7, 'f'), (1, 'u')]),
        c('request_three_units', 0x30, low=[(5, 'f'), (1, 'u')]),
        c('bitset_below', 0x40, base=0x200, low=[(1, 'u'), (9, 'f'), (1, 'u')]),
        c('bitset_below_high_half', 0x20, base=0x200, high=[(2, 'f'), (1, 'u')]),
        c('unaligned_base', 0x20, base=0xa08, low=[(1, 'u'), (5, 'f'), (1, 'u')]),
        c('decoy_header_out_of_range', 0x20, low=[(2, 'f'), (1, 'u')], decoy=True),
        c('location_index_masked', 0x20, low=[(2, 'f'), (1, 'u')], location_index=INDEX + 0x100),
        c('headers_counted_elsewhere', 0x20, low=[(2, 'f')], header_counts=(0xffff, 0)),
    ]


def bin_allocate_aligned_cases():
    def c(cid, request, alignment, **kw):
        return case(BIN_ALIGNED, cid, {'rdi': BINS_ARG, 'rsi': request, 'rdx': alignment, 'rcx': 'buf:view'},
                    **kw)[0]
    # The page's start is mem+0x800: unit u is aligned to 2^k when u is a multiple of 2^(k-4).
    return [
        c('aligned_exact', 0x40, 0x40, low=[(4, 'f'), (1, 'u')]),
        c('aligned_rest', 0x40, 0x40, low=[(10, 'f'), (1, 'u')]),
        c('front_only', 0x40, 0x40, low=[(2, 'u'), (6, 'f'), (1, 'u')]),
        c('front_and_rest', 0x40, 0x40, low=[(1, 'u'), (20, 'f'), (1, 'u')]),
        c('front_one_unit', 0x20, 0x20, low=[(1, 'u'), (3, 'f'), (1, 'u')]),
        c('alignment_0x100', 0x40, 0x100, low=[(3, 'u'), (40, 'f'), (1, 'u')]),
        c('alignment_16', 0x30, 0x10, low=[(1, 'u'), (5, 'f'), (1, 'u')]),
        c('page_pointer_null', 0x40, 0x40, low=[(1, 'u'), (20, 'f'), (1, 'u')], page_pointer=False),
        c('high_half', 0x40, 0x40, high=[(1, 'u'), (20, 'f'), (1, 'u')]),
        c('nothing_fits', 0x80, 0x100, low=[(1, 'u'), (9, 'f'), (1, 'u')]),
        c('nothing_free', 0x40, 0x40, low=[(4, 'u')]),
        c('rest_to_last_bin', 0x20, 0x20, low=[(1, 'u'), (100, 'f'), (1, 'u')]),
        c('bitset_below', 0x40, 0x40, base=0x200, low=[(1, 'u'), (12, 'f'), (1, 'u')]),
        c('unaligned_base', 0x40, 0x40, base=0xa08, low=[(1, 'u'), (12, 'f'), (1, 'u')]),
        c('bins_nonempty', 0x40, 0x40, low=[(1, 'u'), (20, 'f'), (1, 'u'), (3, 'f'), (1, 'u'), (16, 'f'),
                                            (1, 'u')]),
    ]


def bin_free_cases():
    def c(cid, half, position, **kw):
        out, blocks, page = case(BIN_FREE, cid, {}, returns='void', **kw)
        out['args'] = {'rdi': BINS_ARG, 'rsi': PAGE_ARG, 'rdx': f'buf:mem+{blocks[(half, position)][0]}',
                       'rcx': 'buf:view', 'r8': 'buf:out'}
        return out
    return [
        c('neighbours_allocated', 'l', 1, low=[(1, 'u'), (4, 'u'), (1, 'u')]),
        c('neighbours_allocated_bin_nonempty', 'l', 1, low=[(1, 'u'), (4, 'u'), (1, 'u'), (4, 'f'), (1, 'u')]),
        c('one_unit', 'l', 1, low=[(1, 'u'), (1, 'u'), (1, 'u')]),
        c('two_units', 'l', 1, low=[(1, 'u'), (2, 'u'), (1, 'u')]),
        c('six_units', 'l', 1, low=[(1, 'u'), (6, 'u'), (1, 'u')]),
        c('thirty_seven_units', 'l', 1, low=[(1, 'u'), (0x25, 'u'), (1, 'u')]),
        c('long_block', 'l', 1, low=[(1, 'u'), (0x50, 'u'), (1, 'u')]),
        c('first_unit_of_page', 'l', 0, low=[(3, 'u'), (1, 'u')]),
        c('previous_free_merges', 'l', 1, low=[(3, 'f'), (2, 'u'), (1, 'u')]),
        c('next_free_merges', 'l', 1, low=[(1, 'u'), (2, 'u'), (5, 'f'), (1, 'u')]),
        c('both_free_merge', 'l', 1, low=[(4, 'f'), (2, 'u'), (5, 'f'), (1, 'u')]),
        c('first_unit_next_free', 'l', 0, low=[(3, 'u'), (4, 'f'), (1, 'u')]),
        c('last_block_of_page', 'h', 1, high=[(1, 'u'), (0x7b0 // 16 - 1, 'u')]),
        c('last_block_previous_free', 'h', 1, high=[(4, 'f'), (0x7b0 // 16 - 4, 'u')]),
        c('high_half', 'h', 1, high=[(1, 'u'), (3, 'u'), (1, 'u')]),
        c('below_headers', 'l', 1, low=[(1, 'u'), (126, 'u')]),
        c('bitset_below', 'l', 1, base=0x200, low=[(1, 'u'), (5, 'u'), (1, 'u')]),
        c('unaligned_base', 'l', 1, base=0xa08, low=[(1, 'u'), (5, 'u'), (1, 'u')]),
    ]


def bin_reallocate_cases():
    def c(cid, half, position, new, **kw):
        out, blocks, page = case(BIN_REALLOCATE, cid, {}, **kw)
        out['args'] = {'rdi': BINS_ARG, 'rsi': PAGE_ARG, 'rdx': f'buf:mem+{blocks[(half, position)][0]}',
                       'rcx': new, 'r8': 'buf:out', 'r9': 'buf:view'}
        return out
    return [
        c('same_length', 'l', 1, 0x40, low=[(1, 'u'), (4, 'u'), (1, 'u')]),
        c('same_length_next_free', 'l', 1, 0x40, low=[(1, 'u'), (4, 'u'), (5, 'f'), (1, 'u'), (5, 'f'), (1, 'u')],
          orders={4: [('l', 4), ('l', 2)]}),
        c('shrink_next_allocated', 'l', 1, 0x20, low=[(1, 'u'), (6, 'u'), (1, 'u')]),
        c('shrink_by_one_unit', 'l', 1, 0x50, low=[(1, 'u'), (6, 'u'), (1, 'u')]),
        c('shrink_next_free_alone', 'l', 1, 0x20, low=[(1, 'u'), (6, 'u'), (5, 'f'), (1, 'u')]),
        c('shrink_next_free_head', 'l', 1, 0x20, low=[(1, 'u'), (6, 'u'), (5, 'f'), (1, 'u'), (5, 'f'), (1, 'u')]),
        c('shrink_next_free_not_head', 'l', 1, 0x20, low=[(1, 'u'), (6, 'u'), (5, 'f'), (1, 'u'), (5, 'f'),
                                                         (1, 'u')], orders={4: [('l', 4), ('l', 2)]}),
        c('shrink_next_free_three_in_bin', 'l', 1, 0x20,
          low=[(1, 'u'), (6, 'u'), (5, 'f'), (1, 'u'), (5, 'f'), (1, 'u'), (5, 'f'), (1, 'u')],
          orders={4: [('l', 4), ('l', 2), ('l', 6)]}),
        # The bin's head moves to the block's previous one, not its next.
        c('shrink_next_free_head_of_three', 'l', 1, 0x20,
          low=[(1, 'u'), (6, 'u'), (5, 'f'), (1, 'u'), (5, 'f'), (1, 'u'), (5, 'f'), (1, 'u')],
          orders={4: [('l', 2), ('l', 4), ('l', 6)]}),
        c('grow_next_head_of_three', 'l', 1, 0x60,
          low=[(1, 'u'), (4, 'u'), (10, 'f'), (1, 'u'), (10, 'f'), (1, 'u'), (10, 'f'), (1, 'u')],
          orders={9: [('l', 2), ('l', 4), ('l', 6)]}),
        c('shrink_below_headers', 'l', 1, 0x20, low=[(1, 'u'), (126, 'u')]),
        c('shrink_page_end', 'h', 1, 0x20, high=[(1, 'u'), (0x7b0 // 16 - 1, 'u')]),
        c('shrink_long_block', 'l', 1, 0x100, low=[(1, 'u'), (0x50, 'u'), (1, 'u')]),
        c('grow_next_allocated', 'l', 1, 0x80, low=[(1, 'u'), (4, 'u'), (1, 'u')]),
        c('grow_next_too_short', 'l', 1, 0x100, low=[(1, 'u'), (4, 'u'), (3, 'f'), (1, 'u')]),
        c('grow_next_exact', 'l', 1, 0x70, low=[(1, 'u'), (4, 'u'), (3, 'f'), (1, 'u')]),
        c('grow_with_rest', 'l', 1, 0x60, low=[(1, 'u'), (4, 'u'), (10, 'f'), (1, 'u')]),
        c('grow_with_rest_bin_nonempty', 'l', 1, 0x60, low=[(1, 'u'), (4, 'u'), (10, 'f'), (1, 'u'), (8, 'f'),
                                                           (1, 'u')]),
        c('grow_next_head_of_two', 'l', 1, 0x60, low=[(1, 'u'), (4, 'u'), (10, 'f'), (1, 'u'), (10, 'f'),
                                                     (1, 'u')]),
        c('grow_next_not_head', 'l', 1, 0x60, low=[(1, 'u'), (4, 'u'), (10, 'f'), (1, 'u'), (10, 'f'), (1, 'u')],
          orders={9: [('l', 4), ('l', 2)]}),
        c('grow_below_headers', 'l', 1, 0x7f0, low=[(1, 'u'), (126, 'u')]),
        c('grow_page_end', 'h', 1, 0x7b0, high=[(1, 'u'), (0x7b0 // 16 - 1, 'u')]),
        c('grow_high_half', 'h', 1, 0x60, high=[(1, 'u'), (2, 'u'), (8, 'f'), (1, 'u')]),
        c('grow_to_long_block', 'l', 1, 0x400, low=[(1, 'u'), (4, 'u'), (70, 'f'), (1, 'u')]),
        c('bitset_below_grow', 'l', 1, 0x60, base=0x200, low=[(1, 'u'), (2, 'u'), (8, 'f'), (1, 'u')]),
        # Below the boundary with the bitset there, the free block's units past the bitset do not count.
        c('bitset_below_limit', 'l', 1, 0x40, base=0x200, low=[(1, 'u'), (page_units(0x200) - 1, 'u')]),
        # With the headers' and bitset's units reading free, only the limit keeps the block from
        # growing into the bitset (whose first word reads as a one-unit free block).
        c('bitset_below_limit_unmarked', 'l', 1, 16 * page_units(0x200), base=0x200, reserve=False,
          low=[(1, 'u'), (page_units(0x200) - 1, 'u')]),
        c('grow_below_headers_unmarked', 'l', 1, 0x7f0, reserve=False, low=[(1, 'u'), (126, 'u')]),
        c('unaligned_base_shrink', 'l', 1, 0x20, base=0xa08, low=[(1, 'u'), (6, 'u'), (4, 'f'), (1, 'u')]),
    ]


def page_units(base):
    return Page(base).low_units


WHICH = {'bin_allocate': bin_allocate_cases, 'bin_allocate_aligned': bin_allocate_aligned_cases,
         'bin_free': bin_free_cases, 'bin_reallocate': bin_reallocate_cases}


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
