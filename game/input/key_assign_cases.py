#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the key assign queries (game/input/key_assign.cpp), for tools/verify.py run.

usage: key_assign_cases.py OUT_DIR pressed|analog|first_free

- pressed / analog: no mapper (pressed only: analog does not check it and reads through the
  mapper's stub all the same); the mapper's button index -1, the key assign disabled; the key
  missing from the key state map (smaller, larger and between the keys), its entry disabled or
  suppressed (and both); for pressed, the button's bit set or clear in words 0 and 1 (bits 0,
  31, 32, 63), then the alternate input -1 or present / absent in the held set (stubs); for
  analog, values 0, negative, a NaN with a payload and a negative button index -2 (read before
  the array, sign-extended).
- first_free: empty tables; ids 0 to 2 taken; all eight taken; only 1; duplicates; 7 down to 1;
  0 to 6; ids out of range (8, 100, 0xffffffff); trees of several shapes (left and right
  chains, a full tree), so the walk climbs from right children; the second and third table.
The mapper's lookups (0x01390ba0, 0x0138d060) and the held set (0x013994a0) are stubs; the
assignment ids are read by the game's own leaf getter. Generated fixtures, not recorded inputs.
"""
import json
import os
import struct
import sys

ADDRESSES = {'pressed': '0x01391e30', 'analog': '0x01391f60', 'first_free': '0x013983e0'}
RETURNS = {'pressed': 'i8', 'analog': 'f32', 'first_free': 'i32'}
MAPPER_INDEX, MAPPER_ALTERNATE, SET_CONTAINS, LEAF_ID = '0x01390ba0', '0x0138d060', '0x013994a0', '0x013918d0'
NODE = 0x30


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def shape(keys, kind):
    """(root index, {index: (left, right)}) over keys sorted ascending, by tree shape."""
    n = len(keys)
    children = {}
    if kind == 'left_chain':          # root is the largest, each left child the next smaller
        for i in range(n):
            children[i] = (i - 1 if i > 0 else None, None)
        return n - 1, children
    if kind == 'right_chain':
        for i in range(n):
            children[i] = (None, i + 1 if i + 1 < n else None)
        return 0, children

    def build(lo, hi):
        if lo > hi:
            return None
        mid = (lo + hi) // 2
        children[mid] = (build(lo, mid - 1), build(mid + 1, hi))
        return mid
    return build(0, n - 1), children


def add_tree(c, name, keys, values, key_offset, write_value, kind='balanced'):
    """An MSVC map in buffer NAME_nodes (head first, then one node per key) and NAME (the map
    object, head pointer at +8). write_value(node_bytes, value, c, node_name_offset) fills a node."""
    keys = list(keys)
    order = sorted(range(len(keys)), key=lambda i: keys[i])
    sorted_keys = [keys[i] for i in order]
    sorted_values = [values[i] for i in order]
    nodes = name + '_nodes'
    data = bytearray(b'\x33' * NODE * (len(keys) + 1))
    data[0x18], data[0x19] = 1, 1          # head: black, nil

    def at(i):                             # node i (0-based key order) -> offset in nodes
        return NODE * (i + 1)

    def link(off, field, target):
        c['memory'].append({'addr': f'buf:{nodes}+{off + field}', 'pointer': f'{nodes}+{target}'})
    root, children = shape(sorted_keys, kind) if keys else (None, {})
    parent = {}
    for i, (l, r) in children.items():
        for ch in (l, r):
            if ch is not None:
                parent[ch] = i
    for i, k in enumerate(sorted_keys):
        off = at(i)
        l, r = children[i]
        link(off, 0x0, at(l) if l is not None else 0)
        link(off, 0x8, at(parent[i]) if i in parent else 0)
        link(off, 0x10, at(r) if r is not None else 0)
        data[off + 0x18], data[off + 0x19] = 0, 0
        struct.pack_into('<I', data, off + key_offset, k)
        write_value(data, off, sorted_values[i], c, nodes)
    link(0, 0x0, at(0) if keys else 0)                       # head->left: smallest
    link(0, 0x8, at(root) if keys else 0)                    # head->parent: root
    link(0, 0x10, at(len(keys) - 1) if keys else 0)          # head->right: largest
    c['buffers'][nodes] = {'size': len(data), 'bytes': data.hex()}
    c['buffers'][name] = {'size': 0x10, 'bytes': '00' * 0x10}
    c['memory'].append({'addr': f'buf:{name}+8', 'pointer': nodes})


# ---- pressed / analog -------------------------------------------------------------------------

def key_state(data, off, value, c, nodes):
    data[off + 0x20], data[off + 0x21] = value


def query_case(variant, cid, key=10, button=5, enabled=1, mapper=True, states=None, kind='balanced',
               pressed=(), alternate=None, held=None, analog=None):
    c = base(variant, cid)
    keys = bytearray(b'\x5a' * 0x48)
    keys[0x20] = enabled
    c['buffers']['keys'] = {'size': len(keys), 'bytes': keys.hex()}
    c['buffers']['mapper'] = {'size': 0x60, 'bytes': '77' * 0x60}
    c['memory'].append({'addr': 'buf:keys+0x28', 'pointer': 'mapper'} if mapper else
                       {'addr': 'buf:keys+0x28', 'bytes': '00' * 8})
    states = {10: (1, 0), 20: (1, 0), 4: (1, 0)} if states is None else states
    add_tree(c, 'states', list(states), list(states.values()), 0x1c, key_state, kind)
    c['memory'].append({'addr': 'buf:keys+0x40', 'pointer': 'states'})
    c['buffers']['input'] = {'size': 0x48, 'bytes': '44' * 0x48}
    c['memory'].append({'addr': 'buf:keys+0x10', 'pointer': 'input'})
    c['buffers']['state'] = {'size': 0x60, 'bytes': '66' * 0x60}
    c['memory'].append({'addr': 'buf:input+0x8', 'pointer': 'state'})
    bits = bytearray(8)
    for b in pressed:
        bits[b // 8] |= 1 << (b % 8)
    c['buffers']['bits'] = {'size': len(bits), 'bytes': bits.hex()}
    c['memory'].append({'addr': 'buf:state+0x58', 'pointer': 'bits'})
    values = analog or [0.25 * i - 1 for i in range(8)]
    floats = b''.join(struct.pack('<f', v) if isinstance(v, float) else struct.pack('<I', v) for v in values)
    c['buffers']['analog'] = {'size': len(floats), 'bytes': floats.hex()}
    c['memory'].append({'addr': 'buf:state+0x28', 'pointer': 'analog+8'})   # index -2 reads analog[0]
    c['args'] = {'rdi': 'buf:keys', 'rsi': f'{key:#x}'}
    if mapper or variant == 'analog':
        c['stubs'].append({'address': MAPPER_INDEX, 'ret': button & 0xffffffff, 'argc': 2})
    if alternate is not None:
        c['stubs'].append({'address': MAPPER_ALTERNATE, 'ret': alternate & 0xffffffff, 'argc': 2})
    if held is not None:
        c['stubs'].append({'address': SET_CONTAINS, 'ret': held, 'argc': 2})
    return c


def query_cases(variant):
    out = [query_case(variant, 'button_none', button=-1), query_case(variant, 'disabled', enabled=0),
           query_case(variant, 'key_missing_smaller', key=1), query_case(variant, 'key_missing_larger', key=30),
           query_case(variant, 'key_missing_between', key=15),
           query_case(variant, 'entry_disabled', states={10: (0, 0), 20: (1, 0)}),
           query_case(variant, 'entry_suppressed', states={10: (1, 1), 20: (1, 0)}),
           query_case(variant, 'entry_disabled_and_suppressed', states={10: (0, 1)}),
           query_case(variant, 'empty_map', states={}),
           query_case(variant, 'flags_any_nonzero', states={10: (2, 0), 20: (1, 0)}, pressed=(5,)),
           query_case(variant, 'suppressed_any_nonzero', states={10: (1, 0x80)}, pressed=(5,))]
    for kind in ('left_chain', 'right_chain'):
        keys = {k: (1, 0) for k in (2, 4, 6, 8, 10, 12, 14)}
        out.append(query_case(variant, f'{kind}_found', key=8, states=keys, kind=kind, pressed=(5,)))
        out.append(query_case(variant, f'{kind}_missing', key=9, states=keys, kind=kind, pressed=(5,)))
    out.append(query_case(variant, 'key_large', key=0xfffffff0, states={0xfffffff0: (1, 0), 3: (1, 0)}, pressed=(5,)))
    if variant == 'pressed':
        out += [query_case(variant, 'no_mapper', mapper=False),
                query_case(variant, 'pressed_bit_5', pressed=(5,)),
                query_case(variant, 'pressed_bit_0', button=0, pressed=(0,)),
                query_case(variant, 'pressed_bit_31', button=31, pressed=(31,)),
                query_case(variant, 'pressed_bit_32', button=32, pressed=(32,)),
                query_case(variant, 'pressed_bit_63', button=63, pressed=(63,)),
                query_case(variant, 'neighbour_bits_only', button=32, pressed=(31, 33, 0), alternate=-1),
                query_case(variant, 'alternate_none', pressed=(4, 6), alternate=-1),
                query_case(variant, 'alternate_held', alternate=3, held=1),
                query_case(variant, 'alternate_not_held', alternate=3, held=0),
                query_case(variant, 'alternate_zero_held', alternate=0, held=0x101),
                query_case(variant, 'alternate_held_bit_byte_only', alternate=7, held=0x100)]
    else:
        out += [query_case(variant, 'no_mapper_read_anyway', mapper=False),
                query_case(variant, 'value_index_5'), query_case(variant, 'value_index_0', button=0),
                query_case(variant, 'value_negative', button=1, analog=[0.0, -0.75] + [0.5] * 6),
                query_case(variant, 'value_nan_payload', button=3, analog=[0.0] * 3 + [0x7fa12345] + [0.0] * 4),
                query_case(variant, 'value_negative_zero', button=2, analog=[0.0, 0.0, 0x80000000] + [1.0] * 5),
                query_case(variant, 'index_minus_2', button=-2, analog=[0x3f123456] + [0.0] * 7)]
    return out


# ---- first_free -------------------------------------------------------------------------------

def assignment(data, off, value, c, nodes):
    name = f'assignment_{off // NODE}'
    a = bytearray(b'\x61' * 0x28)
    struct.pack_into('<I', a, 0x24, value)
    c['buffers'][name] = {'size': len(a), 'bytes': a.hex()}
    c['memory'].append({'addr': f'buf:{nodes}+{off + 0x28}', 'pointer': name})


def free_case(cid, ids, table=0, kind='balanced'):
    c = base('first_free', cid)
    owner = bytearray(b'\x0f' * (0x38 + 8 * 4))
    c['buffers']['owner'] = {'size': len(owner), 'bytes': owner.hex()}
    add_tree(c, 'table', range(100, 100 + len(ids)), list(ids), 0x20, assignment, kind)
    c['memory'].append({'addr': f'buf:owner+{0x38 + 8 * table}', 'pointer': 'table'})
    c['args'] = {'rdi': 'buf:owner', 'rsi': f'{table:#x}'}
    return c


def free_cases():
    return [free_case('empty', []), free_case('ids_0_to_2', [0, 1, 2]), free_case('all_taken', list(range(8))),
            free_case('all_taken_reversed', list(range(7, -1, -1))),
            free_case('only_1', [1]), free_case('duplicates', [0, 0, 2, 2]), free_case('7_down_to_1', [7, 6, 5, 4, 3, 2, 1]),
            free_case('0_to_6', list(range(7))), free_case('out_of_range', [8, 100, 0xffffffff, 0]),
            free_case('left_chain', [0, 1, 2, 4, 5], kind='left_chain'),
            free_case('right_chain', [0, 1, 2, 4, 5], kind='right_chain'),
            free_case('full_tree', [3, 0, 6, 1, 2, 5, 7], kind='balanced'),
            free_case('second_table', [0, 2], table=1), free_case('third_table_all', list(range(8)), table=2)]


def cases(variant):
    return free_cases() if variant == 'first_free' else query_cases(variant)


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
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
