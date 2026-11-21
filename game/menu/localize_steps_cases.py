#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the language object's release steps (game/menu/localize_steps.cpp), for tools/verify.py run.

usage: localize_steps_cases.py OUT_DIR release_loads|release_load|release_final_loads

The language object is an invented buffer whose load pointers (+0x28, +0x30, +0x38, +0x40, +0x58,
+0x60, +0x68) point at invented load buffers, each with its own state (+0xc) and reference count
(+0x10). A release that reaches 0 looks up the owning allocator (0x0247b720), destroys the load
through its vtable slot 0x8 and frees it through the allocator's slot 0x70: all three are scripted
stubs, compared in order with their arguments, so the order of the releases shows. 0x018e7870 is a
scripted stub too.

- release_loads (0x01d52e50): all four loads awaited (3) with counts 1, 2, 0 and 0x80000000 (the
  last two do not reach 0); each load in turn at states 0, 2, 4, 0x103, 0x80000003, -1 and INT_MAX
  while the others are awaited; the object's own state at 0, 4, -1 and INT_MAX (wraps); the first
  and last pointer naming the same load (counts 1 and 2); the first load being the object itself,
  whose destroy changes the last pointer (the pointer is read again after a freeing release).
- release_load (0x01d53450): the load at +0x58 awaited with counts 1, 2, 0 and 0x80000000, at the
  other states, the object's state at several values, and the load being the object itself.
- release_final_loads (0x01d534b0): counts of the loads at +0x68 and +0x60 in every pair of 1, 2,
  0 and 0x80000000, at states that are not awaited (they are not checked), the two pointers naming
  the same load, the object's state at several values, and the first load being the object
  itself, whose destroy changes the second pointer (read after the first release).
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'release_loads': '0x01d52e50', 'release_load': '0x01d53450', 'release_final_loads': '0x01d534b0'}
ALLOCATOR_OF = '0x0247b720'
AFTER_FIRST_LOADS = '0x018e7870'
# Fake vtable targets (any address: the case stubs them).
DESTROY, HEAP_FREE = '0x00500100', '0x00500200'
STATE, REFERENCES = 0x0c, 0x10
LOADS = {'l28': 0x28, 'l30': 0x30, 'l38': 0x38, 'l40': 0x40, 'l58': 0x58, 'l60': 0x60, 'l68': 0x68}
FILLS = {'l28': '28', 'l30': '30', 'l38': '38', 'l40': '40', 'l58': '58', 'l60': '60', 'l68': '68', 'other': '7e'}
AWAITED = 3
OTHER_STATES = (0, 2, 4, 0x103, 0x80000003, 0xffffffff, 0x7fffffff)
COUNTS = (1, 2, 0, 0x80000000)
LANGUAGE_SIZE = 0x70


def load_buffer(name, state, references):
    data = bytearray.fromhex(FILLS[name] * 0x18)
    struct.pack_into('<II', data, STATE, state, references)
    return {'size': 0x18, 'bytes': data.hex()}


class Case:
    def __init__(self, variant, cid, language_state=1):
        data = bytearray.fromhex('3c' * LANGUAGE_SIZE)
        struct.pack_into('<I', data, STATE, language_state)
        self.c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void',
                  'args': {'rdi': 'buf:language'}, 'imports': [], 'stubs': [],
                  'buffers': {'language': {'size': LANGUAGE_SIZE, 'bytes': data.hex()},
                              'load_vtable': {'size': 0x10, 'bytes': 'c1' * 0x10},
                              'heap': {'size': 8}, 'heap_vtable': {'size': 0x78}},
                  'memory': [{'addr': 'buf:load_vtable+0x8', 'guest': DESTROY},
                             {'addr': 'buf:heap+0', 'pointer': 'heap_vtable'},
                             {'addr': 'buf:heap_vtable+0x70', 'guest': HEAP_FREE}]}

    def load(self, slot, state=AWAITED, references=2, buffer=None):
        """Points the language object's slot at a load buffer (its own unless buffer names another)."""
        name = buffer or slot
        b = self.c['buffers']
        if name not in b:
            b[name] = load_buffer(name, state, references)
            self.c['memory'].append({'addr': f'buf:{name}+0', 'pointer': 'load_vtable'})
        self.c['memory'].append({'addr': f'buf:language+{LOADS[slot]:#x}', 'pointer': name})

    def language_is_load(self, slot, state=AWAITED, references=1):
        """Makes the language object itself the load in this slot (its vtable is a load vtable)."""
        data = bytearray.fromhex(self.c['buffers']['language']['bytes'])
        struct.pack_into('<II', data, STATE, state, references)
        self.c['buffers']['language']['bytes'] = data.hex()
        self.c['memory'] += [{'addr': 'buf:language+0', 'pointer': 'load_vtable'},
                             {'addr': f'buf:language+{LOADS[slot]:#x}', 'pointer': 'language'}]

    def frees(self, writes=None):
        """Scripts one release that reaches 0: owning allocator, destroy, free."""
        destroy = {'address': DESTROY, 'argc': 1, 'argi': [0]}
        if writes:
            destroy['writes'] = writes
        self.c['stubs'] += [{'address': ALLOCATOR_OF, 'argc': 1, 'argi': [0], 'ret': 'buf:heap'}, destroy,
                            {'address': HEAP_FREE, 'argc': 2, 'argi': [0, 1]}]

    def after_first_loads(self):
        self.c['stubs'].append({'address': AFTER_FIRST_LOADS, 'argc': 0})


def release_loads_cases():
    out = []
    for first in COUNTS:
        for last in COUNTS:
            c = Case('release_loads', f'awaited_counts_{first:x}_{last:x}')
            c.load('l28', references=first)
            c.load('l30', references=1)
            c.load('l38', references=1)
            c.load('l40', references=last)
            for count in (first, last):
                if count == 1:
                    c.frees()
            c.after_first_loads()
            out.append(c.c)
    for slot in ('l28', 'l30', 'l38', 'l40'):
        for state in OTHER_STATES:
            c = Case('release_loads', f'{slot}_state_{state:x}')
            for s in ('l28', 'l30', 'l38', 'l40'):
                c.load(s, state=state if s == slot else AWAITED, references=1)
            out.append(c.c)
    for state in (0, 4, 0xffffffff, 0x7fffffff):
        c = Case('release_loads', f'language_state_{state:x}', language_state=state)
        for s in ('l28', 'l30', 'l38', 'l40'):
            c.load(s, references=2)
        c.after_first_loads()
        out.append(c.c)
    for count in (1, 2):
        c = Case('release_loads', f'first_and_last_same_count_{count}')
        c.load('l28', references=count)
        c.load('l30')
        c.load('l38')
        c.load('l40', buffer='l28')
        c.frees()   # count 1: the first release frees, the second wraps; count 2: the second frees
        c.after_first_loads()
        out.append(c.c)
    for other_count in (1, 2):
        c = Case('release_loads', f'first_is_language_destroy_moves_last_{other_count}')
        c.language_is_load('l28')
        c.load('l30')
        c.load('l38')
        c.load('l40', references=1)
        c.c['buffers']['other'] = load_buffer('other', 0, other_count)
        c.c['memory'].append({'addr': 'buf:other+0', 'pointer': 'load_vtable'})
        c.frees(writes=[{'arg': 0, 'offset': 0x40, 'pointer': 'other'}])
        if other_count == 1:
            c.frees()
        c.after_first_loads()
        out.append(c.c)
    return out


def release_load_cases():
    out = []
    for count in COUNTS:
        c = Case('release_load', f'awaited_count_{count:x}')
        c.load('l58', references=count)
        c.load('l60', references=1)
        if count == 1:
            c.frees()
        out.append(c.c)
    for state in OTHER_STATES:
        c = Case('release_load', f'state_{state:x}')
        c.load('l58', state=state, references=1)
        out.append(c.c)
    for state in (0, 4, 0xffffffff, 0x7fffffff):
        c = Case('release_load', f'language_state_{state:x}', language_state=state)
        c.load('l58', references=1)
        c.frees()
        out.append(c.c)
    c = Case('release_load', 'load_is_language')
    c.language_is_load('l58')
    c.frees()
    out.append(c.c)
    return out


def release_final_loads_cases():
    out = []
    for first in COUNTS:
        for second in COUNTS:
            c = Case('release_final_loads', f'counts_{first:x}_{second:x}')
            c.load('l68', state=0, references=first)
            c.load('l60', state=2, references=second)
            for count in (first, second):
                if count == 1:
                    c.frees()
            out.append(c.c)
    for count in (1, 2):
        c = Case('release_final_loads', f'same_load_count_{count}')
        c.load('l68', state=0, references=count)
        c.load('l60', buffer='l68')
        c.frees()
        out.append(c.c)
    for state in (0, 3, 0xffffffff, 0x7fffffff):
        c = Case('release_final_loads', f'language_state_{state:x}', language_state=state)
        c.load('l68', references=2)
        c.load('l60', references=2)
        out.append(c.c)
    c = Case('release_final_loads', 'first_is_language_destroy_moves_second')
    c.language_is_load('l68', state=0)
    c.load('l60', references=2)
    c.c['buffers']['other'] = load_buffer('other', 0, 1)
    c.c['memory'].append({'addr': 'buf:other+0', 'pointer': 'load_vtable'})
    c.frees(writes=[{'arg': 0, 'offset': 0x60, 'pointer': 'other'}])
    c.frees()
    out.append(c.c)
    return out


GENERATORS = {'release_loads': release_loads_cases, 'release_load': release_load_cases,
              'release_final_loads': release_final_loads_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in GENERATORS:
        print(__doc__, file=sys.stderr)
        return 2
    out = GENERATORS[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
