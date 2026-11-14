#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's semaphore (game/engine/semaphore.cpp), for tools/verify.py run.

usage: semaphore_cases.py OUT_DIR wait|signal

Each case builds a semaphore (`sema`: a vtable word, a handle with upper bits set and the created
flag). sceKernelWaitSema / sceKernelSignalSema are scripted imports with the result under test
(upper bits included, which a 32-bit compare must ignore); the error reports are stubs, as many as
the result reports (worked out here).
"""
import json
import os
import struct
import sys

FATAL_ERROR = '0x024b55b0'
WAIT, SIGNAL = '0x02a3ae00', '0x02a3b0d0'
ESRCH, EACCES, EFAULT, EINVAL = 0x80020003, 0x8002000d, 0x8002000e, 0x80020016
ETIMEDOUT, ECANCELED = 0x8002003c, 0x80020055
HANDLE = 0xabcd000000000042
WAIT_CHAIN = [ESRCH, ECANCELED, EFAULT, EACCES, EINVAL]


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def wait_reports(result):
    r = result & 0xffffffff
    if r in (0, ETIMEDOUT):
        return 0
    if r in WAIT_CHAIN:
        return len(WAIT_CHAIN) - WAIT_CHAIN.index(r) + 1
    return 1


def signal_reports(result):
    return 0 if result & 0xffffffff in (0, EINVAL) else 1


def case(kind, cid, result=0, created=True, timeout=-1, count=1):
    name = 'sceKernelWaitSema' if kind == 'wait' else 'sceKernelSignalSema'
    argc = 3 if kind == 'wait' else 2
    reports = (wait_reports if kind == 'wait' else signal_reports)(result) + (0 if created else 1)
    second = timeout if kind == 'wait' else count
    return {'schema': 1, 'address': WAIT if kind == 'wait' else SIGNAL, 'id': cid, 'returns': 'i32',
            'args': {'rdi': 'buf:sema', 'rsi': second & 0xffffffff},
            'buffers': {'sema': {'size': 0x18, 'bytes': '5e' * 0x18}},
            'memory': [{'addr': 'buf:sema+8', 'bytes': u64(HANDLE)},
                       {'addr': 'buf:sema+16', 'bytes': '01' if created else '00'}],
            'stubs': [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports)],
            'imports': [{'name': name, 'argc': argc, 'ret': hex(result)}]}


RESULTS = [('ok', 0), ('positive', 7), ('esrch', ESRCH), ('ecanceled', ECANCELED), ('efault', EFAULT),
           ('eacces', EACCES), ('einval', EINVAL), ('etimedout', ETIMEDOUT), ('other_negative', 0x80020001),
           ('between_codes', 0x80020040), ('lowest_negative', 0x80000000), ('minus_one', 0xffffffff),
           ('ok_upper_bits', 0x1234567800000000), ('esrch_upper_bits', 0xff00000000000000 | ESRCH)]


def make(kind):
    out = [case(kind, name, result=r) for name, r in RESULTS]
    out.append(case(kind, 'not_created', created=False))
    out.append(case(kind, 'not_created_einval', created=False, result=EINVAL))
    if kind == 'wait':
        out += [case(kind, 'timeout_zero', timeout=0), case(kind, 'timeout_given', timeout=16666),
                case(kind, 'timeout_minus_two', timeout=-2), case(kind, 'timeout_timed_out', timeout=100, result=ETIMEDOUT)]
    else:
        out += [case(kind, 'count_zero', count=0), case(kind, 'count_many', count=0x7fffffff),
                case(kind, 'count_negative', count=-1)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ('wait', 'signal'):
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
