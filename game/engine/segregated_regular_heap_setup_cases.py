#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the segregated regular heap's set-up and tear-down
(game/engine/segregated_regular_heap_initialize.cpp, segregated_regular_heap_finalize.cpp), for
tools/verify.py run. Each replacement's own generator calls this one with its function.

usage: segregated_regular_heap_setup_cases.py OUT_DIR initialize|finalize

The heap object (`heap`) and the hash table (`table`) are filled with patterns so that every
field written shows. The managed memory is only addresses to this heap, so the set-up gets plain
numbers. The allocator the heap takes its table from (and gives it back to) has a fake vtable
whose allocate (+0x50) and free (+0x70) are scripted stubs; the descriptor pool's allocation is a
scripted stub that hands out `desc`. For the tear-down, the pool may hold a chunk tree (chunks of
CHUNK bytes in `chunks`), whose release runs as the original code and frees through the stub.
"""
import json
import os
import struct
import sys

INITIALIZE, FINALIZE = '0x02489340', '0x02489530'
POOL_ALLOCATE = '0x02484c70'
BACKING_ALLOCATE, BACKING_FREE = '0x02488cf0', '0x02488f40'   # game functions the vtable points at, stubbed
HEAP_OBJECT, TABLE_BYTES = 0x4c0, 0x2278
POOL = 0x460
CHUNK, DESCRIPTOR_SIZE = 0x100, 0x38


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def buffers():
    return {'heap': {'size': HEAP_OBJECT, 'bytes': '6d' * HEAP_OBJECT},
            'table': {'size': TABLE_BYTES, 'bytes': '4b' * TABLE_BYTES},
            'desc': {'size': 0x28, 'bytes': '29' * 0x28}, 'backing': {'size': 8}, 'vtable': {'size': 0x80},
            'chunks': {'size': CHUNK * 3}}


def vtable_memory():
    return [{'addr': 'buf:backing+0', 'pointer': 'vtable'},
            {'addr': 'buf:vtable+80', 'guest': BACKING_ALLOCATE},
            {'addr': 'buf:vtable+112', 'guest': BACKING_FREE}]


def initialize_case(cid, memory, size, table=True):
    stubs = [{'address': BACKING_ALLOCATE, 'argc': 2, 'ret': 'buf:table' if table else 0}]
    start = (memory + 0xf) & ~0xf
    end = (memory + size) & ~0xf
    if table and ((end - start) & 0xffffffffffffffff) >= 0x10:
        stubs.append({'address': POOL_ALLOCATE, 'argc': 1, 'ret': 'buf:desc'})
    return {'schema': 1, 'address': INITIALIZE, 'id': cid, 'returns': 'i8',
            'args': {'rdi': 'buf:heap', 'rsi': 'buf:backing', 'rdx': memory, 'rcx': size},
            'buffers': buffers(), 'memory': vtable_memory(), 'stubs': stubs, 'imports': []}


def initialize_cases():
    base = 0x20000000
    return [
        initialize_case('no_table', base, 0x10000, table=False),
        initialize_case('range_under_16', base + 1, 0x1e),
        initialize_case('range_16_class_0', base, 0x10),
        initialize_case('range_after_rounding_16', base + 1, 0x2f),
        initialize_case('class_31_0x200', base, 0x200),
        initialize_case('class_32_0x210', base, 0x210),
        initialize_case('class_32_0x3f0', base + 8, 0x3f8),
        initialize_case('class_33_0x400', base, 0x400),
        initialize_case('large_range', base, 0x1000000),
        initialize_case('huge_range', 0x100000000, 0x7f0000000),
        initialize_case('unaligned_both_ends', base + 0x7, 0x12345),
        initialize_case('zero_size', base, 0),
    ]


def finalize_case(cid, tree_chunks=0):
    memory = vtable_memory()
    memory += [{'addr': 'buf:heap+72', 'pointer': 'table'},
               {'addr': f'buf:heap+{POOL + 0x18}', 'bytes': u64(CHUNK)},
               {'addr': f'buf:heap+{POOL + 0x40}', 'pointer': 'backing'}]

    def node(i):
        return f'chunks+{CHUNK * i + CHUNK - DESCRIPTOR_SIZE + 0x10}'

    if tree_chunks == 0:
        memory.append({'addr': f'buf:heap+{POOL + 0x58}', 'bytes': u64(0)})
    else:
        # A root (chunk 0) with up to two children.
        memory.append({'addr': f'buf:heap+{POOL + 0x58}', 'pointer': node(0)})
        children = [node(i) for i in range(1, tree_chunks)] + [None, None]
        for off, child in ((0, children[0]), (8, children[1])):
            name, b = node(0).split('+')
            memory.append({'addr': f'buf:{name}+{int(b) + off}', **({'pointer': child} if child else {'bytes': u64(0)})})
        for i in range(1, tree_chunks):
            name, b = node(i).split('+')
            memory += [{'addr': f'buf:{name}+{int(b)}', 'bytes': u64(0)},
                       {'addr': f'buf:{name}+{int(b) + 8}', 'bytes': u64(0)}]
    stubs = [{'address': BACKING_FREE, 'argc': 2}] * (1 + tree_chunks)
    return {'schema': 1, 'address': FINALIZE, 'id': cid, 'returns': 'i8', 'args': {'rdi': 'buf:heap'},
            'buffers': buffers(), 'memory': memory, 'stubs': stubs, 'imports': []}


def finalize_cases():
    return [
        finalize_case('no_chunks'),
        finalize_case('one_chunk', 1),
        finalize_case('two_chunks', 2),
        finalize_case('three_chunks', 3),
    ]


WHICH = {'initialize': initialize_cases, 'finalize': finalize_cases}


def main(argv):
    if len(argv) != 2 or argv[1] not in WHICH:
        sys.exit(__doc__)
    out, which = argv
    os.makedirs(out, exist_ok=True)
    cases = WHICH[which]()
    for c in cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
