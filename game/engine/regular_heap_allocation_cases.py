#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the regular heap's set-up and allocations (game/engine/regular_heap_initialize.cpp,
regular_heap_allocate.cpp, regular_heap_allocate_aligned.cpp), for tools/verify.py run. Each
replacement's own generator calls this one with its function.

usage: regular_heap_allocation_cases.py OUT_DIR initialize|allocate|allocate_aligned

The allocations use the free cases' layouts (regular_heap_cases.Layout: the heap's state with its
64 list sentinels, and a chain of blocks in `arena`, which starts on a page). The set-up gets
memory inside `arena` at a given offset, the heap object filled with a pattern so that every field
it writes shows; the C library's memset is scripted (it writes nothing).
"""
import json
import os
import sys

from regular_heap_cases import ARENA_START, Layout, u64

INITIALIZE, ALLOCATE, ALIGNED = '0x02485500', '0x02485730', '0x02485a60'
HEAP_OBJECT = 0x440
PATTERN = 'c3' * HEAP_OBJECT
U, F = 'used', 'free'


def initialize_case(cid, offset, size, refused=False):
    imports = [{'name': 'memset', 'argc': 3, 'ret': 'buf:heap'}] if refused else []
    return {'schema': 1, 'address': INITIALIZE, 'id': cid, 'returns': 'i8',
            'args': {'rdi': 'buf:heap', 'rsi': f'buf:arena+{offset}', 'rdx': size},
            'buffers': {'heap': {'size': HEAP_OBJECT, 'bytes': PATTERN}, 'arena': {'size': 0x2000}},
            'memory': [], 'stubs': [], 'imports': imports}


def initialize_cases():
    return [
        initialize_case('aligned_memory', 0, 0x400),
        initialize_case('offset_1', 1, 0x400),
        initialize_case('offset_8', 8, 0x400),
        initialize_case('offset_9', 9, 0x400),
        initialize_case('offset_15', 15, 0x400),
        initialize_case('size_unaligned_end', 0, 0x40f),
        initialize_case('range_exactly_0x40', 8, 0x58),
        initialize_case('range_0x30_refused', 8, 0x38, refused=True),
        initialize_case('end_rounds_down_refused', 0, 0x57, refused=True),
        initialize_case('end_rounds_down_passes', 0, 0x5f),
        initialize_case('offset_pushes_below_0x40', 9, 0x48, refused=True),
        initialize_case('size_zero_aligned_refused', 0x10, 8, refused=True),
        initialize_case('large_range', 0, 0x2000),
        initialize_case('range_0x40_from_aligned', 0, 0x58),
        initialize_case('range_0x80_list_7', 0, 0x98),
    ]


def allocation_case(address, cid, blocks, args, returns='i64', **kw):
    layout = Layout(blocks, **kw)
    layout.memory += [{'addr': 'buf:heap+8', 'pointer': f'arena+{ARENA_START + 0x10}'},
                      {'addr': 'buf:heap+16', 'pointer': f'arena+{layout.end}'}]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns,
            'args': dict({'rdi': 'buf:heap'}, **args),
            'buffers': {'heap': {'size': HEAP_OBJECT}, 'arena': {'size': max(layout.arena_size, 0x100)},
                        'others': {'size': 0x100}},
            'memory': layout.memory, 'stubs': [], 'imports': []}


def allocate_cases():
    def c(cid, blocks, request, **kw):
        return allocation_case(ALLOCATE, cid, blocks, {'rsi': request}, **kw)
    return [
        c('larger_list_split', [(0x20, U), (0x100, F), (0x20, U)], 0x20),
        c('larger_list_lowest', [(0x20, U), (0x400, F), (0x20, U), (0x100, F), (0x20, U)], 0x20),
        c('larger_list_whole_block', [(0x20, U), (0x80, F), (0x20, U)], 0x78),
        c('larger_list_rest_header_only', [(0x20, U), (0x80, F), (0x20, U)], 0x68),
        c('larger_list_keeps_others', [(0x20, U), (0x100, F), (0x20, U), (0x110, F), (0x20, U)], 0x20),
        c('rest_joins_nonempty_list', [(0x20, U), (0x100, F), (0x20, U), (0xc0, F), (0x20, U)], 0x20),
        c('own_list_exact', [(0x20, U), (0x40, F), (0x20, U)], 0x38),
        c('own_list_walk_skips_small', [(0x20, U), (0x40, F), (0x20, U), (0x70, F), (0x20, U)], 0x58),
        c('own_list_walk_second_whole', [(0x20, U), (0x40, F), (0x20, U), (0x60, F), (0x20, U)], 0x58),
        c('own_list_none_fits', [(0x20, U), (0x40, F), (0x20, U), (0x50, F), (0x20, U)], 0x58),
        c('no_list_at_all', [(0x20, U), (0x30, F), (0x20, U)], 0x40),
        c('own_bit_clear_lower_lists', [(0x20, U), (0x20, F), (0x20, U)], 0x30),
        c('available_short', [(0x20, U), (0x100, F), (0x20, U)], 0x40, available=0x3f),
        c('available_exact', [(0x20, U), (0x100, F), (0x20, U)], 0x40, available=0x40),
        c('request_zero', [(0x20, U), (0x100, F), (0x20, U)], 0),
        c('request_0x18', [(0x20, U), (0x40, F), (0x20, U)], 0x18),
        c('request_0x19', [(0x20, U), (0x40, F), (0x20, U)], 0x19),
        c('request_0x28', [(0x20, U), (0x40, F), (0x20, U)], 0x28),
        c('request_0x29', [(0x20, U), (0x80, F), (0x20, U)], 0x29),
        c('first_block_of_heap', [(0x200, F), (0x20, U)], 0x100),
        c('large_request', [(0x20, U), (0x4000, F), (0x20, U)], 0x1000),
        c('extra_mask_bits_kept', [(0x20, U), (0x100, F), (0x20, U)], 0x20, extra_mask=1 << 3),
        c('count_and_available', [(0x20, U), (0x100, F), (0x20, U)], 0x20, available=0x12345, count=0),
    ]


def aligned_cases():
    def c(cid, blocks, request, alignment, **kw):
        return allocation_case(ALIGNED, cid, blocks, {'rsi': request, 'rdx': alignment}, **kw)
    # The arena starts on a page; ARENA_START is 0x10, so a block at arena offset o has its
    # payload at o + 0x10.
    return [
        c('not_power_of_two', [(0x20, U), (0x400, F), (0x20, U)], 0x40, 0x30),
        c('alignment_zero_plain', [(0x20, U), (0x400, F), (0x20, U)], 0x40, 0),
        c('alignment_8_plain', [(0x20, U), (0x400, F), (0x20, U)], 0x40, 8),
        c('alignment_16_plain', [(0x20, U), (0x400, F), (0x20, U)], 0x40, 16),
        c('alignment_0x20_not_plain', [(0x10, U), (0x400, F), (0x20, U)], 0x40, 0x20),
        c('available_short', [(0x20, U), (0x400, F), (0x20, U)], 0x40, 0x40, available=0x3f),
        c('available_exact', [(0x20, U), (0x400, F), (0x20, U)], 0x40, 0x40, available=0x40),
        # The plain allocation takes list 7 for a 0x70 block; an aligned one would skip to 9.
        c('alignment_16_plain_list_choice', [(0x20, U), (0xf0, F), (0x20, U), (0x200, F), (0x20, U)], 0x68, 16),
        # Block at 0x30: payload 0x40, on 0x40 already.
        c('fast_already_aligned_split', [(0x20, U), (0x400, F), (0x20, U)], 0x40, 0x40),
        c('search_already_aligned_whole', [(0x20, U), (0x100, F), (0x20, U)], 0xf8, 0x40),
        # Payload already on the alignment: what is left is only a header (kept out of the
        # lists), or a block that joins a non-empty list.
        c('aligned_rest_header_only', [(0x20, U), (0x60, F), (0x20, U)], 0x40, 0x40),
        c('aligned_rest_joins_nonempty', [(0x20, U), (0x80, F), (0x20, U), (0x30, F), (0x20, U)], 0x40, 0x40),
        # Block at 0x10: payload 0x20, not on 0x40: a 0x20 front block stays free.
        c('fast_front_and_rest', [(0x400, F), (0x20, U)], 0x40, 0x40),
        c('fast_front_header_only', [(0x10, U), (0x400, F), (0x20, U)], 0x40, 0x40),
        c('fast_aligned_rest_into_empty_list', [(0x20, U), (0x20, F), (0x20, U), (0x400, F), (0x20, U)], 0x40, 0x40),
        c('fast_lowest_larger_list', [(0x400, F), (0x20, U), (0x800, F), (0x20, U)], 0x40, 0x80),
        c('fast_list_keeps_others', [(0x400, F), (0x20, U), (0x500, F), (0x20, U)], 0x40, 0x80),
        # The worst case (alignment plus block size) on a power of two (0x80: list 8) and just
        # below one (0x70: list 7): the unchecked path takes the first list above it.
        c('fast_worst_case_on_power_of_two', [(0x10, U), (0x80, F), (0x20, U), (0x100, F), (0x20, U)], 0x38, 0x40),
        c('fast_worst_case_below_power_of_two', [(0x10, U), (0x80, F), (0x20, U), (0x100, F), (0x20, U)], 0x28, 0x40),
        # The unchecked path takes the higher list's block although a lower one would fit.
        c('fast_skips_fitting_lower_block', [(0x10, U), (0x70, F), (0x20, U), (0x400, F), (0x20, U)], 0x48, 0x40),
        # The same at the larger alignments the unchecked path still takes (pins its limit).
        c('fast_skips_fitting_lower_block_0x80', [(0x10, U), (0xf0, F), (0x20, U), (0x800, F), (0x20, U)], 0x48, 0x80),
        c('fast_skips_fitting_lower_block_0x100', [(0x10, U), (0x1f0, F), (0x20, U), (0x800, F), (0x20, U)], 0x48, 0x100),
        c('fast_skips_fitting_lower_block_0x200', [(0x10, U), (0x3f0, F), (0x20, U), (0x800, F), (0x20, U)], 0x48, 0x200),
        # No list above the worst case: the search from the request's own list.
        c('search_first_nonempty_list_fits', [(0x20, U), (0xa0, F), (0x20, U)], 0x40, 0x40),
        # The first block of the list fits: its payload is off the alignment, so a header-only
        # front block stays out of the lists, and nothing is left behind the allocation.
        c('search_first_block_misaligned', [(0x10, U), (0x60, F), (0x20, U), (0xa0, F), (0x20, U)], 0x40, 0x40),
        # A 0x50 block that doesn't fit, then a 0x70 one that does, in the same list (both orders).
        c('search_skips_unfit_block', [(0x10, U), (0x50, F), (0x20, U), (0x70, F), (0x20, U)], 0x48, 0x40),
        c('search_fit_before_unfit_block', [(0x10, U), (0x70, F), (0x20, U), (0x50, F), (0x20, U)], 0x48, 0x40),
        c('search_rest_0x20_pushed', [(0x10, U), (0x80, F), (0x20, U)], 0x48, 0x40),
        c('search_rest_0x30_pushed', [(0x10, U), (0x90, F), (0x20, U)], 0x48, 0x40),
        c('search_next_list', [(0x10, U), (0x70, F), (0x20, U), (0x1f0, F), (0x20, U)], 0x48, 0x100),
        c('search_rest_header_only', [(0x10, U), (0x70, F), (0x20, U)], 0x48, 0x40),
        c('search_fits_with_room_0x18', [(0x10, U), (0x30, F), (0x20, U)], 0x10, 0x40),
        # Non-empty lists, and no block in them fits: the search gives up (null).
        c('search_none', [(0x10, U), (0x50, F), (0x20, U)], 0x48, 0x40),
        # The block's header sits on the alignment, its payload doesn't: the aligned payload is
        # the next one, which leaves no room.
        c('search_header_on_alignment_none', [(0x30, U), (0x60, F), (0x20, U)], 0x48, 0x40),
        c('search_none_two_lists', [(0x10, U), (0x50, F), (0x20, U), (0x90, F), (0x20, U)], 0x48, 0x100),
        c('alignment_0x400_searches', [(0x20, U), (0x2000, F), (0x20, U)], 0x40, 0x400),
        c('alignment_0x400_two_lists', [(0x10, U), (0x420, F), (0x20, U), (0x900, F), (0x20, U)], 0x100, 0x400),
        # 0x400 is past the unchecked limit: the search takes the lower block on the alignment
        # where the unchecked path would take the higher list's first block.
        c('alignment_0x400_searches_lower_block', [(0x3e0, U), (0x80, F), (0x20, U), (0x800, F), (0x20, U)], 0x40, 0x400),
        c('alignment_0x1000', [(0x20, U), (0x3000, F), (0x20, U)], 0x40, 0x1000),
        c('request_zero', [(0x400, F), (0x20, U)], 0, 0x40),
        # The block size's rounding step: a request of 0x18 needs a 0x20 block (a header-only
        # rest is left), 0x19 a 0x30 one (the whole block).
        c('request_0x18_block_0x20', [(0x20, U), (0x30, F), (0x20, U)], 0x18, 0x20),
        c('request_0x19_block_0x30', [(0x20, U), (0x30, F), (0x20, U)], 0x19, 0x20),
        c('no_lists', [(0x20, U)], 0x40, 0x40),
    ]


WHICH = {'initialize': initialize_cases, 'allocate': allocate_cases, 'allocate_aligned': aligned_cases}


def main(argv):
    if len(argv) != 2 or argv[1] not in WHICH:
        sys.exit(__doc__)
    out, which = argv
    os.makedirs(out, exist_ok=True)
    cases = WHICH[which]()
    for case in cases:
        with open(os.path.join(out, f"{case['id']}.json"), 'w') as f:
            json.dump(case, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
