#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the subtree release of the set of 64-bit keys (game/engine/set_free_subtree.cpp),
for tools/verify.py run.

usage: set_free_subtree_cases.py OUT_DIR

Each case builds a set (`set`: head at +0x8, count at +0x10, allocator at +0x18) with its
allocator object behind it in the same buffer (+0x20), so that a scripted free can change the
set's allocator through its first argument. The allocator's free (slot 0x70 of a fake vtable) is a
scripted stub, so every freed node is one recorded call with the allocator and the node. Nodes are
0x28-byte buffers in the MSVC layout (left +0x0, parent +0x8, right +0x10, colour +0x18, nil flag
+0x19, key +0x20); the head's links point at itself and its nil flag is set.

A tree's shape is written as nested pairs: None is the head (no child), (left, right) a node.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02b3bda0'
FREE, OTHER_FREE = '0x02484110', '0x02484ae0'   # any two game functions, stubbed
NODE_SIZE = 0x28
LEAF = (None, None)


def u64(value):
    return struct.pack('<Q', value & 0xffffffffffffffff).hex()


def chain(side, length):
    """A chain of `length` nodes that only goes left (side 0) or right (side 1)."""
    shape = None
    for _ in range(length):
        shape = (shape, None) if side == 0 else (None, shape)
    return shape


def zigzag(length):
    """A chain that turns left and right in turn."""
    shape = None
    for index in range(length):
        shape = (shape, None) if index % 2 else (None, shape)
    return shape


def full(depth):
    """A tree whose every node down to `depth` has both children."""
    return None if depth == 0 else (full(depth - 1), full(depth - 1))


def add_nodes(shape, parent, buffers, memory, names, nil_flag_of):
    """Declare the shape's nodes (numbered root first, then its left subtree, then its right) and
    their links; returns the root's buffer name. A node's key is its number, which the release
    does not read."""
    if shape is None:
        return 'head'
    name = f'node{len(names)}'
    names.append(name)
    buffers[name] = {'size': NODE_SIZE}
    left = add_nodes(shape[0], name, buffers, memory, names, nil_flag_of)
    right = add_nodes(shape[1], name, buffers, memory, names, nil_flag_of)
    index = int(name[4:])
    memory += [{'addr': f'buf:{name}+0', 'pointer': left},
               {'addr': f'buf:{name}+8', 'pointer': parent},
               {'addr': f'buf:{name}+16', 'pointer': right},
               # the colour alternates; the nil flag is clear unless the case sets it
               {'addr': f'buf:{name}+24', 'bytes': f'{index & 1:02x}{nil_flag_of.get(index, 0):02x}'},
               {'addr': f'buf:{name}+32', 'bytes': u64(0x1000 + index)}]
    return name


def freed_count(shape, index, nil_flag_of):
    """How many nodes of the shape the release frees, and the next node index after it."""
    if shape is None:
        return 0, index
    mine = index
    left, index = freed_count(shape[0], index + 1, nil_flag_of)
    right, index = freed_count(shape[1], index, nil_flag_of)
    if nil_flag_of.get(mine):
        return 0, index
    return 1 + left + right, index


def case(cid, shape, head_flag=1, nil_flag_of=None, first_free_writes=None, other_frees=False, results=()):
    """One release of the subtree whose root is the shape's node (the head when it is None).

    head_flag: the head's nil flag byte. nil_flag_of: node index -> nil flag byte, for nodes that
    are marked nil although they are not the head. first_free_writes: what the first free stores
    through its arguments. other_frees: every free after the first goes to the other stub.
    results: what the frees return, in call order (the release ignores them).
    """
    nil_flag_of = nil_flag_of or {}
    buffers = {'set': {'size': 0x28}, 'vtable': {'size': 0x78}, 'head': {'size': NODE_SIZE},
               'other': {'size': 0x8}, 'other_vtable': {'size': 0x78}, 'decoy': {'size': NODE_SIZE}}
    memory = [
        {'addr': 'buf:set+0', 'bytes': u64(0x5a5a5a5a5a5a5a5a)},
        {'addr': 'buf:set+8', 'pointer': 'head'},
        {'addr': 'buf:set+24', 'pointer': 'set+0x20'},
        {'addr': 'buf:set+32', 'pointer': 'vtable'},
        {'addr': 'buf:vtable+112', 'guest': FREE},
        {'addr': 'buf:other+0', 'pointer': 'other_vtable'},
        {'addr': 'buf:other_vtable+112', 'guest': OTHER_FREE},
        {'addr': 'buf:head+0', 'pointer': 'head'},
        {'addr': 'buf:head+8', 'pointer': 'head'},
        {'addr': 'buf:head+16', 'pointer': 'head'},
        {'addr': 'buf:head+24', 'bytes': f'01{head_flag:02x}'},
        # a node no tree links to: freeing it means a link was read from a freed node
        {'addr': 'buf:decoy+0', 'pointer': 'head'},
        {'addr': 'buf:decoy+8', 'pointer': 'head'},
        {'addr': 'buf:decoy+16', 'pointer': 'head'},
        {'addr': 'buf:decoy+24', 'bytes': '0000'},
    ]
    names = []
    root = add_nodes(shape, 'head', buffers, memory, names, nil_flag_of)
    memory.append({'addr': 'buf:set+16', 'bytes': u64(len(names))})
    count, _ = freed_count(shape, 0, nil_flag_of)
    stubs = []
    for index in range(count):
        entry = {'address': OTHER_FREE if other_frees and index else FREE, 'argc': 2}
        if index < len(results):
            entry['ret'] = hex(results[index])
        if index == 0 and first_free_writes:
            entry['writes'] = first_free_writes
        stubs.append(entry)
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:set', 'rsi': f'buf:{root}'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


# What a free stores into the node it is given (argument 1): both links now lead to the decoy.
LINKS_TO_DECOY = [{'arg': 1, 'offset': 0, 'pointer': 'decoy'}, {'arg': 1, 'offset': 16, 'pointer': 'decoy'}]
# The same with the nil flag set too, as if the block had been reused.
NODE_OVERWRITTEN = LINKS_TO_DECOY + [{'arg': 1, 'offset': 24, 'bytes': 'ffff'}]
# What a free stores through the allocator it is given (argument 0), which lies at set+0x20.
SET_GETS_OTHER_ALLOCATOR = [{'arg': 0, 'offset': -8, 'pointer': 'other'}]
ALLOCATOR_GETS_OTHER_VTABLE = [{'arg': 0, 'offset': 0, 'pointer': 'other_vtable'}]

BOTH = (LEAF, LEAF)
MIXED = (((None, LEAF), None), (None, (LEAF, None)))

CASES = [
    case('head_frees_nothing', None),
    case('single_leaf', LEAF),
    case('left_chain_two', chain(0, 2)),
    case('left_chain_three', chain(0, 3)),
    case('left_chain_six', chain(0, 6)),
    case('right_child_only', chain(1, 2)),
    case('right_chain_three', chain(1, 3)),
    case('right_chain_six', chain(1, 6)),
    case('zigzag_five', zigzag(5)),
    case('zigzag_six', zigzag(6)),
    case('both_children', BOTH),
    case('right_then_left_nested', ((None, LEAF), (LEAF, None))),
    case('left_subtree_with_right_branch', MIXED),
    case('full_seven', full(3)),
    case('full_fifteen', full(4)),
    case('full_thirty_one', full(5)),
    # the nil flag is any nonzero byte
    case('head_flag_two', BOTH, head_flag=0x02),
    case('head_flag_high_bit', MIXED, head_flag=0x80),
    case('head_flag_all_ones', full(3), head_flag=0xff),
    # the flag alone ends the walk: a marked node is not freed and its children are not visited
    case('marked_root_with_children', BOTH, nil_flag_of={0: 1}),
    case('marked_left_child', (BOTH, LEAF), nil_flag_of={1: 1}),
    case('marked_right_child', (LEAF, BOTH), nil_flag_of={2: 0x40}),
    case('marked_inner_nodes', full(4), nil_flag_of={2: 1, 9: 0x10}),
    # what the frees return is not used
    case('free_results_ignored', full(3), results=(1, 0xffffffffffffffff, 0, 0x8000000000000000, 5, 6, 7)),
    # a freed node is not read again
    case('first_free_relinks_leaf', LEAF, first_free_writes=LINKS_TO_DECOY),
    case('first_free_relinks_in_left_chain', chain(0, 4), first_free_writes=LINKS_TO_DECOY),
    case('first_free_relinks_in_right_chain', chain(1, 4), first_free_writes=LINKS_TO_DECOY),
    case('first_free_relinks_in_full_tree', full(3), first_free_writes=LINKS_TO_DECOY),
    case('first_free_overwrites_node', MIXED, first_free_writes=NODE_OVERWRITTEN),
    case('first_free_marks_freed_node', chain(0, 3), first_free_writes=[{'arg': 1, 'offset': 25, 'bytes': '01'}]),
    # the allocator and its vtable are read again for every node
    case('first_free_changes_set_allocator', full(3), first_free_writes=SET_GETS_OTHER_ALLOCATOR, other_frees=True),
    case('first_free_changes_set_allocator_left_chain', chain(0, 4), first_free_writes=SET_GETS_OTHER_ALLOCATOR,
         other_frees=True),
    case('first_free_changes_set_allocator_right_chain', chain(1, 4), first_free_writes=SET_GETS_OTHER_ALLOCATOR,
         other_frees=True),
    case('first_free_changes_vtable', full(3), first_free_writes=ALLOCATOR_GETS_OTHER_VTABLE, other_frees=True),
    case('first_free_changes_vtable_zigzag', zigzag(5), first_free_writes=ALLOCATOR_GETS_OTHER_VTABLE,
         other_frees=True),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    ids = [fixture['id'] for fixture in CASES]
    assert len(ids) == len(set(ids)), 'duplicate case id'
    for fixture in CASES:
        with open(os.path.join(out, fixture['id'] + '.json'), 'w') as stream:
            json.dump(fixture, stream, indent=1)
            stream.write('\n')
    print(f'{len(CASES)} synthetic cases written to {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
