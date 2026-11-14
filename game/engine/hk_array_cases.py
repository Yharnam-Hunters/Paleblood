#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for Havok's array growth (game/engine/hk_array.cpp), for tools/verify.py run.

usage: hk_array_cases.py OUT_DIR reserve|more

The allocator is a buffer whose vtable's buffer allocation (slot 0x20) and reallocation (slot
0x30) are scripted stubs: the requested size is compared through its pointer and may be rounded up
(or made odd or negative) through it, and each has a spare scripted call. memcpy is a scripted
import (its arguments are compared). The array (data, size, capacity and flags) owns its buffer,
or does not (top bit set); the bit below it is set in some cases.

- reserve (0x00819040): counts 0, small and wrapping in 32 bits, element sizes 1, 4 and 0x30; the
  allocation failing on either path; sizes rounded up to a multiple and to a remainder, and a
  negative size (the division is signed); an allocation that changes the size before the copy.
- more (0x00819100): sizes 0 (one element), small and wrapping when doubled; the same
  allocator answers, including failures (stored as they are).
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'reserve': '0x00819040', 'more': '0x00819100'}
RETURNS = {'reserve': 'i32', 'more': 'void'}
GUARD_GOT = '0x057e3be0'
BUF_ALLOC, BUF_REALLOC = '0x00500600', '0x00500640'
NOT_OWNED = 0x80000000


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def case(mode, cid, size=3, capacity=4, flags=0, count=10, element=4, answer=None, fails=False, resize=None):
    """answer: the size in bytes the allocator writes back (None: as asked)."""
    c = {'schema': 1, 'address': ADDRESSES[mode], 'id': cid, 'returns': RETURNS[mode], 'args': {},
         'buffers': {'guard': {'size': 0x10, 'bytes': '5a' * 0x10}, 'allocator': {'size': 0x10, 'bytes': '3c' * 0x10},
                     'vtable': {'size': 0x40, 'bytes': '6c' * 0x40}, 'array': {'size': 0x18, 'bytes': 'e1' * 0x18},
                     'old': {'size': 0x40, 'bytes': bytes((0x20 + i * 3) & 0xff for i in range(0x40)).hex()},
                     'new': {'size': 0x40, 'bytes': 'a7' * 0x40}},
         'memory': [{'addr': GUARD_GOT, 'pointer': 'guard'},
                    {'addr': 'buf:allocator', 'pointer': 'vtable'},
                    {'addr': 'buf:vtable+0x20', 'guest': BUF_ALLOC},
                    {'addr': 'buf:vtable+0x30', 'guest': BUF_REALLOC},
                    {'addr': 'buf:array', 'pointer': 'old'},
                    {'addr': 'buf:array+0x8', 'bytes': d(size) + d(capacity | flags)}],
         'imports': [], 'stubs': []}
    c['args'] = {'rdi': 'buf:allocator', 'rsi': 'buf:array'}
    if mode == 'reserve':
        c['args'].update({'rdx': hex(count & 0xffffffff), 'rcx': hex(element)})
    else:
        c['args']['rdx'] = hex(element)
    writes = [] if answer is None else [{'arg': 3 if not flags & NOT_OWNED else 1, 'offset': 0,
                                         'bytes': d(answer)}]
    if resize is not None:
        # The array sits at +0x10 in the allocator's buffer, so the allocation can change its size.
        c['buffers']['allocator'] = {'size': 0x28, 'bytes': '3c' * 0x28}
        c['memory'] = [m for m in c['memory'] if not m['addr'].startswith('buf:array')]
        c['memory'] += [{'addr': 'buf:allocator+0x10', 'pointer': 'old'},
                        {'addr': 'buf:allocator+0x18', 'bytes': d(size) + d(capacity | flags)}]
        c['args']['rsi'] = 'buf:allocator+0x10'
        writes.append({'arg': 0, 'offset': 0x18, 'bytes': d(resize)})
    if flags & NOT_OWNED:
        stub = {'address': BUF_ALLOC, 'argc': 2, 'argmem': [{'arg': 1, 'size': 4}], 'writes': writes,
                'ret': 0 if fails else 'buf:new'}
        c['stubs'].append(stub)
        if not fails or mode == 'more':
            c['imports'].append({'name': 'memcpy', 'argc': 3, 'ret': 0 if fails else 'buf:new'})
    else:
        c['stubs'].append({'address': BUF_REALLOC, 'argc': 4, 'argu32': [2], 'argmem': [{'arg': 3, 'size': 4}],
                           'writes': writes, 'ret': 0 if fails else 'buf:new'})
    c['stubs'] += [{'address': BUF_ALLOC, 'argc': 2}, {'address': BUF_REALLOC, 'argc': 4}]
    return c


def reserve_cases():
    r = 'reserve'
    return [case(r, 'owned_grows'), case(r, 'owned_rounded_up', answer=48), case(r, 'owned_remainder', answer=50),
            case(r, 'owned_negative_answer', answer=-7), case(r, 'owned_fails', fails=True),
            case(r, 'owned_bit_30_set', flags=0x40000000, capacity=6),
            # Odd element sizes: with the bit kept, the old size the reallocation gets would differ.
            case(r, 'owned_bit_30_set_element_1', flags=0x40000000, capacity=6, element=1, count=33),
            case(r, 'owned_bit_30_set_element_3', flags=0x40000000, capacity=5, element=3, count=9),
            case(r, 'owned_count_zero', count=0),
            case(r, 'owned_count_wraps', count=0x40000001), case(r, 'owned_element_1', element=1, count=33),
            case(r, 'owned_element_0x30', element=0x30, count=3, capacity=2, size=1),
            case(r, 'owned_capacity_wraps', capacity=0x3fffffff, element=0x10),
            case(r, 'not_owned_copies', flags=NOT_OWNED), case(r, 'not_owned_rounded', flags=NOT_OWNED, answer=64),
            case(r, 'not_owned_fails', flags=NOT_OWNED, fails=True),
            case(r, 'not_owned_size_zero', flags=NOT_OWNED, size=0),
            case(r, 'not_owned_size_wraps', flags=NOT_OWNED, size=0x10000001, element=0x10),
            case(r, 'not_owned_negative_size', flags=NOT_OWNED, size=-2),
            case(r, 'not_owned_negative_answer', flags=NOT_OWNED, answer=-9, element=8),
            case(r, 'not_owned_size_changed_by_allocation', flags=NOT_OWNED, resize=7)]


def more_cases():
    m = 'more'
    return [case(m, 'owned_doubles'), case(m, 'owned_empty_one_element', size=0),
            case(m, 'owned_rounded_up', answer=40), case(m, 'owned_remainder', answer=27, element=8),
            case(m, 'owned_negative_answer', answer=-17), case(m, 'owned_fails_stored', fails=True),
            case(m, 'owned_bit_30_set', flags=0x40000000, capacity=3),
            case(m, 'owned_bit_30_set_element_1', flags=0x40000000, capacity=3, element=1),
            case(m, 'owned_bit_30_set_element_3', flags=0x40000000, capacity=5, element=3),
            case(m, 'owned_doubling_wraps', size=0x40000000),
            case(m, 'owned_product_wraps', size=0x08000001, element=0x10), case(m, 'owned_negative_size', size=-3),
            case(m, 'owned_element_0x30', element=0x30, size=1, capacity=1),
            case(m, 'not_owned_copies', flags=NOT_OWNED), case(m, 'not_owned_empty', flags=NOT_OWNED, size=0),
            case(m, 'not_owned_rounded', flags=NOT_OWNED, answer=32),
            case(m, 'not_owned_fails_stored', flags=NOT_OWNED, fails=True),
            case(m, 'not_owned_negative_answer', flags=NOT_OWNED, answer=-5),
            case(m, 'not_owned_size_changed_by_allocation', flags=NOT_OWNED, resize=9)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = reserve_cases() if sys.argv[2] == 'reserve' else more_cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
