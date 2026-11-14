#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for releasing a fixed-size allocator's chunks (game/engine/fixed_allocator_release_chunks.cpp
and fixed_allocator_clear.cpp), for tools/verify.py run. Each replacement's own generator calls
this one with its function.

usage: fixed_allocator_release_cases.py OUT_DIR release_chunks|clear

A case lays out an allocator (`alloc`, filled with a pattern) and its chunk tree: chunks of
CHUNK bytes side by side in `chunks`, each with its descriptor at the end and the tree node 0x10
into the descriptor. A tree is given as nested tuples (chunk index, left, right) or None. The
backing allocator's free is a scripted stub reached through a fake vtable, so its calls (and the
chunk addresses they get, in order) are compared.
"""
import json
import os
import struct
import sys

RELEASE, CLEAR = '0x02484ae0', '0x02484c20'
BACKING_FREE = '0x02488cf0'     # any game function: the fake vtable points at it, stubbed
DESCRIPTOR_SIZE, NODE = 0x38, 0x10


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def tree_nodes(tree):
    if tree is None:
        return []
    index, left, right = tree
    return tree_nodes(left) + tree_nodes(right) + [index]


def case(address, cid, tree, chunk=0x100, chunk_count=7, from_node=None):
    """from_node: for release_chunks, the subtree to start from (a nested tuple inside `tree`)."""
    memory = [{'addr': 'buf:backing+0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+112', 'guest': BACKING_FREE},
              {'addr': 'buf:alloc+24', 'bytes': u64(chunk)},
              {'addr': 'buf:alloc+64', 'pointer': 'backing'}]

    def node(index):
        return f'chunks+{chunk * index + chunk - DESCRIPTOR_SIZE + NODE}'

    def lay(t, parent):
        if t is None:
            return
        index, left, right = t
        at = node(index)
        for off, child in ((0, left), (8, right)):
            name, base = at.split('+')
            where = f'{name}+{int(base) + off}'
            if child is None:
                memory.append({'addr': f'buf:{where}', 'bytes': u64(0)})
            else:
                memory.append({'addr': f'buf:{where}', 'pointer': node(child[0])})
        name, base = at.split('+')
        if parent is None:
            memory.append({'addr': f'buf:{name}+{int(base) + 16}', 'bytes': u64(1)})
        else:
            memory.append({'addr': f'buf:{name}+{int(base) + 16}', 'pointer': node(parent)})
        lay(left, index)
        lay(right, index)

    lay(tree, None)
    if tree is not None:
        memory.append({'addr': 'buf:alloc+88', 'pointer': node(tree[0])})
    else:
        memory.append({'addr': 'buf:alloc+88', 'bytes': u64(0)})
    start = from_node if from_node is not None else tree
    frees = len(tree_nodes(start))
    args = {'rdi': 'buf:alloc'}
    if address == RELEASE:
        args['rsi'] = f'buf:{node(start[0])}'
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': args,
            'buffers': {'alloc': {'size': 0x60, 'bytes': '7e' * 0x60}, 'backing': {'size': 8},
                        'vtable': {'size': 0x80}, 'chunks': {'size': chunk * chunk_count}},
            'memory': memory, 'stubs': [{'address': BACKING_FREE, 'argc': 2}] * frees, 'imports': []}


SINGLE = (0, None, None)
LEFT_ONLY = (1, (0, None, None), None)
RIGHT_ONLY = (0, None, (1, None, None))
THREE = (1, (0, None, None), (2, None, None))
SEVEN = (3, (1, (0, None, None), (2, None, None)), (5, (4, None, None), (6, None, None)))
CHAIN = (0, None, (2, (1, None, None), (3, None, (4, None, None))))


def release_cases():
    def c(cid, tree, **kw):
        return case(RELEASE, cid, tree, **kw)
    return [
        c('single', SINGLE),
        c('left_only', LEFT_ONLY),
        c('right_only', RIGHT_ONLY),
        c('three', THREE),
        c('seven', SEVEN),
        c('uneven_chain', CHAIN),
        c('subtree_only', SEVEN, from_node=SEVEN[1]),
        c('right_subtree_only', SEVEN, from_node=SEVEN[2]),
        c('chunk_0x40', THREE, chunk=0x40),
        c('chunk_0x1028', THREE, chunk=0x1028, chunk_count=3),
    ]


def clear_cases():
    def c(cid, tree, **kw):
        return case(CLEAR, cid, tree, **kw)
    return [
        c('no_chunks', None),
        c('single', SINGLE),
        c('three', THREE),
        c('seven', SEVEN),
        c('uneven_chain', CHAIN, chunk=0x80),
    ]


WHICH = {'release_chunks': release_cases, 'clear': clear_cases}


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
