#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine list's push_back copies (game/engine/list_push_back.cpp), for
tools/verify.py run.

usage: list_push_back_cases.py OUT_DIR ADDRESS   (one of the 30 copies, e.g. 0x0179b2a0)

Each case lays out a list (proxy, sentinel, size, allocator) over nodes in one buffer (`nodes`:
the sentinel, then the list's nodes, each 0x18 bytes) and a value to append (in `value`, between
other bytes so the width read shows, or one of the list's own values). The allocator's aligned
allocation (slot 0x58) is a scripted stub that compares the size and alignment and answers a new
node filled with a pattern; it has a spare scripted call. Covers: empty, one, two and five nodes;
a sentinel whose previous node is not the last one in the chain (the link goes through the new
node's own previous pointer); a value that is the list's own last value; values with every byte
set and with the sign bit; and sizes just below the largest (the size check). A list at its
largest size throws (no return) and a failed allocation faults in the game; neither is cased.
Generated fixtures, not game data.
"""
import json
import os
import struct
import sys

A64 = ('0x0179b2a0 0x017d5790 0x0182fd80 0x01860ea0 0x01860f90 0x01868b10 0x0187de40 0x0189e6a0 0x018ad810 '
       '0x018bf1d0 0x018c6cd0 0x019851b0 0x01b91b30 0x01c4c2d0 0x01c4c3c0 0x020f0380 0x020f2ff0 0x020fed20 '
       '0x02105de0 0x0219fff0 0x0223f000 0x02390550 0x023a07e0 0x02432780 0x02db58d0 0x02dc9180 0x02dd3540').split()
A32 = '0x017dc0f0 0x02222210 0x023a9520'.split()
WIDTH = {**{a: 8 for a in A64}, **{a: 4 for a in A32}}
ALLOCATE, ALLOCATE_SLOT = '0x00500200', 0x58
NODE = 0x18
MAX_SIZE = ((1 << 64) - 1) // NODE - 1  # the largest size


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(address, cid, count=2, value=0x1122334455667788, size=None, stray_prev=False, own_value=False):
    """count: the nodes in the chain; size: the list's size word (default: count)."""
    width = WIDTH[address]
    nodes = bytearray(b'\x4b' * (NODE * (count + 2)))
    memory = []
    # Node i sits at NODE * i (0: the sentinel). The chain is sentinel -> 1 -> ... -> count -> sentinel.
    for i in range(count + 1):
        nxt = (i + 1) % (count + 1)
        prv = (i - 1) % (count + 1)
        memory.append({'addr': f'buf:nodes+{NODE * i:#x}', 'pointer': f'nodes+{NODE * nxt:#x}'})
        memory.append({'addr': f'buf:nodes+{NODE * i + 8:#x}', 'pointer': f'nodes+{NODE * prv:#x}'})
        if i:
            nodes[NODE * i + 0x10:NODE * i + 0x10 + width] = (0x1000 * i + 0x55).to_bytes(width, 'little')
    if stray_prev:
        # The sentinel points back at a spare node (after the chain) whose next is not the sentinel.
        spare = count + 1
        memory.append({'addr': 'buf:nodes+8', 'pointer': f'nodes+{NODE * spare:#x}'})
        memory.append({'addr': f'buf:nodes+{NODE * spare:#x}', 'pointer': 'nodes+0x18'})
    buffers = {'nodes': bytes(nodes), 'list': bytes.fromhex('a1' * 0x20), 'allocator': bytes.fromhex('3c' * 0x10),
               'vtable': bytes.fromhex('e7' * 0x80), 'node': bytes.fromhex('d2' * NODE),
               'value': b'\xee' * 4 + (value & ((1 << (8 * width)) - 1)).to_bytes(width, 'little') + b'\xcc' * 4}
    memory += [{'addr': 'buf:list+8', 'pointer': 'nodes+0x0'},
               {'addr': 'buf:list+0x10', 'bytes': q(count if size is None else size)},
               {'addr': 'buf:list+0x18', 'pointer': 'allocator+0'},
               {'addr': 'buf:allocator', 'pointer': 'vtable+0'},
               {'addr': f'buf:vtable+{ALLOCATE_SLOT:#x}', 'guest': ALLOCATE}]
    item = f'buf:nodes+{NODE * count + 0x10:#x}' if own_value else 'buf:value+4'
    stubs = [{'address': ALLOCATE, 'argc': 3, 'argi': [0, 1, 2], 'ret': 'buf:node'},
             {'address': ALLOCATE, 'argc': 3}]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:list', 'rsi': item},
            'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in buffers.items()},
            'memory': memory, 'imports': [], 'stubs': stubs}


def cases(address):
    a = address
    return [case(a, 'empty', count=0), case(a, 'one', count=1), case(a, 'two'), case(a, 'five', count=5),
            case(a, 'stray_prev', count=2, stray_prev=True), case(a, 'stray_prev_empty', count=0, stray_prev=True),
            case(a, 'own_last_value', count=3, own_value=True),
            case(a, 'all_bits', value=(1 << 64) - 1), case(a, 'sign_bit', value=1 << 63 | 1 << 31),
            case(a, 'zero_value', value=0),
            case(a, 'size_below_largest', size=MAX_SIZE - 1), case(a, 'size_two_below_largest', size=MAX_SIZE - 2),
            case(a, 'size_out_of_step', count=3, size=7)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in WIDTH:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
