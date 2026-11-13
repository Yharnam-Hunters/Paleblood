#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/fast_fixed_allocator_release.cpp, for tools/verify.py run.

usage: fast_fixed_allocator_release_cases.py OUT_DIR

Each case builds a fast fixed-size allocator (`alloc`) whose two chunk lists are circular lists
through chunks in `chunks` (each chunk: its blocks, then its links at the allocator's blocks
size), with or without a spare chunk. The backing allocator's free (through a fake vtable) is a
scripted stub; its argument order shows the chunks freed and their order.
"""
import json
import os
import struct
import sys

RELEASE, FREE = '0x02a3bc40', '0x02484110'   # FREE: any game function, stubbed
CHUNK_STRIDE = 0x80


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, first=(), second=(), spare=None, blocks_bytes=0x40):
    """first/second: chunk indexes in list order; spare: a chunk index or None."""
    memory = [{'addr': 'buf:alloc+64', 'bytes': u64(blocks_bytes)},
              {'addr': 'buf:alloc+88', 'pointer': 'backing'},
              {'addr': 'buf:backing+0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+112', 'guest': FREE}]

    def links(index):
        return f'chunks+{index * CHUNK_STRIDE + blocks_bytes}'

    for sentinel, chain in ((0x10, first), (0x20, second)):
        nodes = [('alloc', sentinel)] + [('chunks', i * CHUNK_STRIDE + blocks_bytes) for i in chain]
        for at, (name, offset) in enumerate(nodes):
            nxt, prev = nodes[(at + 1) % len(nodes)], nodes[at - 1]
            memory += [{'addr': f'buf:{name}+{offset}', 'pointer': f'{nxt[0]}+{nxt[1]}'},
                       {'addr': f'buf:{name}+{offset + 8}', 'pointer': f'{prev[0]}+{prev[1]}'}]
    if spare is None:
        memory.append({'addr': 'buf:alloc+0', 'bytes': u64(0)})
    else:
        memory.append({'addr': 'buf:alloc+0', 'pointer': links(spare)})
    frees = len(first) + len(second) + (spare is not None)
    count = max(list(first) + list(second) + ([spare] if spare is not None else []) + [0]) + 1
    return {'schema': 1, 'address': RELEASE, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:alloc'},
            'buffers': {'alloc': {'size': 0x60, 'bytes': '7e' * 0x60}, 'backing': {'size': 8},
                        'vtable': {'size': 0x78},
                        'chunks': {'size': count * CHUNK_STRIDE + 0x40, 'bytes': '5a' * (count * CHUNK_STRIDE + 0x40)}},
            'memory': memory, 'stubs': [{'address': FREE, 'argc': 2} for _ in range(frees)], 'imports': []}


CASES = [
    case('empty'),
    case('spare_only', spare=0),
    case('first_list_one', first=[0]),
    case('second_list_one', second=[0]),
    case('both_lists', first=[0, 1], second=[2], spare=3),
    case('long_first_list', first=[3, 1, 0, 2]),
    case('long_second_list', second=[2, 0, 1], spare=3),
    case('blocks_bytes_zero', first=[0], second=[1], spare=2, blocks_bytes=0),
    case('blocks_bytes_large', first=[0, 1], blocks_bytes=0x70),
    case('blocks_bytes_odd', first=[0], second=[1], spare=2, blocks_bytes=0x38),
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
