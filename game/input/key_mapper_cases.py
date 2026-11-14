#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the key mapper's and the held set's lookups (game/input/key_mapper.cpp), for
tools/verify.py run.

usage: key_mapper_cases.py OUT_DIR button_index|alternate|contains

Each lookup is a std::map find on signed 32-bit keys. Cases: an empty map; the key found as the
root, a leaf, the smallest and the largest; missing below, above and between keys; negative keys
and the extremes (-0x80000000, 0x7fffffff, -1, which is also the "none" answer); a negative
key missing below only non-negative keys (an unsigned compare would find one); a stored value
of -1 and of 0x80000000; left and right chains and a full tree, so every branch of the walk runs.
contains: flags 0, 1, 2 and 0x80 (any nonzero byte counts) and 0x100 in a wider word (only the
byte is read). The maps are built by key_assign_cases.add_tree. Generated fixtures, not recorded
inputs.
"""
import json
import os
import struct
import sys

import key_assign_cases as trees

ADDRESSES = {'button_index': '0x01390ba0', 'alternate': '0x0138d060', 'contains': '0x013994a0'}
RETURNS = {'button_index': 'i32', 'alternate': 'i32', 'contains': 'i8'}


def int_value(data, off, value, c, nodes):
    struct.pack_into('<I', data, off + 0x20, value & 0xffffffff)


def lookup_case(variant, cid, entries, key, kind='balanced'):
    """entries: {key: value}; the map is placed where the variant's owner keeps it."""
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
         'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}
    owner = bytearray(b'\x5c' * 0x60)
    c['buffers']['owner'] = {'size': len(owner), 'bytes': owner.hex()}
    trees.add_tree(c, 'map', list(entries), list(entries.values()), 0x1c, int_value, kind)
    if variant == 'button_index':        # a pointer to the map object at +0x40
        c['memory'].append({'addr': 'buf:owner+0x40', 'pointer': 'map'})
    elif variant == 'alternate':         # the map object itself at +0x48: its head at +0x50
        c['memory'].append({'addr': 'buf:owner+0x50', 'pointer': 'map_nodes'})
    else:                                # the set's map object at +0x10: its head at +0x18
        c['memory'].append({'addr': 'buf:owner+0x18', 'pointer': 'map_nodes'})
    c['args'] = {'rdi': 'buf:owner', 'rsi': f'{key & 0xffffffff:#x}'}
    return c


def lookup_cases(variant):
    flag = variant == 'contains'
    seven = {k: (1 if flag else 100 + k) for k in (2, 4, 6, 8, 10, 12, 14)}
    out = [lookup_case(variant, 'empty', {}, 5),
           lookup_case(variant, 'root', seven, 8), lookup_case(variant, 'leaf', seven, 6),
           lookup_case(variant, 'smallest', seven, 2), lookup_case(variant, 'largest', seven, 14),
           lookup_case(variant, 'missing_below', seven, 1), lookup_case(variant, 'missing_above', seven, 15),
           lookup_case(variant, 'missing_between', seven, 9),
           lookup_case(variant, 'single_found', {7: 1 if flag else 3}, 7),
           lookup_case(variant, 'single_missing', {7: 1 if flag else 3}, 8)]
    signed = {-0x80000000: 1, -5: 2, -1: 3, 0: 4, 3: 5, 0x7fffffff: 6}
    if flag:
        signed = {k: 1 for k in signed}
    for name, key in (('key_min', -0x80000000), ('key_minus_5', -5), ('key_minus_1', -1), ('key_zero', 0),
                      ('key_max', 0x7fffffff), ('key_minus_2_missing', -2), ('key_4_missing', 4)):
        out.append(lookup_case(variant, name, signed, key))
    # A negative key below only non-negative keys: unsigned, it would compare above them.
    out.append(lookup_case(variant, 'negative_below_positive', {3: 1 if flag else 7, 9: 1 if flag else 8}, -1))
    for kind in ('left_chain', 'right_chain'):
        for name, key in (('found', 10), ('missing', 11), ('first', 2), ('last', 14)):
            out.append(lookup_case(variant, f'{kind}_{name}', seven, key, kind))
    if flag:
        for value in (0, 1, 2, 0x80, 0x100, 0xff00):
            out.append(lookup_case(variant, f'flag_{value:#x}', {3: 1, 6: value, 9: 1}, 6))
    else:
        out += [lookup_case(variant, 'value_minus_1', {3: 1, 6: -1, 9: 1}, 6),
                lookup_case(variant, 'value_int_min', {3: 1, 6: -0x80000000, 9: 1}, 6),
                lookup_case(variant, 'value_zero', {3: 1, 6: 0, 9: 1}, 6)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = lookup_cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
