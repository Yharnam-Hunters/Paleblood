#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the config integer getter (game/frame_timing/config_get_int.cpp), for
tools/verify.py run.

usage: config_get_int_cases.py OUT_DIR

Constructed configs, never recorded state. The map lookup, the string copy, the marker search and
the reference expansion are scripted stubs; wcslen and wcstol are scripted imports. Cases cover a
missing key (several fallbacks), a found value kept inline or on the heap (capacity 7 and 8 at
the boundary), a copy that writes nothing, an allocator the copy replaces, no, one and several
reference markers, and parse results that are negative or carry bits above 32.
"""
import json
import os
import struct
import sys

ADDRESS = '0x024eba00'
GUARD_GOT = '0x057e3be0'
LOOKUP, EXPAND, ASSIGN, FIND = '0x024ed680', '0x024ed7e0', '0x02a279a0', '0x02d74270'
FREE_FN = '0x00400020'
NOT_FOUND = 0xffffffffffffffff
MARKER_LENGTH = 2


def q(v):
    return struct.pack('<Q', v & NOT_FOUND).hex()


def case(cid, found=True, fallback=4, parsed=12, heap=False, capacity=None, copies=True,
         finds=(NOT_FOUND,), expands=(), owns=1, swap_allocator=False):
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
         'args': {'rdi': 'buf:config', 'rsi': 'buf:key', 'rdx': hex(fallback & 0xffffffff)},
         'buffers': {'guard': {'size': 0x10, 'bytes': '5a' * 0x10},
                     'config': {'size': 0x10, 'bytes': '11' * 0x10},
                     'key': {'size': 0x38, 'bytes': '22' * 0x38},
                     'head': {'size': 0x90, 'bytes': '33' * 0x90},
                     'node': {'size': 0x90, 'bytes': '44' * 0x88 + bytes([owns]).hex() + '44' * 7},
                     'heap_text': {'size': 0x20, 'bytes': '3100320033003400350036003700380039000000' + '00' * 12}},
         'memory': [{'addr': GUARD_GOT, 'pointer': 'guard'}, {'addr': 'buf:config+0x8', 'pointer': 'head'}],
         'imports': [], 'stubs': []}
    for name in ('alloc', 'alloc2'):
        c['buffers'][name] = {'size': 8}
        c['buffers'][name + '_vt'] = {'size': 0x78}
        c['memory'] += [{'addr': f'buf:{name}', 'pointer': name + '_vt'},
                        {'addr': f'buf:{name}_vt+0x70', 'guest': FREE_FN}]
    c['memory'].append({'addr': 'buf:node+0x80', 'pointer': 'alloc'})
    c['stubs'].append({'address': LOOKUP, 'argc': 3, 'ret': 0,
                       'writes': [{'arg': 0, 'offset': 0x10, 'pointer': 'node' if found else 'head'}]})
    if not found:
        return c
    if copies:
        if capacity is None:
            capacity = 15 if heap else 7
        writes = [{'arg': 0, 'offset': 0x18, 'bytes': q(2)}, {'arg': 0, 'offset': 0x20, 'bytes': q(capacity)}]
        if capacity >= 8:
            writes.append({'arg': 0, 'offset': 0x8, 'pointer': 'heap_text'})
        else:
            writes.append({'arg': 0, 'offset': 0x8, 'bytes': '3100320000000000'})
        if swap_allocator:
            writes.append({'arg': 0, 'offset': 0x28, 'pointer': 'alloc2'})
        c['stubs'].append({'address': ASSIGN, 'argc': 4, 'ret': 0, 'writes': writes})
    else:
        c['stubs'].append({'address': ASSIGN, 'argc': 4, 'ret': 0})
    for at in finds:
        c['imports'].append({'name': 'wcslen', 'argc': 1, 'ret': MARKER_LENGTH})
        c['stubs'].append({'address': FIND, 'argc': 4, 'ret': hex(at)})
    for nxt in expands:
        c['stubs'].append({'address': EXPAND, 'argc': 3, 'ret': hex(nxt)})
    c['imports'].append({'name': 'wcstol', 'argc': 3, 'ret': hex(parsed & NOT_FOUND)})
    if copies and capacity >= 8:
        c['stubs'].append({'address': FREE_FN, 'argc': 2})
    return c


CASES = [
    case('missing_default', found=False),
    case('missing_zero', found=False, fallback=0),
    case('missing_all_bits', found=False, fallback=0xffffffff),
    case('missing_high_fallback', found=False, fallback=0x80000003),
    case('inline_value'),
    case('inline_zero', parsed=0),
    case('inline_negative', parsed=-5),
    case('inline_high_bits', parsed=0x100000007),
    case('inline_long_min', parsed=-(1 << 63)),
    case('inline_owns_zero', owns=0),
    case('heap_value', heap=True, parsed=123456789),
    case('capacity_seven', capacity=7),
    case('capacity_eight', capacity=8),
    case('capacity_huge', capacity=NOT_FOUND),
    case('copy_writes_nothing', copies=False, parsed=3),
    case('heap_allocator_swapped', heap=True, swap_allocator=True),
    case('inline_allocator_swapped', swap_allocator=True),
    case('one_reference', finds=(0, NOT_FOUND), expands=(3,), parsed=60),
    case('reference_at_end', finds=(5, NOT_FOUND), expands=(NOT_FOUND - 1,), heap=True),
    case('three_references', finds=(1, 6, 0, NOT_FOUND), expands=(4, 0, 9), heap=True, parsed=30),
    case('reference_expands_to_zero', finds=(2, NOT_FOUND), expands=(0,), parsed=-1),
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
