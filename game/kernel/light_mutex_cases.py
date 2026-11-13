#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's light mutex (game/kernel/light_mutex.cpp), for tools/verify.py run.

usage: light_mutex_cases.py OUT_DIR construct|delete|destruct|finalize|initialize|lock|try_lock|unlock

The mutex object is a buffer: its vtable (a buffer whose third slot names a stub), the system
mutex's handle and the initialized flag, with garbage around them. Members that ask whether the
mutex is initialized go through that slot; the stub answers, sometimes disagreeing with the flag
(or with garbage above the bool's byte), so the virtual call is what counts. The system calls, the
PthreadMutex initialize (0x02483fe0), the C++ delete and the fatal error (0x024b55b0) are stubbed.
The fatal error never returns in the game; the stubs return, so the reports that follow in the
original's chain run as well.
"""
import json
import os
import sys

FATAL, DELETE, PTHREAD_INIT = '0x024b55b0', '0x02fbe588', '0x02483fe0'
IS_INITIALIZED = '0x00500200'  # a stub in the vtable's third slot
IS_INITIALIZED_SLOT = 0x10
DESTROY, LOCK, UNLOCK, TRYLOCK = '0x02fbfee8', '0x02fbfef8', '0x02fbff08', '0x02fc0cb8'
ADDRESSES = {'construct': '0x0247ecb0', 'delete': '0x0247ecc0', 'destruct': '0x0247ed40', 'finalize': '0x0247edc0',
             'initialize': '0x0247ee40', 'lock': '0x0247ee90', 'try_lock': '0x0247ef80', 'unlock': '0x0247f070'}
RETURNS = {'construct': 'void', 'delete': 'void', 'destruct': 'void'}

MASK = (1 << 64) - 1
EPERM, EDEADLK, EBUSY = 0x80020001, 0x8002000b, 0x80020010
EINVAL, EAGAIN, ETIMEDOUT, EIO = 0x80020016, 0x80020023, 0x8002003c, 0x80020005
HIGH = 0xdead_beef_0000_0000  # garbage above the 32-bit result
HANDLE = 0x1122334455667788


def word(value):
    return (value & MASK).to_bytes(8, 'little').hex()


def reports(count):
    return [{'address': FATAL, 'argc': 3, 'ret': '0x0'} for _ in range(count)]


def call(address, ret):
    return {'address': address, 'argc': 1, 'ret': hex(ret & MASK), 'argmem': [{'arg': 0, 'size': 8}]}


def case(variant, cid, flag=0, virtual=None, stubs=()):
    """virtual: what the vtable's is-initialized slot answers (None: not called)."""
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS.get(variant, 'i32'),
         'args': {'rdi': 'buf:mutex+0x8'},
         'buffers': {'mutex': {'size': 0x28, 'bytes': 'a5' * 8 + '00' * 8 + word(HANDLE) + f'{flag:02x}' + '5a' * 15},
                     'vtable': {'size': 0x20, 'bytes': 'c3' * 0x20}},
         'memory': [{'addr': 'buf:mutex+0x8', 'pointer': 'vtable'},
                    {'addr': f'buf:vtable+{IS_INITIALIZED_SLOT:#x}', 'guest': IS_INITIALIZED}],
         'imports': [], 'stubs': []}
    if virtual is not None:
        c['stubs'].append({'address': IS_INITIALIZED, 'argc': 1, 'ret': hex(virtual)})
    c['stubs'] += list(stubs)
    return c


def destroy_reports(ret):
    low = ret & 0xffffffff
    if low in (0, EBUSY):
        return 0
    return 2 if low == EINVAL else 1


DESTROY_RESULTS = [('ok', 0), ('ebusy', EBUSY), ('einval', EINVAL), ('eio', EIO), ('eperm', EPERM), ('positive', 1),
                   ('ebusy_high_bits', HIGH | EBUSY), ('ok_high_bits', HIGH)]


def construct_cases():
    return [case('construct', 'clear'), case('construct', 'was_set', flag=1), case('construct', 'nonboolean', flag=0x80)]


def delete_cases():
    out = [case('delete', 'not_initialized', 0, stubs=[{'address': DELETE, 'argc': 1, 'ret': '0x0'}])]
    for cid, ret in DESTROY_RESULTS:
        out.append(case('delete', cid, 1, stubs=[call(DESTROY, ret)] + reports(destroy_reports(ret))
                        + [{'address': DELETE, 'argc': 1, 'ret': '0x0'}]))
    return out


def destruct_cases():
    out = [case('destruct', 'not_initialized', 0), case('destruct', 'nonboolean_flag', 2, stubs=[call(DESTROY, 0)])]
    out += [case('destruct', cid, 1, stubs=[call(DESTROY, ret)] + reports(destroy_reports(ret)))
            for cid, ret in DESTROY_RESULTS]
    return out


def finalize_cases():
    out = [case('finalize', 'not_initialized', 0, 0), case('finalize', 'flag_set_virtual_no', 1, 0),
           case('finalize', 'virtual_upper_bits', 1, 0x100), case('finalize', 'virtual_yes_flag_clear', 0, 1,
                                                                    [call(DESTROY, 0)])]
    out += [case('finalize', cid, 1, 1, [call(DESTROY, ret)] + reports(destroy_reports(ret)))
            for cid, ret in DESTROY_RESULTS]
    return out


def initialize_cases():
    def i(cid, ret=0, virtual=0, flag=0):
        stubs = [] if virtual & 0xff else [call(PTHREAD_INIT, ret)]
        return case('initialize', cid, flag, virtual, stubs)
    return [i('already', virtual=1, flag=1), i('already_flag_clear', virtual=1), i('ok'), i('ok_high_bits', HIGH),
            i('out_of_resources', -1), i('other', 5), i('flag_set_virtual_no', flag=1), i('virtual_upper_bits', virtual=0x100)]


def chain_cases(variant, address, results):
    def l(cid, ret, fatal, virtual=1):
        return case(variant, cid, 1, virtual, reports(0 if virtual & 0xff else 1) + [call(address, ret)] + reports(fatal))
    out = [l(cid, ret, fatal) for cid, ret, fatal in results]
    out += [l('not_created', 0, 0, 0), l('virtual_upper_bits', 0, 0, 0x100)]
    return out


LOCK_RESULTS = [('ok', 0, 0), ('ok_high_bits', HIGH, 0), ('etimedout', ETIMEDOUT, 0), ('etimedout_high_bits', HIGH | ETIMEDOUT, 0),
        ('edeadlk', EDEADLK, 4), ('einval', EINVAL, 3), ('eagain', EAGAIN, 2), ('ebusy', EBUSY, 1), ('eperm', EPERM, 1),
        ('positive', 1, 1), ('below_etimedout', ETIMEDOUT - 1, 1), ('above_etimedout', ETIMEDOUT + 1, 1)]
TRY_LOCK_RESULTS = [('ok', 0, 0), ('ok_high_bits', HIGH, 0), ('ebusy', EBUSY, 0), ('ebusy_high_bits', HIGH | EBUSY, 0),
            ('edeadlk', EDEADLK, 4), ('einval', EINVAL, 3), ('eagain', EAGAIN, 2), ('etimedout', ETIMEDOUT, 1),
            ('eperm', EPERM, 1), ('positive', 1, 1), ('below_eagain', EAGAIN - 1, 1), ('above_eagain', EAGAIN + 1, 1)]
UNLOCK_RESULTS = [('ok', 0, 0), ('ok_high_bits', HIGH, 0), ('eperm', EPERM, 0), ('eperm_high_bits', HIGH | EPERM, 0),
          ('einval', EINVAL, 1), ('ebusy', EBUSY, 1), ('positive', 1, 1), ('above_eperm', EPERM + 1, 1)]

VARIANTS = {'construct': construct_cases, 'delete': delete_cases, 'destruct': destruct_cases,
            'finalize': finalize_cases, 'initialize': initialize_cases,
            'lock': lambda: chain_cases('lock', LOCK, LOCK_RESULTS),
            'try_lock': lambda: chain_cases('try_lock', TRYLOCK, TRY_LOCK_RESULTS),
            'unlock': lambda: chain_cases('unlock', UNLOCK, UNLOCK_RESULTS)}


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
