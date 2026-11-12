#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's read-write lock (game/engine/read_write_lock.cpp), for
tools/verify.py run.

usage: read_write_lock_cases.py OUT_DIR read_lock|unlock_0247f540|unlock_0247f5c0|unlock_0247f710

Each case builds a lock (`lock`: a fake vtable whose created query is a scripted stub, and a
handle). The system rwlock calls are scripted imports with the results under test (upper bits
included); the error reports are stubs, as many as the case makes (worked out here).
"""
import json
import os
import struct
import sys

FATAL_ERROR, IS_CREATED = '0x024b55b0', '0x0247f820'   # IS_CREATED: any game function, stubbed
ADDRESSES = {'read_lock': '0x0247f640', 'unlock_0247f540': '0x0247f540', 'unlock_0247f5c0': '0x0247f5c0',
             'unlock_0247f710': '0x0247f710'}
EPERM, EDEADLK, EINVAL, EAGAIN, ETIMEDOUT = 0x80020001, 0x8002000b, 0x80020016, 0x80020023, 0x8002003c


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def reports(kind, result, created):
    r = result & 0xffffffff
    n = 0 if created else 1
    if kind == 'read_lock':
        return n + {EINVAL: 3, EAGAIN: 2, EDEADLK: 1}.get(r, 0)
    return n + (1 if r == EINVAL else 0)


def case(kind, cid, result=0, created=True, timeout=-1):
    if kind == 'read_lock':
        imp = ({'name': 'scePthreadRwlockRdlock', 'argc': 1, 'ret': hex(result)} if timeout == -1
               else {'name': 'scePthreadRwlockTimedrdlock', 'argc': 2, 'ret': hex(result)})
        args = {'rdi': 'buf:lock', 'rsi': timeout & 0xffffffff}
    else:
        imp = {'name': 'scePthreadRwlockUnlock', 'argc': 1, 'ret': hex(result)}
        args = {'rdi': 'buf:lock'}
    stubs = [{'address': IS_CREATED, 'argc': 1, 'ret': int(created)}]
    stubs += [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports(kind, result, created))]
    return {'schema': 1, 'address': ADDRESSES[kind], 'id': cid, 'returns': 'i32', 'args': args,
            'buffers': {'lock': {'size': 0x10}, 'vtable': {'size': 0x20}},
            'memory': [{'addr': 'buf:lock+0', 'pointer': 'vtable'}, {'addr': 'buf:lock+8', 'bytes': u64(0x7777000012345678)},
                       {'addr': 'buf:vtable+16', 'guest': IS_CREATED}],
            'stubs': stubs, 'imports': [imp]}


RESULTS = [('ok', 0), ('positive', 9), ('eperm', EPERM), ('edeadlk', EDEADLK), ('einval', EINVAL),
           ('eagain', EAGAIN), ('etimedout', ETIMEDOUT), ('other_negative', 0x80020002),
           ('lowest_negative', 0x80000000), ('minus_one', 0xffffffff), ('above_etimedout', 0x8002003d),
           ('einval_upper_bits', 0xfedc000000000000 | EINVAL), ('eperm_upper_bits', 0x100000000 | EPERM)]


def make(kind):
    out = [case(kind, name, result=r) for name, r in RESULTS]
    out += [case(kind, 'not_created', created=False), case(kind, 'not_created_einval', created=False, result=EINVAL)]
    if kind == 'read_lock':
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
