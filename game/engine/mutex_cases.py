#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine mutex (game/engine/mutex.cpp), for tools/verify.py run.

usage: mutex_cases.py OUT_DIR construct|initialize

The mutex object is a buffer: its vtable (a buffer whose third slot names a stub), the system
mutex's handle and the initialized flag, with garbage around them. The initialize asks through
that slot; the stub answers, sometimes disagreeing with the flag (or with garbage above the
bool's byte), so the virtual call is what counts. The PthreadMutex initialize (0x02483fe0) is
stubbed and writes a new handle, so a case shows whether the handle's address was passed.
"""
import json
import os
import sys

PTHREAD_INIT = '0x02483fe0'
IS_INITIALIZED = '0x00500200'  # a stub in the vtable's third slot
IS_INITIALIZED_SLOT = 0x10
ADDRESSES = {'construct': '0x02a3a840', 'initialize': '0x02a3a920'}
RETURNS = {'construct': 'void', 'initialize': 'i32'}

MASK = (1 << 64) - 1
ENOMEM, EINVAL, EAGAIN = 0x8002000c, 0x80020016, 0x80020023
HIGH = 0xdead_beef_0000_0000  # garbage above the 32-bit result
HANDLE, NEW_HANDLE = 0x1122334455667788, 0x0102030405060708


def word(value):
    return (value & MASK).to_bytes(8, 'little').hex()


def case(variant, cid, flag=0, virtual=None, stubs=(), vtable='a5' * 8):
    """virtual: what the vtable's is-initialized slot answers (None: not called)."""
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant],
         'args': {'rdi': 'buf:mutex+0x8'},
         'buffers': {'mutex': {'size': 0x28, 'bytes': 'a5' * 8 + vtable + word(HANDLE) + f'{flag:02x}' + '5a' * 15},
                     'vtable': {'size': 0x20, 'bytes': 'c3' * 0x20}},
         'memory': [], 'imports': [], 'stubs': []}
    if variant == 'initialize':
        c['memory'] = [{'addr': 'buf:mutex+0x8', 'pointer': 'vtable'},
                       {'addr': f'buf:vtable+{IS_INITIALIZED_SLOT:#x}', 'guest': IS_INITIALIZED}]
    if virtual is not None:
        c['stubs'].append({'address': IS_INITIALIZED, 'argc': 1, 'ret': hex(virtual)})
    c['stubs'] += list(stubs)
    return c


def create(ret):
    """The PthreadMutex initialize: returns `ret` and writes a new handle through its argument."""
    return {'address': PTHREAD_INIT, 'argc': 1, 'ret': hex(ret & MASK), 'argmem': [{'arg': 0, 'size': 8}],
            'writes': [{'arg': 0, 'bytes': word(NEW_HANDLE)}]}


def construct_cases():
    return [case('construct', 'clear'), case('construct', 'was_set', flag=1),
            case('construct', 'nonboolean', flag=0x80), case('construct', 'old_vtable', 1, vtable='00' * 8)]


def initialize_cases():
    def i(cid, ret=0, virtual=0, flag=0):
        stubs = [] if virtual & 0xff else [create(ret)]
        return case('initialize', cid, flag, virtual, stubs)
    return [i('already', virtual=1, flag=1), i('already_flag_clear', virtual=1), i('already_nonboolean', virtual=0x80),
            i('ok'), i('ok_high_bits', HIGH), i('out_of_resources', -1), i('out_of_resources_flag_set', -1, flag=1),
            i('other', 5), i('negative_high_bits', HIGH | 0xfffffffe), i('enomem', ENOMEM), i('einval', EINVAL),
            i('eagain', EAGAIN), i('flag_set_virtual_no', flag=1), i('nonboolean_flag_virtual_no', flag=0x80),
            i('virtual_upper_bits', virtual=0x100), i('virtual_upper_bits_fails', -1, virtual=0xff00)]


VARIANTS = {'construct': construct_cases, 'initialize': initialize_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in VARIANTS:
        print(__doc__, file=sys.stderr)
        return 2
    cases = VARIANTS[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
