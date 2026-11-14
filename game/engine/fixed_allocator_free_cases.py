#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the fixed-size allocator's free (game/engine/fixed_allocator_free.cpp), for
tools/verify.py run.

usage: fixed_allocator_free_cases.py OUT_DIR

Each case lays out an allocator (`alloc`) and its chunks side by side in `chunks`, CHUNK bytes
each with the descriptor at the end: six 0x20-byte elements, then padding. Chunks with free
elements are in the list in the order given; all chunks are in a balanced red-black address tree
(bit 0 of the parent word = black), whose removal runs as the original code. The backing
allocator's free is a scripted stub reached through a fake vtable.
"""
import json
import os
import struct
import sys

FREE, FATAL_ERROR = '0x02484de0', '0x024b55b0'
BACKING_FREE = '0x02488cf0'     # any game function: the fake vtable points at it and it is stubbed
CHUNK, ELEMENT, PER_CHUNK = 0x100, 0x20, 6
DESCRIPTOR = CHUNK - 0x38


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, chunks, chunk_index, element, list_order=None, last=None, current=None, extra_free=0,
         per_chunk=PER_CHUNK, null=False, raw_offset=None):
    """chunks: per chunk, the indices of its free elements (in free-list order)."""
    memory = []

    def word(where, value):
        memory.append({'addr': f'buf:{where}', 'bytes': u64(value)})

    def ptr(where, target):
        memory.append({'addr': f'buf:{where}', 'pointer': target})

    desc = [f'chunks+{CHUNK * i + DESCRIPTOR}' for i in range(len(chunks))]
    off = [CHUNK * i + DESCRIPTOR for i in range(len(chunks))]
    for i, free in enumerate(chunks):
        word(f'chunks+{off[i] + 0x30}', len(free))
        if free:
            ptr(f'chunks+{off[i] + 0x28}', f'chunks+{CHUNK * i + ELEMENT * free[0]}')
        else:
            word(f'chunks+{off[i] + 0x28}', 0)
        for a, b in zip(free, free[1:] + [None]):
            where = f'chunks+{CHUNK * i + ELEMENT * a}'
            if b is None:
                word(where, 0)
            else:
                ptr(where, f'chunks+{CHUNK * i + ELEMENT * b}')
    # Chunks with room, in the order given (default: address order).
    if list_order is None:
        list_order = [i for i, free in enumerate(chunks) if free]
    ring = ['alloc+72'] + [desc[i] for i in list_order]
    for j, node in enumerate(ring):
        name, o = node.split('+')
        ptr(f'{name}+{int(o)}', ring[(j + 1) % len(ring)])
        ptr(f'{name}+{int(o) + 8}', ring[j - 1])
    for i, free in enumerate(chunks):
        if not free and i not in list_order:
            word(f'chunks+{off[i]}', 0xdead0000 + i)
            word(f'chunks+{off[i] + 8}', 0xdeae0000 + i)
    # The address tree (nodes at descriptor + 0x10): a balanced tree, red only on a partial last level.
    nodes = [o + 0x10 for o in off]

    def build(lo, hi, parent, depth, total):
        if lo >= hi:
            return None
        mid = (lo + hi - 1) // 2
        n = nodes[mid]
        left = build(lo, mid, n, depth + 1, total)
        right = build(mid + 1, hi, n, depth + 1, total)
        for field, child in ((0, left), (8, right)):
            if child is None:
                word(f'chunks+{n + field}', 0)
            else:
                ptr(f'chunks+{n + field}', f'chunks+{child}')
        full = (1 << total) - 1 == len(nodes)
        black = 0 if (depth == total - 1 and depth > 0 and not full) else 1
        if parent is None:
            word(f'chunks+{n + 16}', black)
        else:
            ptr(f'chunks+{n + 16}', f'chunks+{parent + black}')
        return n

    total = 0
    while (1 << total) - 1 < len(nodes):
        total += 1
    root = build(0, len(nodes), None, 0, total)
    if root is None:
        word('alloc+88', 0)
    else:
        ptr('alloc+88', f'chunks+{root}')
    for field, value in ((0x10, ELEMENT), (0x18, CHUNK), (0x20, 0x10), (0x28, per_chunk),
                         (0x30, sum(len(f) for f in chunks) + extra_free), (0x38, len(chunks))):
        word(f'alloc+{field}', value)
    for field, which in ((0, current), (8, last)):
        if which is None:
            word(f'alloc+{field}', 0)
        else:
            ptr(f'alloc+{field}', desc[which])
    ptr('alloc+64', 'backing')
    ptr('backing+0', 'vtable')
    memory.append({'addr': 'buf:vtable+112', 'guest': BACKING_FREE})
    offset = CHUNK * chunk_index + ELEMENT * element if raw_offset is None else raw_offset
    element_arg = 0 if null else f'buf:chunks+{offset}'
    stubs = [{'address': BACKING_FREE, 'argc': 2}, {'address': FATAL_ERROR, 'argc': 3}]
    return {'schema': 1, 'address': FREE, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:alloc', 'rsi': element_arg},
            'buffers': {'alloc': {'size': 0x60}, 'chunks': {'size': CHUNK * len(chunks)},
                        'backing': {'size': 8}, 'vtable': {'size': 0x78}},
            'memory': memory, 'stubs': stubs, 'imports': []}


def cases():
    return [
        case('last_chunk_hit', [[1, 3], [2]], 0, 0, last=0),
        case('last_chunk_hit_last_element', [[0], [2]], 1, 5, last=1),
        # An address exactly chunk_size below the last chunk's descriptor still counts as that
        # chunk's (here it is the descriptor of the chunk before, which the tree would find).
        case('last_chunk_upper_bound', [[1], [2]], 0, 0, last=1, raw_offset=DESCRIPTOR),
        case('search_left', [[1], [2], [3]], 0, 4, last=2),
        case('search_right', [[1], [2], [3]], 2, 4, last=0),
        case('search_root', [[1], [2], [3]], 1, 0),
        case('search_deep', [[1], [2], [3], [4], [0], [5], [1]], 6, 2, last=None),
        case('chunk_had_no_room', [[], [2]], 0, 3, last=0),
        case('chunk_had_no_room_list_empty', [[], []], 1, 3),
        case('chunk_had_no_room_list_order', [[1], [], [2, 4]], 1, 0, list_order=[2, 0]),
        case('emptied_released', [[0, 1, 2, 3, 4], [2]], 0, 5, last=0, extra_free=6),
        case('emptied_released_current', [[0, 1, 2, 3, 4], [2]], 0, 5, current=0, last=1, extra_free=6),
        case('emptied_released_root', [[1], [0, 1, 2, 3, 4], [2]], 1, 5, extra_free=6),
        case('emptied_released_only_chunk', [[0, 1, 2, 3, 4]], 0, 5, extra_free=7),
        case('emptied_threshold_exact', [[0, 1, 2, 3, 4], [2]], 0, 5, extra_free=5),
        case('emptied_kept_below_threshold', [[0, 1, 2, 3, 4], [2]], 0, 5, extra_free=4),
        case('one_per_chunk', [[], [0]], 0, 0, per_chunk=1),
        case('one_per_chunk_threshold', [[], [0], [0]], 0, 0, per_chunk=1, extra_free=4),
        case('null', [[1], [2]], 0, 0, null=True),
    ]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in cases():
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases())} cases in {sys.argv[1]}')


if __name__ == '__main__':
    main()
