#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for event_emk_add (0x016efd00), for tools/verify.py run.

usage: emk_add_cases.py OUT_DIR

Lists as chains of small node buffers (+0x28 id, +0x2c sub-id, +0x70 next). Covers: found in
either list, negative keys (no search), no id pointer, out of memory, insertion at the head, in
the middle and at the end of the second list (ties on the id ordered by sub-id), and the start
flag (low byte only).
"""
import json
import os
import struct
import sys

ALLOC_FN, ADVANCE_FN = '0x00400020', '0x004000a0'


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def u16(v):
    return struct.pack('<H', v & 0xffff).hex()


def case(cid, key, subkey, a=(), b=(), start=1, new_key=None, alloc=True):
    c = {'schema': 1, 'address': '0x016efd00', 'id': cid, 'returns': 'void',
         'args': {'rdi': 'buf:owner', 'rsi': hex(start), 'rdx': hex(subkey & 0xffffffff), 'rcx': 'buf:spec',
                  'r8': '0x1234', 'r9': '0x7'},
         'buffers': {'owner': {'size': 16}, 'spec': {'size': 16}, 'id': {'size': 4}, 'allocator': {'size': 8},
                     'allocator_vt': {'size': 0x60}, 'entry': {'size': 0xe0}, 'entry_vt': {'size': 0x18}},
         'memory': [], 'stubs': [], 'imports': []}
    m = c['memory']
    if key is None:
        m.append({'addr': 'buf:spec+8', 'bytes': '00' * 8})
    else:
        m += [{'addr': 'buf:spec+8', 'pointer': 'id'}, {'addr': 'buf:id', 'bytes': u32(key)}]
    for tag, off, nodes in (('a', 0, a), ('b', 8, b)):
        at = f'buf:owner+{off}'
        for n, (k, s) in enumerate(nodes):
            name = f'{tag}{n}'
            c['buffers'][name] = {'size': 0x78}
            m += [{'addr': at, 'pointer': name}, {'addr': f'buf:{name}+40', 'bytes': u32(k)},
                  {'addr': f'buf:{name}+44', 'bytes': u16(s)}]
            at = f'buf:{name}+112'
        m.append({'addr': at, 'bytes': '00' * 8})
    m += [{'addr': '0x05940420', 'pointer': 'allocator'}, {'addr': 'buf:allocator', 'pointer': 'allocator_vt'},
          {'addr': 'buf:allocator_vt+88', 'guest': ALLOC_FN}, {'addr': 'buf:entry_vt+16', 'guest': ADVANCE_FN}]
    nk = (key if key is not None else -1) if new_key is None else new_key
    c['stubs'] = [{'address': ALLOC_FN, 'argc': 3, 'ret': 'buf:entry' if alloc else '0x0'},
                  {'address': '0x016ec990', 'argc': 5,
                   'writes': [{'arg': 0, 'offset': 0, 'pointer': 'entry_vt'}, {'arg': 0, 'offset': 40, 'bytes': u32(nk)}]},
                  {'address': ADVANCE_FN, 'argc': 2, 'argi': [0, 1], 'argmem': [{'arg': 1, 'size': 12}]}]
    return c


CASES = [
    case('empty_lists', 5, 2),
    case('found_in_a', 5, 2, a=[(1, 0), (5, 2)]),
    case('found_in_b', 5, 2, b=[(9, 9), (5, 2), (1, 1)]),
    case('negative_subkey_no_search', 5, -1, a=[(5, -1)]),
    case('negative_key_no_search', -3, 2, a=[(-3, 2)]),
    case('no_id_pointer', None, 2, b=[(4, 0)]),
    case('same_key_other_subkey', 5, 3, a=[(5, 2)], b=[(5, 2)]),
    case('insert_head', 9, 0, b=[(5, 0), (1, 0)]),
    case('insert_middle', 4, 0, b=[(7, 0), (5, 0), (1, 0)]),
    case('insert_end', 0, 0, b=[(7, 0), (5, 0), (1, 0)]),
    case('tie_subkey_after_bigger', 5, 1, b=[(5, 3), (5, 0)]),
    case('tie_subkey_first', 5, 9, b=[(5, 3), (5, 0)]),
    case('constructor_changes_key', 5, 0, b=[(7, 0), (3, 0)], new_key=8),
    case('no_start', 5, 0, start=0),
    case('start_high_bits_only', 5, 0, start=0x100),
    case('no_memory', 5, 0, alloc=False),
    # Equal id and sub-id at insertion (the search is skipped, or the constructor sets the id).
    case('tie_equal_negative_subkey', 5, -1, b=[(5, -1), (2, 0)]),
    case('tie_equal_constructor_key', 4, 2, b=[(5, 2), (1, 0)], new_key=5),
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
