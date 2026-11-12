#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's read-write lock (game/engine/read_write_lock.cpp), for
tools/verify.py run.

usage: read_write_lock_cases.py OUT_DIR KIND
  KIND: read_lock, unlock_0247f540, unlock_0247f5c0, unlock_0247f710, write_lock, try_write_lock, create,
        finalize, destroy_base, delete

Each case builds a lock (`lock`: a fake vtable whose created query is a scripted stub, and a
handle, then the created flag). The system rwlock calls are scripted imports with the results under test (upper bits
included); the error reports are stubs, as many as the case makes (worked out here).
"""
import json
import os
import struct
import sys

FATAL_ERROR, IS_CREATED = '0x024b55b0', '0x0247f820'   # IS_CREATED: any game function, stubbed
ADDRESSES = {'read_lock': '0x0247f640', 'unlock_0247f540': '0x0247f540', 'unlock_0247f5c0': '0x0247f5c0',
             'unlock_0247f710': '0x0247f710', 'write_lock': '0x0247f3d0', 'try_write_lock': '0x0247f4a0',
             'create': '0x0247f2b0', 'finalize': '0x0247f250', 'destroy_base': '0x0247f1f0', 'delete': '0x0247f190'}
EPERM, EDEADLK, EINVAL, EAGAIN, ETIMEDOUT = 0x80020001, 0x8002000b, 0x80020016, 0x80020023, 0x8002003c
ENOMEM, EFAULT, EBUSY = 0x8002000c, 0x8002000e, 0x80020010
CREATE_CHAIN = [EPERM, EBUSY, EINVAL, EFAULT]
# Which functions ask the vtable whether the lock is created, and which read the flag.
ASKS = {'read_lock', 'unlock_0247f540', 'unlock_0247f5c0', 'unlock_0247f710', 'write_lock', 'try_write_lock',
        'create', 'finalize'}


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def reports(kind, result, created, flag):
    r = result & 0xffffffff
    if kind == 'create':
        if created or r in (ENOMEM, EAGAIN):
            return 0
        return len(CREATE_CHAIN) - CREATE_CHAIN.index(r) if r in CREATE_CHAIN else 0
    if kind in ('finalize', 'destroy_base', 'delete'):
        runs = created if kind == 'finalize' else flag
        return 1 if runs and r == EINVAL else 0
    n = 0 if created else 1
    if kind == 'read_lock':
        return n + {EINVAL: 3, EAGAIN: 2, EDEADLK: 1}.get(r, 0)
    if kind == 'write_lock':
        return n + (0 if r in (0, ETIMEDOUT) else {EINVAL: 3, EDEADLK: 2}.get(r, 1))
    if kind == 'try_write_lock':
        return n + {EINVAL: 2, EDEADLK: 1}.get(r, 0)
    return n + (1 if r == EINVAL else 0)


def case(kind, cid, result=0, created=True, timeout=-1, flag=True):
    args = {'rdi': 'buf:lock'}
    imps = []
    if kind in ('read_lock', 'write_lock'):
        name = 'Rdlock' if kind == 'read_lock' else 'Wrlock'
        timed = 'Timedrdlock' if kind == 'read_lock' else 'Timedwrlock'
        imps.append({'name': f'scePthreadRwlock{name}', 'argc': 1, 'ret': hex(result)} if timeout == -1
                    else {'name': f'scePthreadRwlock{timed}', 'argc': 2, 'ret': hex(result)})
        args['rsi'] = timeout & 0xffffffff
    elif kind == 'try_write_lock':
        imps.append({'name': 'scePthreadRwlockTrywrlock', 'argc': 1, 'ret': hex(result)})
    elif kind == 'create':
        if not created:
            imps.append({'name': 'scePthreadRwlockInit', 'argc': 3, 'ret': hex(result)})
    elif kind in ('finalize', 'destroy_base', 'delete'):
        if (created if kind == 'finalize' else flag):
            imps.append({'name': 'scePthreadRwlockDestroy', 'argc': 1, 'ret': hex(result)})
        if kind == 'delete':
            imps.append({'name': '_ZdlPv', 'argc': 1})
    else:
        imps.append({'name': 'scePthreadRwlockUnlock', 'argc': 1, 'ret': hex(result)})
    stubs = [{'address': IS_CREATED, 'argc': 1, 'ret': int(created)}] if kind in ASKS else []
    stubs += [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports(kind, result, created, flag))]
    return {'schema': 1, 'address': ADDRESSES[kind], 'id': cid,
            'returns': 'void' if kind in ('destroy_base', 'delete') else 'i32', 'args': args,
            'buffers': {'lock': {'size': 0x18, 'bytes': '2d' * 0x18}, 'vtable': {'size': 0x20}},
            'memory': [{'addr': 'buf:lock+0', 'pointer': 'vtable'}, {'addr': 'buf:lock+8', 'bytes': u64(0x7777000012345678)},
                       {'addr': 'buf:lock+16', 'bytes': '01' if flag else '00'},
                       {'addr': 'buf:vtable+16', 'guest': IS_CREATED}],
            'stubs': stubs, 'imports': imps}


RESULTS = [('ok', 0), ('positive', 9), ('eperm', EPERM), ('edeadlk', EDEADLK), ('einval', EINVAL),
           ('eagain', EAGAIN), ('etimedout', ETIMEDOUT), ('other_negative', 0x80020002),
           ('lowest_negative', 0x80000000), ('minus_one', 0xffffffff), ('above_etimedout', 0x8002003d),
           ('einval_upper_bits', 0xfedc000000000000 | EINVAL), ('eperm_upper_bits', 0x100000000 | EPERM),
           ('enomem', ENOMEM), ('efault', EFAULT), ('ebusy', EBUSY), ('between_efault_ebusy', 0x8002000f),
           ('above_einval', 0x80020017)]


def make(kind):
    if kind == 'create':
        out = [case(kind, name, result=r, created=False) for name, r in RESULTS]
        return out + [case(kind, 'already_created', created=True), case(kind, 'flag_clear', created=False, flag=False)]
    if kind in ('finalize', 'destroy_base', 'delete'):
        out = [case(kind, name, result=r) for name, r in RESULTS]
        out += [case(kind, 'flag_clear', flag=False), case(kind, 'flag_clear_einval', flag=False, result=EINVAL)]
        if kind == 'finalize':
            out += [case(kind, 'not_created', created=False), case(kind, 'not_created_flag_set', created=False, result=EINVAL)]
        return out
    out = [case(kind, name, result=r) for name, r in RESULTS]
    out += [case(kind, 'not_created', created=False), case(kind, 'not_created_einval', created=False, result=EINVAL)]
    if kind in ('read_lock', 'write_lock'):
        out += [case(kind, 'timed', timeout=1000), case(kind, 'timed_zero', timeout=0),
                case(kind, 'timed_out', timeout=5, result=ETIMEDOUT), case(kind, 'timed_einval', timeout=5, result=EINVAL),
                case(kind, 'timeout_minus_two', timeout=-2)]
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
