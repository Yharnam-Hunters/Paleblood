#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_reallocate.cpp, for tools/verify.py run.

usage: scaleform_heap_reallocate_cases.py OUT_DIR

Small blocks are in `mem` (0x2000 bytes): a page based at mem+0x800 whose headers (at mem+0xff0 and
mem+0x1000) name descriptor 3 of page table entry 5 (`pages`), owned by the heap `heap` (its mutex
at +0x98, thread-safe flag at +0xa0, engine `eng` at +0xa8). Large blocks are in `tree`, a buffer
of 0x20-byte nodes of the page manager's large-block tree (`pm`, root at +0x28), each naming its
heap at +0x18 (with flag bits). The tree is a chain of left children from the root, deep enough
that the key's first set bit (bit 35 of the sandbox's addresses) chooses a right child. The engine's
in-place resize, the moving reallocation, the large-block reallocation and the locks are scripted.
"""
import json
import os
import struct
import sys

ADDRESS = '0x023392d0'
BIN_REALLOCATE, SMALL_MOVING, LARGE = '0x0233b2f0', '0x0233d140', '0x0233ccf0'
GUARD_GOT, PAGE_MANAGER, PAGE_TABLE = 0x057e3be0, 0x05940360, 0x05994470
TABLE, INDEX = 5, 3
SIGNATURE = 0x5fc0
NODE = 0x20
KEY_BIT = 35                 # the highest set bit of a sandbox address
CHAIN = 63 - KEY_BIT         # left steps before the walk reaches it

LOCK = {'name': 'pthread_mutex_lock', 'argc': 1, 'ret': 0}
UNLOCK = {'name': 'pthread_mutex_unlock', 'argc': 1, 'ret': 0}


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def in_place(ret, old):
    return {'address': BIN_REALLOCATE, 'argc': 6, 'ret': ret,
            'writes': [{'arg': 4, 'offset': 0, 'bytes': u64(old)}]}


def moving(ret='buf:moved'):
    return {'address': SMALL_MOVING, 'argc': 6, 'ret': ret}


def large(ret='buf:moved'):
    return {'address': LARGE, 'argc': 5, 'ret': ret}


def case(cid, block, size, stubs, imports=(), thread_safe=False, nodes=None, root=None, heap_flags=0,
         decoy=False):
    """nodes: {node: (left, right)} by node number (node n at tree+0x20*n); root: a node number."""
    memory = [{'addr': hex(GUARD_GOT), 'pointer': 'guard'},
              {'addr': hex(PAGE_MANAGER), 'pointer': 'pm'},
              {'addr': hex(PAGE_TABLE + 16 * TABLE), 'pointer': 'pages'},
              {'addr': hex(PAGE_TABLE + 16 * TABLE + 8), 'bytes': u64(0xff)},
              {'addr': f'buf:pages+{0x20 * INDEX + 0x10}', 'pointer': 'heap'},
              {'addr': f'buf:pages+{0x20 * INDEX + 0x18}', 'pointer': 'mem+2048'},
              {'addr': 'buf:heap+160', 'bytes': '01' if thread_safe else '00'},
              {'addr': 'buf:heap+168', 'pointer': 'eng'},
              {'addr': 'buf:eng+568', 'bytes': u64(0x1000)}]
    location = (TABLE | (INDEX << 7)) << 32
    for header in (0xff0, 0x1000):
        memory.append({'addr': f'buf:mem+{header}', 'bytes': u64(SIGNATURE | location)})
    if decoy:
        memory.append({'addr': 'buf:mem+0', 'bytes': u64(SIGNATURE | ((TABLE | (1 << 7)) << 32))})
        memory.append({'addr': 'buf:pages+56', 'pointer': 'heap'})
    if root is not None:
        memory.append({'addr': 'buf:pm+40', 'pointer': f'tree+{NODE * root}'})
    for n, (left, right) in (nodes or {}).items():
        for slot, child in ((8, left), (16, right)):
            if child is not None:
                memory.append({'addr': f'buf:tree+{NODE * n + slot}', 'pointer': f'tree+{NODE * child}'})
        memory.append({'addr': f'buf:tree+{NODE * n + 24}', 'pointer': f'heap+{heap_flags}'})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:self', 'rsi': block, 'rdx': size},
            'buffers': {'mem': {'size': 0x2000}, 'pages': {'size': 0x100}, 'heap': {'size': 0xb0},
                        'eng': {'size': 0x260}, 'pm': {'size': 0x30}, 'tree': {'size': 0x1000},
                        'self': {'size': 8}, 'moved': {'size': 0x10},
                        'guard': {'size': 8, 'bytes': 'fedcba9876543210'}},
            'memory': memory, 'stubs': list(stubs), 'imports': list(imports)}


def chain(length, last_right=None, extra=None):
    """Nodes 0..length-1, each the left child of the one before; the last gets `last_right`."""
    nodes = {n: (n + 1 if n + 1 < length else None, None) for n in range(length)}
    if last_right is not None:
        nodes[length - 1] = (None, last_right)
    nodes.update(extra or {})
    return nodes


def small_cases():
    low, high = 'buf:mem+0x810', 'buf:mem+0x1060'
    pair = [LOCK, UNLOCK]
    return [
        case('in_place_grow', low, 0x30, [in_place(low, 0x20)]),
        case('in_place_shrink', low, 0x10, [in_place(low, 0x40)]),
        case('in_place_rounds_up', low, 0x31, [in_place(low, 0x20)]),
        case('in_place_size_zero', low, 0, [in_place(low, 0x20)]),
        case('in_place_largest', low, 0x7ff, [in_place(low, 0x20)]),
        case('in_place_fails_moves', low, 0x100, [in_place(0, 0x20), moving()], imports=pair),
        # 0x800 is not resized in place (a resize scripted to succeed would show).
        case('too_long_moves', low, 0x800, [moving(), in_place(low, 0x20)], imports=pair),
        case('moving_fails', low, 0x900, [moving(0)], imports=pair),
        case('high_half', high, 0x40, [in_place(high, 0x20)]),
        case('decoy_header', low, 0x40, [in_place(low, 0x20)], decoy=True),
        case('thread_safe_in_place', low, 0x30, [in_place(low, 0x20)], imports=pair, thread_safe=True),
        case('thread_safe_moves', low, 0x100, [in_place(0, 0x20), moving()], imports=pair * 2, thread_safe=True),
        case('thread_safe_too_long', low, 0x1000, [moving()], imports=pair * 2, thread_safe=True),
    ]


def large_cases():
    pair = [LOCK, UNLOCK]
    # Nodes ordered by address: node n at tree+0x20*n. The key sits between nodes.
    key = lambda n: f'buf:tree+{NODE * n + 8}'
    exact = lambda n: f'buf:tree+{NODE * n}'
    imports = pair * 2
    return [
        case('root_is_block', exact(0), 0x1000, [large()], imports, nodes={0: (None, None)}, root=0),
        case('root_above_block', key(0), 0x1000, [large()], imports, nodes={1: (None, None)}, root=1),
        case('left_child_closer', key(2), 0x1000, [large()], imports,
             nodes={8: (3, None), 3: (None, None)}, root=8),
        case('left_child_below_key', key(4), 0x1000, [large()], imports,
             nodes={8: (3, None), 3: (None, None)}, root=8),
        case('right_branch_closer', key(2), 0x1000, [large()], imports,
             nodes={8: (9, 4), 9: (None, None), 4: (3, 5), 3: (None, None), 5: (None, None)}, root=8),
        case('right_branch_leftmost_walk', key(2), 0x1000, [large()], imports,
             nodes={8: (9, 6), 9: (None, None), 6: (None, 4), 4: (5, 3), 5: (None, None), 3: (None, None)}, root=8),
        case('right_branch_same_as_left', key(2), 0x1000, [large()], imports,
             nodes={8: (5, 5), 5: (None, None)}, root=8),
        case('exact_deeper', exact(3), 0x1000, [large()], imports,
             nodes={8: (3, 9), 3: (None, None), 9: (None, None)}, root=8),
        case('key_bit_chooses_right', key(40), 0x1000, [large()], imports,
             nodes=chain(CHAIN + 1, last_right=CHAIN + 20, extra={CHAIN + 20: (None, None)}), root=0),
        case('key_bit_right_exact', exact(CHAIN + 20), 0x1000, [large()], imports,
             nodes=chain(CHAIN + 1, last_right=CHAIN + 20, extra={CHAIN + 20: (None, None)}), root=0),
        # The walk follows the key's bits: it reaches the block's own node on the right of the chain's
        # end; going left instead would end below a farther right branch.
        case('key_bits_shift', exact(40), 0x1000, [large()], imports,
             nodes=chain(CHAIN + 1, extra={CHAIN: (30, 40), 30: (None, 60), 40: (None, None), 60: (None, None)}),
             root=0),
        # A right child that is the walk's own next step does not replace the right branch passed
        # before it, which holds the closest node.
        case('right_branch_kept_when_on_path', key(41), 0x1000, [large()], imports,
             nodes=chain(CHAIN + 1, extra={5: (6, 45), CHAIN: (None, 60), 45: (None, None), 60: (None, None)}),
             root=0),
        case('size_rounded_to_8', key(0), 0x1001, [large()], imports, nodes={1: (None, None)}, root=1),
        case('heap_flag_bits', key(0), 0x40, [large()], imports, nodes={1: (None, None)}, root=1, heap_flags=3),
        case('large_fails', key(0), 0x40, [large(0)], imports, nodes={1: (None, None)}, root=1),
        case('thread_safe_large', key(0), 0x40, [large()], pair + pair * 2, thread_safe=True,
             nodes={1: (None, None)}, root=1),
    ]


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    out = argv[0]
    os.makedirs(out, exist_ok=True)
    all_cases = small_cases() + large_cases()
    for c in all_cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(all_cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
