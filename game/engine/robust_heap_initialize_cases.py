#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the robust heap's set-up (game/engine/robust_heap_initialize.cpp), for
tools/verify.py run.

usage: robust_heap_initialize_cases.py OUT_DIR

The heap object is filled with a pattern so that every field the set-up writes shows; the memory
is inside `arena`, which starts on a page, at a given offset (so the sentinels' rounding to 16 and
then 128 bytes shows). A first block larger than the largest small size goes into the empty tree
through the engine's own fix-up, which runs as the original code.
"""
import json
import os
import sys

INITIALIZE = '0x024863f0'
ARENA = 0x3000


def case(cid, offset, size, null_memory=False):
    return {'schema': 1, 'address': INITIALIZE, 'id': cid, 'returns': 'i8',
            'args': {'rdi': 'buf:heap', 'rsi': 0 if null_memory else f'buf:arena+{offset}', 'rdx': size},
            'buffers': {'heap': {'size': 0x40, 'bytes': '5c' * 0x40},
                        'arena': {'size': ARENA, 'bytes': '3a' * ARENA}},
            'memory': [], 'stubs': [], 'imports': []}


def cases():
    return [
        case('null_memory', 0, 0x1000, null_memory=True),
        case('size_0x1f8_refused', 0, 0x1f8),
        case('size_0x1f9_too_short', 0, 0x1f9),
        case('end_just_short', 0, 0x26e),
        case('end_just_enough', 0, 0x26f),
        case('smallest_block_unaligned_end', 0, 0x27e),
        case('small_block_0x58', 0, 0x270),
        case('small_block_largest', 0, 0x1f0 + 0x200 + 0x8),
        case('large_block_smallest', 0, 0x1f0 + 0x210),
        case('large_block', 0, 0x2000),
        case('offset_1', 1, 0x800),
        case('offset_0x10', 0x10, 0x800),
        case('offset_0x80', 0x80, 0x800),
        case('offset_0x81', 0x81, 0x800),
        case('offset_eats_room', 0x81, 0x2ee),
        case('offset_leaves_room', 0x81, 0x2ff),
        case('size_unaligned', 0x30, 0x1234),
    ]


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    out = argv[0]
    os.makedirs(out, exist_ok=True)
    all_cases = cases()
    for c in all_cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(all_cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
