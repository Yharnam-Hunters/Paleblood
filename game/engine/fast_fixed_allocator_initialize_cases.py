#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/fast_fixed_allocator_initialize.cpp, for tools/verify.py run.

usage: fast_fixed_allocator_initialize_cases.py OUT_DIR

Each case sets up an allocator (`alloc`, filled with a pattern so every field written shows) for a
block size, a chunk size and an alignment. The error report is a scripted stub; which reports a
case makes (and the arguments each passes) follows the set-up's checks, worked out here.
"""
import json
import os
import sys

INITIALIZE, FATAL_ERROR = '0x02a3baa0', '0x024b55b0'
MASK = (1 << 64) - 1


def highest_bit(v):
    return 1 << ((v.bit_length() - 1) & 63) if v else 1 << 63


def reports(block, chunk_size, alignment):
    """The argument counts of the reports, in order."""
    out = []
    if chunk_size <= 0x40:
        out.append(4)
        if chunk_size == 0:
            out.append(3)
    chunk = highest_bit((chunk_size - 1 + highest_bit(chunk_size)) & MASK)
    if alignment & (alignment - 1):
        out.append(4)
    a = max(alignment, 8)
    up = lambda v: (v + a - 1) & (-a) & MASK  # noqa: E731
    chunk_alignment, stride = up(chunk), up(block)
    header = (up(0x20) + stride) & MASK
    assert stride, 'a block size of 0 divides by zero'
    if chunk_alignment <= header:
        out.append(5)
    if chunk <= header:
        out.append(5)
    return out


def case(cid, block, chunk_size, alignment):
    return {'schema': 1, 'address': INITIALIZE, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:alloc', 'rsi': 'buf:backing', 'rdx': block, 'rcx': chunk_size, 'r8': alignment},
            'buffers': {'alloc': {'size': 0x60, 'bytes': '7e' * 0x60}, 'backing': {'size': 8}},
            'memory': [], 'stubs': [{'address': FATAL_ERROR, 'argc': n} for n in reports(block, chunk_size, alignment)],
            'imports': []}


CASES = [
    case('typical', 0x30, 0x4000, 16),
    case('chunk_power_of_two', 0x20, 0x1000, 8),
    case('chunk_rounded_up', 0x20, 0x1001, 8),
    case('chunk_just_under', 0x20, 0xfff, 8),
    case('chunk_0x41', 0x8, 0x41, 8),
    case('chunk_0x40', 0x8, 0x40, 8),
    case('chunk_1', 0x8, 1, 8),
    case('chunk_0', 0x8, 0, 8),
    case('chunk_top_bit', 0x40, 1 << 63, 16),
    case('chunk_above_top_bit', 0x40, (1 << 63) + 1, 16),
    case('block_1', 1, 0x400, 8),
    case('block_rounded', 0x21, 0x400, 16),
    case('alignment_0', 0x18, 0x400, 0),
    case('alignment_1', 0x18, 0x400, 1),
    case('alignment_4', 0x18, 0x400, 4),
    case('alignment_0x40', 0x18, 0x1000, 0x40),
    case('alignment_0x200', 0x18, 0x1000, 0x200),
    case('alignment_3', 0x18, 0x400, 3),
    case('alignment_0x18', 0x30, 0x400, 0x18),
    case('header_and_block_fill_chunk', 0x60, 0x80, 8),
    case('header_and_block_just_fit', 0x58, 0x80, 8),
    case('block_bigger_than_chunk', 0x800, 0x400, 8),
    case('chunk_alignment_too_small_only', 0x30, 0x40, 0x80),
    case('one_block_per_chunk', 0xa0, 0x100, 32),
    case('alignment_6', 0x18, 0x400, 6),
    case('header_rounded_to_alignment', 0x30, 0x80, 0x40),
    case('chunk_too_small_rounded', 0x60, 0x7f, 8),
]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for c in CASES:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases in {out}')


if __name__ == '__main__':
    main()
