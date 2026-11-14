#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the regular and robust heaps' block checks (game/engine/regular_heap_check_block.cpp,
robust_heap_check_block.cpp), for tools/verify.py run.

usage: heap_check_block_cases.py OUT_DIR regular|robust

Each case lays out a sound heap with the free-path generators' layouts (regular_heap_cases.py,
robust_heap_cases.py) and checks the payload of one block, after `extra` writes that break one
word: (buffer offset, value), the value a number or a 'buffer+offset' pointer. The checks only
read; what they return (the broken word's address, or 0) is compared, with the error reports'
calls.
"""
import json
import os
import struct
import sys

import regular_heap_cases as regular
import robust_heap_cases as robust

REGULAR_CHECK, ROBUST_CHECK, FATAL_ERROR = '0x02485e20', '0x02486080', '0x024b55b0'


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def writes(extra):
    out = []
    for where, value in extra:
        if isinstance(value, str):
            out.append({'addr': f'buf:{where}', 'pointer': value})
        else:
            out.append({'addr': f'buf:{where}', 'bytes': u64(value)})
    return out


def regular_case(cid, blocks, index=None, payload=None, extra=(), reports=0):
    """payload: an arena offset instead of block `index`'s payload."""
    layout = regular.Layout(blocks)
    layout.memory += [{'addr': 'buf:heap+1064', 'pointer': f'arena+{regular.ARENA_START}'},
                      {'addr': 'buf:heap+1072', 'pointer': f'arena+{layout.end}'}]
    if payload is None:
        payload = layout.offsets[index] + 0x10
    return {'schema': 1, 'address': REGULAR_CHECK, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:heap', 'rsi': f'buf:arena+{payload}'},
            'buffers': {'heap': {'size': 0x440}, 'arena': {'size': layout.arena_size}, 'others': {'size': 0x100}},
            'memory': layout.memory + writes(extra),
            'stubs': [{'address': FATAL_ERROR, 'argc': 3}] * reports, 'imports': []}


def robust_case(cid, blocks, index=None, payload=None, extra=(), reports=0, highest=None):
    """highest: the payload range's top (an arena offset; the closing block by default)."""
    layout = robust.Layout(blocks)
    top = layout.end if highest is None else highest
    layout.memory += [{'addr': 'buf:heap+40', 'pointer': f'arena+{robust.ARENA_START}'},
                      {'addr': 'buf:heap+48', 'pointer': f'arena+{top}'}]
    if payload is None:
        payload = layout.offsets[index] + 0x10
    return {'schema': 1, 'address': ROBUST_CHECK, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:heap', 'rsi': f'buf:arena+{payload}'},
            'buffers': {'heap': {'size': 0x38}, 'bins': {'size': 0x1f0}, 'arena': {'size': layout.arena_size},
                        'others': {'size': 0x100}},
            'memory': layout.memory + writes(extra),
            'stubs': [{'address': FATAL_ERROR, 'argc': 3}] * reports, 'imports': []}


U, F = 'used', 'free'


def regular_cases():
    S = regular.ARENA_START
    mixed = [(0x20, U), (0x60, F), (0x40, U), (0x10, F), (0x20, U), (0x100, F), (0x30, U)]
    # Offsets of mixed's blocks.
    o = [S]
    for size, _ in mixed:
        o.append(o[-1] + size)
    sentinel = lambda k: f'heap+{0x18 + 16 * k}'  # noqa: E731
    c = regular_case
    return [
        c('used_sound', mixed, 0),
        c('used_before_closing_block', mixed, 6),
        c('free_sound', mixed, 1),
        c('free_large_sound', mixed, 5),
        c('free_header_only', mixed, 3),
        c('free_shared_list', [(0x20, U), (0x60, F), (0x20, U), (0x50, F), (0x20, U)], 3),
        c('misaligned_payload', mixed, payload=o[2] + 0x18, reports=1),
        c('payload_below', mixed, 0, extra=[('heap+1064', f'arena+{o[0] + 0x20}')], reports=1),
        c('payload_above', mixed, payload=o[7] + 0x10, reports=1),
        c('payload_at_closing_block', mixed, payload=o[7]),
        c('next_bit_2', mixed, 0, extra=[(f'arena+{o[0] + 8}', f'arena+{o[1] + 4 + 2 + 1}')]),
        c('next_bit_3', mixed, 0, extra=[(f'arena+{o[0] + 8}', f'arena+{o[1] + 8}')]),
        c('next_below', mixed, 2, extra=[(f'arena+{o[2] + 8}', 0x10 | 1)]),
        c('next_above', mixed, 2, extra=[(f'arena+{o[2] + 8}', f'arena+{o[7] + 0x10 + 1}')]),
        c('next_is_itself', mixed, 2, extra=[(f'arena+{o[2] + 8}', f'arena+{o[2] + 1}')]),
        c('next_before', mixed, 2, extra=[(f'arena+{o[2] + 8}', f'arena+{o[0] + 3}')]),
        c('next_next_broken', mixed, 0, extra=[(f'arena+{o[1] + 8}', f'arena+{o[0]}')]),
        c('next_next_bit_2', mixed, 0, extra=[(f'arena+{o[1] + 8}', f'arena+{o[2] + 4 + 2}')]),
        c('closing_next_not_checked', mixed, 6, extra=[(f'arena+{o[7] + 8}', 4)]),
        c('used_next_thinks_free', mixed, 0, extra=[(f'arena+{o[1] + 8}', f'arena+{o[2]}')]),
        c('used_followed_by_free_sound', [(0x20, U), (0x40, U), (0x60, F), (0x20, U)], 1),
        c('free_wrong_back_pointer', mixed, 1, extra=[(f'arena+{o[2]}', f'arena+{o[0]}')]),
        c('free_next_thinks_used', mixed, 1, extra=[(f'arena+{o[2] + 8}', f'arena+{o[3] + 3}')]),
        c('free_followed_by_free', mixed, 1, extra=[(f'arena+{o[2] + 8}', f'arena+{o[3]}')]),
        c('list_prev_wrong_sentinel', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', sentinel(7))]),
        c('list_next_wrong_sentinel', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', sentinel(5))]),
        c('list_prev_low_sentinel', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', sentinel(4))]),
        c('list_next_above_sentinels', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', 'heap+1048')]),
        c('list_prev_misaligned', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', f'arena+{o[5] + 8}')]),
        c('list_next_misaligned', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', f'arena+{o[5] + 4}')]),
        c('list_prev_below', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', 0x10)]),
        c('list_next_at_end', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', f'arena+{o[7]}')]),
        c('list_next_in_heap', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', f'arena+{o[5]}'),
                                                (f'arena+{o[5] + 0x10}', f'arena+{o[1]}')]),
        c('list_prev_no_back_link', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', f'arena+{o[5]}')]),
        c('list_next_no_back_link', [(0x20, U), (0x60, F), (0x20, U), (0x50, F), (0x20, U)], 3,
          extra=[(f'arena+{S + 0x20 + 0x60 + 0x20 + 0x18}', f'arena+{S + 0x20}'),
                 (f'arena+{S + 0x20 + 0x10}', f'arena+{S}')]),
    ]


def robust_cases():
    S = robust.ARENA_START
    mixed = [(0x18, U), (0x58, F), (0x38, U), (0x8, F), (0x18, U), (0x208, F), (0x28, U)]
    o = [S]
    for usable, _ in mixed:
        o.append(o[-1] + usable + 8)
    bin_of = lambda usable: f'bins+{16 * ((usable - 9) // 16)}'  # noqa: E731
    c = robust_case
    return [
        c('used_sound', mixed, 0),
        c('used_before_closing_block', mixed, 6),
        c('free_small_sound', mixed, 1),
        c('free_large_sound', mixed, 5),
        c('free_too_small_for_links', mixed, 3),
        c('free_smallest_small_broken_link', [(0x18, U), (0x18, F), (0x18, U)], 1,
          extra=[(f'arena+{S + 0x20 + 0x10}', bin_of(0x28))]),
        c('free_largest_small', [(0x18, U), (0x1f8, F), (0x18, U)], 1),
        c('free_smallest_large', [(0x18, U), (0x208, F), (0x18, U)], 1),
        c('free_shared_list', [(0x18, U), (0x58, F), (0x18, U), (0x58, F), (0x18, U)], 3),
        c('large_tree_of_three', [(0x18, U), (0x208, F), (0x18, U), (0x308, F), (0x18, U), (0x408, F),
                                  (0x18, U)], 3),
        c('misaligned_payload', mixed, payload=o[2] + 0x18, reports=1),
        c('payload_below', mixed, 0, extra=[('heap+40', f'arena+{o[0] + 0x20}')], reports=1),
        c('payload_above', mixed, payload=o[7] + 0x10, reports=1),
        c('next_alignment_bits', mixed, 0, extra=[(f'arena+{o[0] + 8}', f'arena+{o[1] + 0xc + 3}')]),
        c('next_below', mixed, 2, extra=[(f'arena+{o[2] + 8}', 0x10 | 1)]),
        c('next_above', mixed, 2, extra=[(f'arena+{o[2] + 8}', f'arena+{o[7] + 0x10}')]),
        c('next_is_itself', mixed, 2, extra=[(f'arena+{o[2] + 8}', f'arena+{o[2] + 3}')]),
        c('next_next_broken', mixed, 0, extra=[(f'arena+{o[1] + 8}', f'arena+{o[0] + 2}')]),
        c('next_next_bits', mixed, 0, extra=[(f'arena+{o[1] + 8}', f'arena+{o[2] + 4 + 2}')]),
        c('next_next_outside', mixed, 0, extra=[(f'arena+{o[1] + 8}', 0x12)]),
        c('next_at_range_top_not_checked', mixed, 4, highest=o[5], extra=[(f'arena+{o[5] + 8}', 4)]),
        c('used_next_thinks_free', mixed, 0, extra=[(f'arena+{o[1] + 8}', f'arena+{o[2]}')]),
        c('free_wrong_back_pointer', mixed, 1, extra=[(f'arena+{o[2]}', f'arena+{o[0]}')]),
        c('free_next_thinks_used', mixed, 1, extra=[(f'arena+{o[2] + 8}', f'arena+{o[3] + 3}')]),
        c('free_followed_by_free', mixed, 1, extra=[(f'arena+{o[2] + 8}', f'arena+{o[3]}')]),
        c('small_next_wrong_sentinel', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', bin_of(0x18))]),
        c('small_prev_wrong_sentinel', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', bin_of(0x1f8))]),
        c('small_next_past_bins', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', 'bins+496')]),
        c('small_next_misaligned', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', f'arena+{o[5] + 0x18}')]),
        c('small_prev_misaligned', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', f'arena+{o[5] + 0x14}')]),
        c('small_next_below', mixed, 1, extra=[(f'arena+{o[1] + 0x10}', f'arena+{S}')]),
        c('small_prev_above', mixed, 1, extra=[(f'arena+{o[1] + 0x18}', f'arena+{o[7] + 0x20}')]),
        c('small_next_no_back_link', [(0x18, U), (0x58, F), (0x18, U), (0x58, F), (0x18, U)], 1,
          extra=[(f'arena+{S + 0x20 + 0x60 + 0x20 + 0x18}', 'bins+80')]),
        c('small_prev_no_back_link', [(0x18, U), (0x58, F), (0x18, U), (0x58, F), (0x18, U)], 3,
          extra=[(f'arena+{S + 0x20 + 0x10}', 'bins+80')]),
        c('large_wrong_size', mixed, 5, extra=[(f'arena+{o[5] + 0x28}', 0x200)]),
        c('large_left_outside', mixed, 5, extra=[(f'arena+{o[5] + 0x10}', 0x20)]),
        c('large_right_outside', mixed, 5, extra=[(f'arena+{o[5] + 0x18}', f'arena+{o[7] + 0x30}')]),
        c('large_children_inside', mixed, 5, extra=[(f'arena+{o[5] + 0x10}', f'arena+{o[1]}'),
                                                    (f'arena+{o[5] + 0x18}', f'arena+{o[7]}')]),
        c('large_parent_outside', mixed, 5, extra=[(f'arena+{o[5] + 0x20}', 0x41)]),
        c('large_parent_inside_red', mixed, 5, extra=[(f'arena+{o[5] + 0x20}', f'arena+{o[2]}')]),
        c('large_parent_colour_only', mixed, 5, extra=[(f'arena+{o[5] + 0x20}', 1)]),
    ]


WHICH = {'regular': regular_cases, 'robust': robust_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in WHICH:
        sys.exit(__doc__)
    out = sys.argv[1]
    cases = WHICH[sys.argv[2]]()
    os.makedirs(out, exist_ok=True)
    for c in cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main()
