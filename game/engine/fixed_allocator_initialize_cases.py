#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/fixed_allocator_initialize.cpp, for tools/verify.py run.

usage: fixed_allocator_initialize_cases.py OUT_DIR

Each case sets up an allocator (`alloc`, filled with a pattern so every field the set-up writes
shows) for an element size, the room a chunk's elements may take and an alignment. The error
report is a scripted stub whose arguments (the values the messages print) are compared.
"""
import json
import os
import sys

INITIALIZE, FATAL_ERROR = '0x02484b30', '0x024b55b0'


def case(cid, element_size, room, alignment, reports=()):
    """reports: the argument count of each report expected (4: alignment, 5: room)."""
    return {'schema': 1, 'address': INITIALIZE, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:alloc', 'rsi': 'buf:backing', 'rdx': element_size, 'rcx': room,
                     'r8': alignment},
            'buffers': {'alloc': {'size': 0x60, 'bytes': '7e' * 0x60}, 'backing': {'size': 8}},
            'memory': [], 'stubs': [{'address': FATAL_ERROR, 'argc': n} for n in reports], 'imports': []}


CASES = [
    case('descriptor_pool', 0x28, 0xff0, 8),
    case('element_1', 1, 0x100, 8),
    case('element_0', 0, 0x100, 8),
    case('element_7_align_4', 7, 0x100, 4),
    case('element_9', 9, 0x100, 8),
    case('element_0x30_align_0x20', 0x30, 0x1000, 0x20),
    case('align_1', 0x13, 0x200, 1),
    case('align_2', 0x13, 0x200, 2),
    case('align_0x1000', 0x10, 0x4000, 0x1000),
    case('room_exactly_two', 0x40, 0x80, 0x10),
    case('room_not_a_multiple', 0x40, 0xbf, 0x10),
    case('room_for_one', 0x40, 0x7f, 0x10, reports=(5,)),
    case('room_for_none', 0x40, 0x3f, 0x10, reports=(5,)),
    case('room_zero', 0x10, 0, 8, reports=(5,)),
    case('align_3', 0x10, 0x100, 3, reports=(4,)),
    case('align_6', 0x10, 0x100, 6, reports=(4,)),
    case('align_0x30_room_small', 0x40, 0x50, 0x30, reports=(4, 5)),
    case('huge_element', 1 << 62, 1 << 63, 8),
    case('stride_overflows_check', 0x8000000000000000, 0x100, 8),
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
