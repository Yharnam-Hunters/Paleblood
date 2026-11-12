#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/fixed_allocator_shrink.cpp, for tools/verify.py run.

usage: fixed_allocator_shrink_cases.py OUT_DIR

A case lays out an allocator (`alloc`) and the chunks in its list of chunks with room: chunks of
CHUNK bytes side by side in `chunks`, each with its descriptor at the end, linked in the given
order through the allocator's sentinel, each with its own free count. The tree removal and the
backing allocator's free (reached through a fake vtable) are scripted stubs, so the chunks they
get, in order, are compared.
"""
import json
import os
import struct
import sys

SHRINK, TREE_REMOVE = '0x02484f20', '0x024f8460'
BACKING_FREE = '0x02488650'     # any game function: the fake vtable points at it, stubbed
DESCRIPTOR_SIZE = 0x38
SENTINEL = 0x48


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, free_counts, per_chunk=4, order=None, chunk=0x100, current=None, last_freed=None,
         total_free=None, chunk_count=9):
    """free_counts: each chunk's free count; order: the list order (chunk indexes), all by default;
    current / last_freed: a chunk index, or None for a chunk outside the list."""
    n = len(free_counts)
    order = list(range(n)) if order is None else order

    def desc(i):
        return f'chunks+{chunk * i + chunk - DESCRIPTOR_SIZE}'

    def at(node, field):
        name, off = node.split('+')
        return f'buf:{name}+{int(off) + field}'

    memory = [{'addr': 'buf:backing+0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+112', 'guest': BACKING_FREE},
              {'addr': 'buf:alloc+24', 'bytes': u64(chunk)},
              {'addr': 'buf:alloc+40', 'bytes': u64(per_chunk)},
              {'addr': 'buf:alloc+48', 'bytes': u64(sum(free_counts) if total_free is None else total_free)},
              {'addr': 'buf:alloc+56', 'bytes': u64(chunk_count)},
              {'addr': 'buf:alloc+64', 'pointer': 'backing'},
              {'addr': 'buf:alloc+88', 'pointer': 'tree'}]
    for field, which in ((0, current), (8, last_freed)):
        memory.append({'addr': f'buf:alloc+{field}', 'pointer': desc(which) if which is not None else 'other'})
    ring = [f'alloc+{SENTINEL}'] + [desc(i) for i in order]
    for j, node in enumerate(ring):
        memory.append({'addr': at(node, 0), 'pointer': ring[(j + 1) % len(ring)]})
        memory.append({'addr': at(node, 8), 'pointer': ring[j - 1]})
    for i, count in enumerate(free_counts):
        memory.append({'addr': at(desc(i), 0x30), 'bytes': u64(count)})
    released = sum(1 for i in order if free_counts[i] == per_chunk)
    stubs = [{'address': TREE_REMOVE, 'argc': 2}, {'address': BACKING_FREE, 'argc': 2}] * released
    return {'schema': 1, 'address': SHRINK, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:alloc'},
            'buffers': {'alloc': {'size': 0x60, 'bytes': '7e' * 0x60}, 'backing': {'size': 8},
                        'vtable': {'size': 0x80}, 'chunks': {'size': chunk * max(n, 1)},
                        'tree': {'size': 8}, 'other': {'size': 8}},
            'memory': memory, 'stubs': stubs, 'imports': []}


CASES = [
    case('empty_list', []),
    case('one_all_free', [4]),
    case('one_partly_free', [3]),
    case('none_all_free', [1, 2, 3]),
    case('all_all_free', [4, 4, 4]),
    case('mixed', [4, 1, 4, 3, 4]),
    case('list_order_given', [4, 2, 4, 4], order=[3, 1, 0, 2]),
    case('only_some_listed', [4, 4, 4], order=[2]),
    case('current_released', [2, 4, 1], current=1),
    case('last_freed_released', [4, 2], last_freed=0),
    case('current_and_last_freed_same', [4], current=0, last_freed=0),
    case('current_kept', [4, 3], current=1, last_freed=1),
    case('free_count_over_per_chunk', [5, 4]),
    case('counts_wrap', [4, 4], total_free=3, chunk_count=1),
    case('one_per_chunk', [1, 0, 1], per_chunk=1),
    case('zero_per_chunk', [0, 1], per_chunk=0),
    case('chunk_0x1028', [4, 4], chunk=0x1028),
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
