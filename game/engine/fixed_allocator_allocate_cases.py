#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the fixed-size allocator's allocation (game/engine/fixed_allocator_allocate.cpp),
for tools/verify.py run.

usage: fixed_allocator_allocate_cases.py OUT_DIR

Each case lays out an allocator (`alloc`) and chunk slots side by side in `chunks`, CHUNK bytes
each with the descriptor at the end. A slot is a chunk (given by its free elements, in free-list
order) or empty (None): the backing allocator may hand out an empty slot as a new chunk, or a
separate buffer that lies below (`achunk`) or above (`zchunk`) all of them. Chunks with free
elements are in the list in the order given; all chunks are in a balanced red-black address tree
(bit 0 of the parent word = black), whose rebalancing after an insertion runs as the original
code. The backing allocator's aligned allocation is a scripted stub reached through a fake vtable.
"""
import json
import os
import struct
import sys

ALLOCATE = '0x02484c70'
BACKING_ALLOCATE = '0x02488cf0'     # any game function: the fake vtable points at it, stubbed
CHUNK, DESCRIPTOR_SIZE = 0x100, 0x38
DESCRIPTOR = CHUNK - DESCRIPTOR_SIZE
ALIGNMENT = 0x10
STALE = 'a55aa55aa55aa55a'     # one word of old contents


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, slots, list_order=None, current=None, element=0x20, per_chunk=6, new_chunk=None,
         extra_free=0, alignment=ALIGNMENT, stale_tail=None):
    """slots: per slot, the indices of its free elements, or None for an empty slot.
    new_chunk: what the backing allocator returns: a slot index, 'achunk', 'zchunk' or 'fail'."""
    memory = []

    def word(where, value):
        memory.append({'addr': f'buf:{where}', 'bytes': u64(value)})

    def ptr(where, target):
        memory.append({'addr': f'buf:{where}', 'pointer': target})

    used = [i for i, s in enumerate(slots) if s is not None]
    for i, s in enumerate(slots):
        if s is None:
            # Memory the backing allocator hands out holds old contents.
            memory.append({'addr': f'buf:chunks+{CHUNK * i}', 'bytes': STALE * (CHUNK // 8)})
    off = {i: CHUNK * i + DESCRIPTOR for i in used}
    desc = {i: f'chunks+{off[i]}' for i in used}
    for i in used:
        free = slots[i]
        word(f'chunks+{off[i] + 0x30}', len(free))
        if free:
            ptr(f'chunks+{off[i] + 0x28}', f'chunks+{CHUNK * i + element * free[0]}')
        else:
            word(f'chunks+{off[i] + 0x28}', 0)
        for a, b in zip(free, free[1:] + [None]):
            where = f'chunks+{CHUNK * i + element * a}'
            if b is None:
                word(where, 0)
            else:
                ptr(where, f'chunks+{CHUNK * i + element * b}')
    if list_order is None:
        list_order = [i for i in used if slots[i]]
    ring = ['alloc+72'] + [desc[i] for i in list_order]
    for j, node in enumerate(ring):
        name, o = node.split('+')
        ptr(f'{name}+{int(o)}', ring[(j + 1) % len(ring)])
        ptr(f'{name}+{int(o) + 8}', ring[j - 1])
    if stale_tail is not None:
        # The list's head says empty but its tail link still names a chunk: tells which of the
        # two the new chunk is linked after.
        ptr('alloc+80', desc[stale_tail])
    for i in used:
        if i not in list_order:
            word(f'chunks+{off[i]}', 0xdead0000 + i)
            word(f'chunks+{off[i] + 8}', 0xdeae0000 + i)
    # The address tree (nodes at descriptor + 0x10): balanced, red only on a partial last level.
    nodes = [off[i] + 0x10 for i in used]

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
    for field, value in ((0x8, 0x5a5a0000), (0x10, element), (0x18, CHUNK), (0x20, alignment),
                         (0x28, per_chunk), (0x30, sum(len(slots[i]) for i in used) + extra_free),
                         (0x38, len(used))):
        word(f'alloc+{field}', value)
    if current is None:
        word('alloc+0', 0)
    else:
        ptr('alloc+0', desc[current])
    ptr('alloc+64', 'backing')
    ptr('backing+0', 'vtable')
    memory.append({'addr': 'buf:vtable+88', 'guest': BACKING_ALLOCATE})
    buffers = {'alloc': {'size': 0x60}, 'chunks': {'size': CHUNK * len(slots)},
               'backing': {'size': 8}, 'vtable': {'size': 0x78}}
    stubs = []
    if new_chunk is not None:
        if new_chunk == 'fail':
            ret = 0
        elif isinstance(new_chunk, str):
            buffers[new_chunk] = {'size': CHUNK, 'bytes': STALE * (CHUNK // 8)}
            ret = f'buf:{new_chunk}'
        else:
            ret = f'buf:chunks+{CHUNK * new_chunk}'
        stubs.append({'address': BACKING_ALLOCATE, 'argc': 3, 'ret': ret})
    return {'schema': 1, 'address': ALLOCATE, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:alloc'}, 'buffers': buffers, 'memory': memory,
            'stubs': stubs, 'imports': []}


def cases():
    return [
        # From the current chunk.
        case('current_many_free', [[1, 3, 4], [2]], current=0),
        case('current_last_free', [[5], [2]], current=0),
        case('current_last_free_only_chunk', [[2]], current=0),
        case('current_not_first_in_list', [[1], [2, 0]], current=1),
        case('current_last_free_middle_of_list', [[1], [4], [3]], current=1),
        # No current chunk: the first chunk with room.
        case('first_with_room', [[], [2, 5], [1]]),
        case('first_with_room_list_order', [[0], [2, 5], [1]], list_order=[2, 0, 1]),
        case('first_with_room_last_free', [[3], [1]]),
        case('first_with_room_last_free_only', [[], [4], []]),
        # A new chunk.
        case('new_chunk_no_chunks', [None], new_chunk='zchunk'),
        case('new_chunk_in_empty_slot', [None], new_chunk=0),
        case('new_chunk_full_chunks_above', [[], []], new_chunk='achunk'),
        case('new_chunk_full_chunks_below', [[], []], new_chunk='zchunk'),
        case('new_chunk_between', [[], None, []], new_chunk=1),
        case('new_chunk_deep_tree', [[], [], None, [], [], [], [], []], new_chunk=2),
        case('new_chunk_seven_below', [[], [], [], [], [], [], []], new_chunk='zchunk'),
        case('new_chunk_seven_above', [[], [], [], [], [], [], []], new_chunk='achunk'),
        case('new_chunk_two_elements', [[]], new_chunk='zchunk', per_chunk=2),
        case('new_chunk_three_elements', [[]], new_chunk='zchunk', per_chunk=3),
        case('new_chunk_large_elements', [[], None], new_chunk=1, element=0x30, per_chunk=4),
        case('new_chunk_alignment', [[]], new_chunk='zchunk', alignment=0x100, extra_free=3),
        case('new_chunk_stale_tail', [[], []], new_chunk='zchunk', stale_tail=1),
        case('new_chunk_backing_fails', [[], []], new_chunk='fail'),
        case('new_chunk_backing_fails_no_chunks', [None], new_chunk='fail'),
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
