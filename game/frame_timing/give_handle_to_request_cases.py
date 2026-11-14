#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for giving a pass-on request the gate's handle (game/frame_timing/give_handle_to_request.cpp),
for tools/verify.py run.

usage: give_handle_to_request_cases.py OUT_DIR server|local

The request (0xe0 bytes, filled with a pattern) is followed in one buffer by the handle's object.
Its vtable's send (slot 0x28) and, for the local one, slot 0x38 are scripted stubs that compare
both when called, so the handle and kind the request holds and the object's marks by then show. The objects' destroy (slot 0) is a stub that compares the object it
is given; in two cases the handle lives inside the object being dropped and the destroy rewrites
it (to a third object, or to null), so the handle's second read shows. Each stub has a spare
scripted call. Covers: an empty handle (false, nothing touched); a request already holding the
object; an empty request; a held object with references left, with its last one (destroyed), and
with a count at 0 or below (reported); kinds 0, 1, 2, -1, the 32-bit ends and garbage above the low
32 bits; an object whose count starts at 0; and a missing FrpgNetMan (reported, not read again).
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

MODES = {'server': '0x02269e30', 'local': '0x02269d40'}
FRPG_NET_MAN, FATAL_ERROR = '0x0593b120', '0x024b55b0'
SEND, PREPARE, DESTROY = '0x00500700', '0x00500740', '0x00400080'
OBJECT_SIZE, REQUEST_SIZE = 0x130, 0xe0
# The request and the handle's object share one buffer, so the request's stubs see the marks too.
BLOCK_SIZE, OBJECT = REQUEST_SIZE + OBJECT_SIZE, f'block+{REQUEST_SIZE:#x}'
HELD_AT_IN_OLD = 0x20  # where the handle sits inside the dropped object in the rewrite cases


def d(v):
    return struct.pack('<i', v).hex()


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(mode, cid, kind=1, handle=True, held='old', old_refs=2, new_refs=1, rewrite=None, net=True):
    """held: None (empty request), 'same' (already the handle's object) or 'old' (another object).
    rewrite: None, 'third' or 'null': the old object's destroy rewrites the handle (which then sits
    inside the old object)."""
    c = {'schema': 1, 'address': MODES[mode], 'id': cid, 'returns': 'i8', 'args': {},
         'buffers': {'holder': {'size': 0x10, 'bytes': '00' * 8 + q(0x1122334455667788)},
                     'old': {'size': OBJECT_SIZE, 'bytes': '4e' * OBJECT_SIZE},
                     'third': {'size': OBJECT_SIZE, 'bytes': '9d' * OBJECT_SIZE},
                     'block': {'size': BLOCK_SIZE, 'bytes': 'c5' * REQUEST_SIZE + 'b3' * OBJECT_SIZE},
                     'vtable': {'size': 0x40, 'bytes': '6c' * 0x40},
                     'objvtable': {'size': 0x10, 'bytes': '2d' * 0x10},
                     'net': {'size': 0x10}},
         'memory': [{'addr': 'buf:block', 'pointer': 'vtable'},
                    {'addr': 'buf:vtable+0x28', 'guest': SEND},
                    {'addr': 'buf:vtable+0x38', 'guest': PREPARE},
                    {'addr': 'buf:objvtable', 'guest': DESTROY}],
         'imports': [], 'stubs': []}
    for name, at, refs in (('block', REQUEST_SIZE, new_refs), ('old', 0, old_refs), ('third', 0, 1)):
        c['memory'] += [{'addr': f'buf:{name}+{at:#x}', 'pointer': 'objvtable'},
                        {'addr': f'buf:{name}+{at + 8:#x}', 'bytes': d(refs)}]
    holder = f'buf:old+{HELD_AT_IN_OLD}' if rewrite else 'buf:holder'
    c['memory'].append({'addr': holder, 'pointer': OBJECT} if handle else {'addr': holder, 'bytes': '00' * 8})
    held_value = {'old': {'pointer': 'old'}, 'same': {'pointer': OBJECT}, None: {'bytes': '00' * 8}}[held]
    c['memory'].append({'addr': 'buf:block+0xc8', **held_value})
    c['memory'].append({'addr': FRPG_NET_MAN, 'pointer': 'net'} if net else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8})
    c['args'] = {'rdi': 'buf:block', 'rsi': holder, 'rdx': hex(kind & 0xffffffffffffffff)}
    if not net:
        c['stubs'].append({'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3]})
    if handle and held == 'old':
        if old_refs == 1:
            writes = []
            if rewrite == 'third':
                writes = [{'arg': 0, 'offset': HELD_AT_IN_OLD, 'pointer': 'third'}]
            elif rewrite == 'null':
                writes = [{'arg': 0, 'offset': HELD_AT_IN_OLD, 'bytes': '00' * 8}]
            c['stubs'].append({'address': DESTROY, 'argc': 1, 'argi': [0], 'argmem': [{'arg': 0, 'size': 0x10}],
                               'writes': writes})
        elif old_refs <= 0:
            c['stubs'].append({'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]})
    if handle:
        if mode == 'local':
            c['stubs'].append({'address': PREPARE, 'argc': 1, 'argi': [0],
                               'argmem': [{'arg': 0, 'size': BLOCK_SIZE}]})
        c['stubs'].append({'address': SEND, 'argc': 2, 'argi': [0], 'argu32': [1],
                           'argmem': [{'arg': 0, 'size': BLOCK_SIZE}]})
    c['stubs'] += [{'address': SEND, 'argc': 2}, {'address': PREPARE, 'argc': 1}, {'address': DESTROY, 'argc': 1}]
    return c


def cases(mode):
    m = mode
    out = [case(m, 'no_handle', handle=False), case(m, 'no_handle_empty_request', handle=False, held=None),
           case(m, 'no_handle_missing_net', handle=False, net=False),
           case(m, 'already_held', held='same', new_refs=2), case(m, 'already_held_one_reference', held='same', new_refs=1),
           case(m, 'empty_request', held=None), case(m, 'empty_request_count_zero', held=None, new_refs=0),
           case(m, 'old_has_references_left', old_refs=3),
           case(m, 'old_last_reference', old_refs=1),
           case(m, 'old_last_reference_rewrites_handle', old_refs=1, rewrite='third'),
           case(m, 'old_last_reference_clears_handle', old_refs=1, rewrite='null'),
           case(m, 'old_last_reference_keeps_handle', old_refs=1, rewrite='same'),
           case(m, 'old_count_zero_reported', old_refs=0), case(m, 'old_count_negative_reported', old_refs=-5),
           case(m, 'missing_net', net=False), case(m, 'missing_net_empty_request', held=None, net=False)]
    for kind in (0, 2, -1, 0x7fffffff, -0x80000000):
        out.append(case(m, f'kind_{kind & 0xffffffff:#010x}', kind=kind))
    out.append(case(m, 'kind_upper_garbage', kind=0xdeadbeef00000002 - (1 << 64)))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in MODES:
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
