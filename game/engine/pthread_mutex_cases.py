#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's mutex (game/engine/pthread_mutex.cpp), for tools/verify.py run.

usage: pthread_mutex_cases.py OUT_DIR FUNCTION
  FUNCTION: create, finalize, destroy_base, lock, try_lock, unlock

Each case builds a mutex (`mutex`: a fake vtable whose initialization query is a scripted stub, a
handle and the initialized flag) or, for create, a handle slot. The system mutex calls are
scripted imports with the results under test, including upper bits a 32-bit compare must ignore;
the error reports are stubs, as many as the result reports (worked out here).
"""
import json
import os
import struct
import sys

FATAL_ERROR, IS_INITIALIZED = '0x024b55b0', '0x0247ee80'
ADDRESSES = {'create': '0x02483fe0', 'finalize': '0x0247edc0', 'destroy_base': '0x0247ed40',
             'lock': '0x0247ee90', 'try_lock': '0x0247ef80', 'unlock': '0x0247f070'}
RETURNS = {'destroy_base': 'void'}
EPERM, EDEADLK, ENOMEM, EBUSY = 0x80020001, 0x8002000b, 0x8002000c, 0x80020010
EINVAL, EAGAIN, ETIMEDOUT = 0x80020016, 0x80020023, 0x8002003c
HANDLE = 0x1122334455667788


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def low(result):
    return result & 0xffffffff


def lock_reports(result, busy_code):
    """Reports after a lock or try-lock result (the chain from the error's own message on)."""
    r = low(result)
    if r in (0, busy_code):
        return 0
    return {EDEADLK: 4, EINVAL: 3, EAGAIN: 2}.get(r, 1)


def import_call(name, argc, ret=0):
    return {'name': name, 'argc': argc, 'ret': hex(ret)}


def case(function, cid, result=0, queried=True, flag=True):
    """result: the system call's result (create: scePthreadMutexInit's)."""
    stubs, imports = [], []
    buffers = {'mutex': {'size': 0x20, 'bytes': '6d' * 0x20}, 'mutex_vtable': {'size': 0x30}}
    memory = [{'addr': 'buf:mutex+0', 'pointer': 'mutex_vtable'},
              {'addr': 'buf:mutex+8', 'bytes': u64(HANDLE)},
              {'addr': 'buf:mutex+16', 'bytes': '01' if flag else '00'},
              {'addr': 'buf:mutex_vtable+16', 'guest': IS_INITIALIZED}]
    args = {'rdi': 'buf:mutex'}
    reports = 0
    if function == 'create':
        buffers = {'handle': {'size': 0x10, 'bytes': '6d' * 0x10}}
        memory = []
        args = {'rdi': 'buf:handle+8'}
        imports = [import_call('scePthreadMutexattrInit', 1), import_call('scePthreadMutexattrSettype', 2),
                   import_call('scePthreadMutexInit', 3, result), import_call('scePthreadMutexattrDestroy', 1)]
        r = low(result)
        reports = 0 if r in (0, ENOMEM, EAGAIN) else 2 if r == EINVAL else 1
    elif function in ('finalize', 'destroy_base'):
        if function == 'finalize':
            stubs.append({'address': IS_INITIALIZED, 'argc': 1, 'ret': int(queried)})
            runs = queried
        else:
            runs = flag
        if runs:
            imports.append(import_call('scePthreadMutexDestroy', 1, result))
            r = low(result)
            reports = 0 if r in (0, EBUSY) else 2 if r == EINVAL else 1
    else:
        stubs.append({'address': IS_INITIALIZED, 'argc': 1, 'ret': int(queried)})
        reports = 0 if queried else 1
        name = {'lock': 'scePthreadMutexLock', 'try_lock': 'scePthreadMutexTrylock',
                'unlock': 'scePthreadMutexUnlock'}[function]
        imports.append(import_call(name, 1, result))
        r = low(result)
        if function == 'lock':
            reports += 0 if r == ETIMEDOUT else lock_reports(result, None)
        elif function == 'try_lock':
            reports += lock_reports(result, EBUSY)
        else:
            reports += 0 if r in (0, EPERM) else 1
    stubs += [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports)]
    return {'schema': 1, 'address': ADDRESSES[function], 'id': cid, 'returns': RETURNS.get(function, 'i32'),
            'args': args, 'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': imports}


COMMON_ERRORS = [('ok', 0), ('positive', 5), ('eperm', EPERM), ('edeadlk', EDEADLK), ('enomem', ENOMEM),
                 ('ebusy', EBUSY), ('einval', EINVAL), ('eagain', EAGAIN), ('etimedout', ETIMEDOUT),
                 ('other_negative', 0x80020002), ('lowest_negative', 0x80000000), ('minus_one', 0xffffffff),
                 ('above_etimedout', 0x8002003d), ('ok_upper_bits', 0xdeadbeef00000000),
                 ('einval_upper_bits', 0x1234567800000000 | EINVAL)]


def make(function):
    out = [case(function, name, result=r) for name, r in COMMON_ERRORS]
    if function == 'finalize':
        out += [case(function, 'not_initialized', queried=False),
                case(function, 'query_false_flag_set', queried=False, flag=True),
                case(function, 'query_true_flag_clear', result=0, flag=False),
                case(function, 'query_true_flag_clear_busy', result=EBUSY, flag=False)]
    elif function == 'destroy_base':
        out += [case(function, 'flag_clear', flag=False), case(function, 'flag_clear_einval', result=EINVAL, flag=False)]
    elif function != 'create':
        out += [case(function, 'not_initialized', queried=False),
                case(function, 'not_initialized_einval', queried=False, result=EINVAL),
                case(function, 'not_initialized_flag_clear', queried=False, flag=False, result=EDEADLK)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    made = make(sys.argv[2])
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
