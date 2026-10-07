#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for input_pad_step_01972900 (0x01972900), for tools/verify.py run.

usage: pad_step_cases.py OUT_DIR

Builds real search trees in the MSVC node layout (left +0x0, right +0x10, nil +0x19, key
+0x20, value +0x28) with the header as the nil node. Keys are guest addresses (so they order the
same on both sides) or raw numbers below/above the image, including one with the sign bit set
(the lookup compares signed). Covers both keys present in different positions, either missing,
a lower bound that lands on a larger key, an empty map, and each of the three conditions that
skip the second step.
"""
import json
import os
import struct
import sys

K1, K2 = 0x059464a8, 0x059566c4


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def build(keys):
    """keys: sorted list of ('guest', addr) or ('raw', int). Returns (buffers, memory)."""
    buffers = {'nodeh': {'size': 56}}
    memory = [{'addr': 'buf:nodeh+25', 'bytes': '01'}, {'addr': 'buf:holder+8', 'pointer': 'nodeh'}]

    def sub(lo, hi):
        if lo >= hi:
            return 'nodeh'
        mid = (lo + hi) // 2
        name = f'n{mid}'
        kind, k = keys[mid]
        buffers[name] = {'size': 56}
        memory.append({'addr': f'buf:{name}+25', 'bytes': '00'})
        memory.append({'addr': f'buf:{name}+32', 'guest': hex(k)} if kind == 'guest'
                      else {'addr': f'buf:{name}+32', 'bytes': struct.pack('<Q', k & 0xffffffffffffffff).hex()})
        memory.append({'addr': f'buf:{name}+40', 'bytes': struct.pack('<Q', 0x1000 + mid).hex()})
        memory.append({'addr': f'buf:{name}+0', 'pointer': sub(lo, mid)})
        memory.append({'addr': f'buf:{name}+16', 'pointer': sub(mid + 1, hi)})
        return name
    memory.append({'addr': 'buf:nodeh+8', 'pointer': sub(0, len(keys))})
    return buffers, memory


def case(cid, keys, state=1, owner=True, blocked=0):
    buffers, memory = build(sorted(keys, key=lambda kv: struct.unpack('<q', struct.pack('<Q', (kv[1] if kv[0] == 'raw' else 0x800000000 + kv[1]) & 0xffffffffffffffff))[0]))
    buffers.update({'self': {'size': 224}, 'manager': {'size': 64}, 'holder': {'size': 16}, 'owner': {'size': 8}})
    memory = [{'addr': '0x058b31f0', 'pointer': 'manager'}, {'addr': 'buf:manager+56', 'pointer': 'holder'},
              {'addr': 'buf:self+196', 'bytes': u32(state)}, {'addr': 'buf:self+200', 'bytes': u32(blocked)},
              {'addr': 'buf:self+208', 'pointer': 'owner'} if owner else {'addr': 'buf:self+208', 'bytes': '00' * 8}] + memory
    stubs = [{'address': '0x017064b0', 'argc': 1},
             {'address': '0x01972cd0', 'argc': 1, 'ret': '0x7777'},
             {'address': '0x021145b0', 'argc': 2, 'argmem': [{'arg': 1, 'size': 20}]}]
    return {'schema': 1, 'address': '0x01972900', 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:self'},
            'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': []}


G = lambda a: ('guest', a)
R = lambda v: ('raw', v)
both = [G(0x00400100), G(K1), G(0x05950000), G(K2), G(0x059d0000)]
CASES = [
    case('both_present', both),
    case('only_keys', [G(K1), G(K2)]),
    case('many_keys', [G(0x00400000 + i * 0x100000) for i in range(40)] + [G(K1), G(K2)]),
    case('first_missing', [G(0x00400100), G(K2)]),
    case('second_missing', [G(K1), G(0x059d0000)]),
    case('lands_on_larger_key', [G(K1 + 8), G(K2 + 8)]),
    case('empty_map', []),
    case('state_not_1', both, state=2),
    case('state_0', both, state=0),
    case('no_owner', both, owner=False),
    case('blocked', both, blocked=1),
    case('raw_keys_around', [R(0x10), G(K1), R(0x7fffffffffffffff), G(K2)]),
    case('negative_key', [R(-5), G(K1), G(K2)]),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
