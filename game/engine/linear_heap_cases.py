#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the linear heap (game/engine/linear_heap_allocate.cpp, linear_heap_free.cpp,
linear_heap_query.cpp), for tools/verify.py run.

usage: linear_heap_cases.py OUT_DIR FUNCTION
  FUNCTION: allocate, allocate_aligned, allocate_top, allocate_aligned_top, free, free_until,
            check_owned, contains, walk

Each case builds a heap (`heap`: lowest, highest, bottom, top) over `mem`, filled with a pattern,
with top block headers (the top before the block, in use or not) written where the case puts
them. Positions are offsets into `mem`, or plain numbers for addresses outside it. The error report
is a scripted stub.
"""
import json
import os
import struct
import sys

FATAL_ERROR = '0x024b55b0'
ADDRESSES = {
    'allocate': '0x02485100', 'allocate_aligned': '0x02485120', 'allocate_top': '0x02485190',
    'allocate_aligned_top': '0x024851d0', 'free': '0x02485230', 'free_until': '0x024852d0',
    'check_owned': '0x02485160', 'contains': '0x02485330',
    'walk': '0x02485340',
}
RETURNS = {'allocate': 'i64', 'allocate_aligned': 'i64', 'allocate_top': 'i64', 'allocate_aligned_top': 'i64',
           'contains': 'i8', 'walk': 'i8'}
MEM = 0x200
LOWEST, HIGHEST = 0x10, 0x1f0


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def place(value):
    """A position: an int offset into mem, or ('abs', number)."""
    if isinstance(value, tuple):
        return {'bytes': u64(value[1])}
    return {'pointer': f'mem+{value}'}


def arg(value):
    if isinstance(value, tuple):
        return value[1]
    return f'buf:mem+{value}'


def case(function, cid, bottom=0x40, top=0x1f0, lowest=LOWEST, highest=HIGHEST, headers=(), args=(),
         reports=0, walk=None):
    """headers: (offset, above, in_use); args: rsi, rdx values (positions or ('abs', n) / ints)."""
    memory = []
    for offset, field in ((0, lowest), (8, highest), (16, bottom), (24, top)):
        memory.append({'addr': f'buf:heap+{offset}', **place(field)})
    for offset, above, in_use in headers:
        memory += [{'addr': f'buf:mem+{offset}', **place(above)},
                   {'addr': f'buf:mem+{offset + 8}', 'bytes': u64(in_use)}]
    buffers = {'heap': {'size': 0x20}, 'mem': {'size': MEM, 'bytes': 'c3' * MEM}}
    registers = {'rdi': 'buf:heap'}
    for name, value in zip(('rsi', 'rdx'), args):
        registers[name] = arg(value) if isinstance(value, tuple) or function in POSITION_ARGS else value
    if function == 'walk':
        if walk is None:
            registers['rsi'] = 0
        else:
            buffers['walk'] = {'size': 0x30, 'bytes': '9d' * 0x30}
            memory.append({'addr': 'buf:walk+0', **place(walk)})
            registers['rsi'] = 'buf:walk'
    return {'schema': 1, 'address': ADDRESSES[function], 'id': cid, 'returns': RETURNS.get(function, 'void'),
            'args': registers, 'buffers': buffers, 'memory': memory,
            'stubs': [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports)], 'imports': []}


# Functions whose first argument is an address (a position), not a number.
POSITION_ARGS = {'free', 'free_until', 'check_owned', 'contains'}
ABS = 'abs'


def allocate():
    f = 'allocate'
    return [case(f, 'fits', args=[0x20]), case(f, 'exact_fit', args=[0x1b0]), case(f, 'one_too_many', args=[0x1b1]),
            case(f, 'size_zero', args=[0]), case(f, 'full_heap_zero', bottom=0x80, top=0x80, args=[0]),
            case(f, 'full_heap_one', bottom=0x80, top=0x80, args=[1]),
            case(f, 'size_wraps', args=[0xffffffffffffffff]), case(f, 'huge', args=[1 << 62])]


def allocate_aligned():
    f = 'allocate_aligned'
    return [case(f, 'aligned_bottom', bottom=0x40, args=[0x10, 0x10]),
            case(f, 'padded', bottom=0x43, args=[0x10, 0x10]),
            case(f, 'padded_64', bottom=0x41, args=[0x10, 0x40]),
            case(f, 'alignment_one', bottom=0x43, args=[8, 1]),
            case(f, 'alignment_two', bottom=0x43, args=[8, 2]),
            case(f, 'alignment_zero', bottom=0x43, args=[8, 0]),
            case(f, 'alignment_three', bottom=0x43, args=[8, 3]),
            case(f, 'alignment_six', bottom=0x40, args=[8, 6]),
            case(f, 'alignment_top_bit', bottom=0x40, args=[8, 1 << 63]),
            case(f, 'padding_overflows', bottom=0x1e1, top=0x1f0, args=[0x8, 0x20]),
            case(f, 'exact_after_padding', bottom=0x1e1, top=0x1f0, args=[0xf, 0x10]),
            case(f, 'one_over_after_padding', bottom=0x1e1, top=0x1f0, args=[0x10, 0x10])]


def allocate_top():
    f = 'allocate_top'
    return [case(f, 'fits', args=[0x20]), case(f, 'odd_size', args=[0x13]), case(f, 'size_zero', args=[0]),
            case(f, 'unaligned_top', top=0x1eb, args=[0x10]), case(f, 'unaligned_top_odd', top=0x1e5, args=[0x9]),
            case(f, 'exact_fit', bottom=0x40, top=0x60, args=[0x10]),
            case(f, 'one_too_many', bottom=0x41, top=0x60, args=[0x10]),
            case(f, 'below_existing_block', top=0x150, headers=[(0x150, HIGHEST, 1)], args=[0x30]),
            case(f, 'size_wraps', args=[0xfffffffffffffff0])]


def allocate_aligned_top():
    f = 'allocate_aligned_top'
    return [case(f, 'alignment_16', top=0x1e8, args=[0x20, 0x10]), case(f, 'alignment_64', top=0x1e8, args=[0x20, 0x40]),
            case(f, 'alignment_8', top=0x1ec, args=[0x13, 8]), case(f, 'alignment_4', top=0x1ec, args=[0x13, 4]),
            case(f, 'alignment_1', top=0x1ef, args=[0x13, 1]), case(f, 'alignment_zero', top=0x1ef, args=[0x13, 0]),
            case(f, 'alignment_three', args=[0x10, 3]), case(f, 'alignment_twelve', args=[0x10, 12]),
            case(f, 'no_room_after_padding', bottom=0x48, top=0x70, args=[0x10, 0x20]),
            case(f, 'exact_room_after_padding', bottom=0x40, top=0x70, args=[0x10, 0x20])]


def free():
    f = 'free'
    chain = [(0x100, 0x140, 1), (0x140, 0x180, 1), (0x180, HIGHEST, 1)]
    return [
        case(f, 'null', args=[(ABS, 0)]),
        case(f, 'lowest_block_next_in_use', top=0x100, headers=chain, args=[0x110]),
        case(f, 'lowest_block_next_free', top=0x100, headers=[(0x100, 0x140, 1), (0x140, 0x180, 0), (0x180, HIGHEST, 1)],
             args=[0x110]),
        case(f, 'lowest_block_rest_free', top=0x100, headers=[(0x100, 0x140, 1), (0x140, 0x180, 0), (0x180, HIGHEST, 0)],
             args=[0x110]),
        case(f, 'only_block', top=0x180, headers=[(0x180, HIGHEST, 1)], args=[0x190]),
        case(f, 'middle_block', top=0x100, headers=chain, args=[0x150]),
        case(f, 'highest_block', top=0x100, headers=chain, args=[0x190]),
        case(f, 'block_already_free', top=0x100, headers=[(0x100, 0x140, 1), (0x140, 0x180, 0), (0x180, HIGHEST, 1)],
             args=[0x150]),
        case(f, 'below_range_reported', lowest=0x20, top=0x100, headers=chain, args=[0x18], reports=1),
        case(f, 'at_highest_reported', top=0x100, headers=chain, highest=0x1e0, args=[0x1e0], reports=1),
        case(f, 'outside_but_lowest_block', lowest=0x120, top=0x100, headers=chain, args=[0x110], reports=1),
        case(f, 'at_lowest', lowest=0x110, top=0x100, headers=chain, args=[0x110]),
    ]


def free_until():
    f = 'free_until'
    return [case(f, 'rewinds', bottom=0x80, args=[0x40]), case(f, 'mark_past_bottom', bottom=0x80, args=[0xa0]),
            case(f, 'mark_at_bottom', bottom=0x80, args=[0x80]), case(f, 'mark_at_lowest', bottom=0x80, args=[LOWEST]),
            case(f, 'mark_at_highest', bottom=0x80, args=[HIGHEST]),
            case(f, 'mark_below_reported', bottom=0x80, args=[LOWEST - 1], reports=1),
            case(f, 'mark_past_highest_reported', bottom=0x80, args=[HIGHEST + 1], reports=1),
            case(f, 'mark_zero_reported', bottom=0x80, args=[(ABS, 0)], reports=1)]


def range_checks(f, report):
    r = 1 if report else 0
    return [case(f, 'inside', args=[0x80]), case(f, 'at_lowest', args=[LOWEST]),
            case(f, 'last_byte', args=[HIGHEST - 1]), case(f, 'at_highest', args=[HIGHEST], reports=r),
            case(f, 'below', args=[LOWEST - 1], reports=r), case(f, 'null', args=[(ABS, 0)], reports=r),
            case(f, 'all_ones', args=[(ABS, 0xffffffffffffffff)], reports=r),
            case(f, 'empty_range', lowest=0x80, highest=0x80, args=[0x80], reports=r)]


def walk():
    f = 'walk'
    chain = [(0x100, 0x140, 1), (0x140, HIGHEST, 0)]
    return [case(f, 'null_walk'),
            case(f, 'start', top=0x100, headers=chain, walk=(ABS, 0)),
            case(f, 'free_gap', top=0x100, headers=chain, walk=LOWEST),
            case(f, 'block_in_use', top=0x100, headers=chain, walk=0x100),
            case(f, 'block_free', top=0x100, headers=chain, walk=0x140),
            case(f, 'end', top=0x100, headers=chain, walk=HIGHEST),
            case(f, 'empty_bottom', bottom=LOWEST, top=HIGHEST, walk=(ABS, 0)),
            case(f, 'no_top_blocks_gap', bottom=0x80, top=HIGHEST, walk=LOWEST),
            case(f, 'lowest_zero_start', lowest=(ABS, 0), bottom=0x80, walk=(ABS, 0)),
            case(f, 'lowest_is_highest', lowest=0x80, highest=0x80, bottom=0x80, top=0x80, walk=0x80)]


WHICH = {'allocate': allocate, 'allocate_aligned': allocate_aligned, 'allocate_top': allocate_top,
         'allocate_aligned_top': allocate_aligned_top, 'free': free, 'free_until': free_until,
         'check_owned': lambda: range_checks('check_owned', True),
         'contains': lambda: range_checks('contains', False), 'walk': walk}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in WHICH:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    made = WHICH[sys.argv[2]]()
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
