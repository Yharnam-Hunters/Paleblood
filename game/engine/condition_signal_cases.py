#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's condition signal (game/engine/condition_signal.cpp), for
tools/verify.py run.

usage: condition_signal_cases.py OUT_DIR set|wait|create|construct|delete|destroy_base|finalize|poll|check

Each case builds a condition signal (`signal`: a vtable word, a mutex handle, a condition and the
flags under test). The mutex lock and unlock and pthread_cond_broadcast are scripted imports with
the results under test; the condition wait (0x02483e80) and the error reports are stubs, as many
reports as the case makes (worked out here). For create, the vtable is a fake whose creation
query is a stub, the mutex creation (0x02483fe0) is a stub and pthread_cond_init a scripted import.
The teardown cases script scePthreadMutexDestroy and pthread_cond_destroy (and operator delete).
"""
import json
import os
import struct
import sys

FATAL_ERROR, CONDITION_WAIT = '0x024b55b0', '0x02483e80'
SET, WAIT, CREATE = '0x0247e780', '0x0247e8f0', '0x0247e690'
IS_CREATED, MUTEX_CREATE = '0x02484110', '0x02483fe0'
LIFECYCLE = {'construct': '0x0247e400', 'delete': '0x0247e420', 'destroy_base': '0x0247e4f0',
             'finalize': '0x0247e5c0', 'poll': '0x0247ea40', 'check': '0x0247ea80'}
EBUSY = 0x80020010   # the query: any game function, stubbed
EPERM, EDEADLK, EINVAL, EAGAIN, ETIMEDOUT = 0x80020001, 0x8002000b, 0x80020016, 0x80020023, 0x8002003c
CREATED, SIGNALLED, WAITED = 1, 2, 4


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def lock_reports(result):
    r = result & 0xffffffff
    if r in (0, ETIMEDOUT):
        return 0
    return {EDEADLK: 4, EINVAL: 3, EAGAIN: 2}.get(r, 1)


def unlock_reports(result):
    return 0 if result & 0xffffffff in (0, EPERM) else 1


def case(kind, cid, flags=CREATED, argument=1, lock=0, unlock=0, broadcast=0, waited_result=0):
    reports = (0 if flags & CREATED else 1) + lock_reports(lock) + unlock_reports(unlock)
    imports = [{'name': 'scePthreadMutexLock', 'argc': 1, 'ret': hex(lock)}]
    stubs = []
    if kind == 'set':
        if flags & WAITED and argument & 0xff == 1:
            imports.append({'name': 'pthread_cond_broadcast', 'argc': 1, 'ret': hex(broadcast)})
            reports += 1 if broadcast & 0xffffffff == 22 else 0
    elif not flags & SIGNALLED:
        stubs.append({'address': CONDITION_WAIT, 'argc': 3, 'ret': waited_result})
    imports.append({'name': 'scePthreadMutexUnlock', 'argc': 1, 'ret': hex(unlock)})
    stubs += [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports)]
    return {'schema': 1, 'address': SET if kind == 'set' else WAIT, 'id': cid,
            'returns': 'i32', 'args': {'rdi': 'buf:signal', 'rsi': argument},
            'buffers': {'signal': {'size': 0x20, 'bytes': '3c' * 0x20}},
            'memory': [{'addr': 'buf:signal+8', 'bytes': u64(0x1111222233334444)},
                       {'addr': 'buf:signal+16', 'bytes': u64(0x5555666677778888)},
                       {'addr': 'buf:signal+24', 'bytes': struct.pack('<I', flags).hex()}],
            'stubs': stubs, 'imports': imports}


def create_case(cid, flags=0, argument=0, created=False, mutex=0, init=0):
    stubs = [{'address': IS_CREATED, 'argc': 1, 'ret': int(created)}]
    imports = []
    if not created:
        stubs.append({'address': MUTEX_CREATE, 'argc': 1, 'ret': hex(mutex)})
        if mutex & 0xffffffff == 0:
            imports.append({'name': 'pthread_cond_init', 'argc': 2, 'ret': hex(init)})
            e = init & 0xffffffff
            reports = 0 if e in (0, 12, 35) else {22: 3, 16: 2}.get(e, 1)
            stubs += [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports)]
    return {'schema': 1, 'address': CREATE, 'id': cid, 'returns': 'i32',
            'args': {'rdi': 'buf:signal', 'rsi': argument},
            'buffers': {'signal': {'size': 0x20, 'bytes': '3c' * 0x20}, 'vtable': {'size': 0x18}},
            'memory': [{'addr': 'buf:signal+0', 'pointer': 'vtable'},
                       {'addr': 'buf:vtable+16', 'guest': IS_CREATED},
                       {'addr': 'buf:signal+8', 'bytes': u64(0x1111222233334444)},
                       {'addr': 'buf:signal+16', 'bytes': u64(0x5555666677778888)},
                       {'addr': 'buf:signal+24', 'bytes': struct.pack('<I', flags).hex()}],
            'stubs': stubs, 'imports': imports}


def lifecycle_case(kind, cid, flags=CREATED, created=True, mutex=0, cond=0):
    stubs, imports = [], []
    reports = 0
    if kind in ('poll', 'check'):
        reports = 0 if flags & CREATED else 1
    elif kind != 'construct':
        if kind == 'finalize':
            stubs.append({'address': IS_CREATED, 'argc': 1, 'ret': int(created)})
            runs = created
        else:
            runs = flags & CREATED
        if runs:
            imports.append({'name': 'scePthreadMutexDestroy', 'argc': 1, 'ret': hex(mutex)})
            m = mutex & 0xffffffff
            if m != EBUSY:
                reports += 0 if m == 0 else 2 if m == EINVAL else 1
                imports.append({'name': 'pthread_cond_destroy', 'argc': 1, 'ret': hex(cond)})
                c = cond & 0xffffffff
                reports += 0 if c in (0, 16) else 2 if c == 22 else 1
        if kind == 'delete':
            imports.append({'name': '_ZdlPv', 'argc': 1})
    stubs += [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports)]
    return {'schema': 1, 'address': LIFECYCLE[kind], 'id': cid,
            'returns': 'void' if kind in ('construct', 'delete', 'destroy_base') else 'i32',
            'args': {'rdi': 'buf:signal'},
            'buffers': {'signal': {'size': 0x20, 'bytes': '3c' * 0x20}, 'vtable': {'size': 0x18}},
            'memory': [{'addr': 'buf:signal+0', 'pointer': 'vtable'},
                       {'addr': 'buf:vtable+16', 'guest': IS_CREATED},
                       {'addr': 'buf:signal+8', 'bytes': u64(0x1111222233334444)},
                       {'addr': 'buf:signal+16', 'bytes': u64(0x5555666677778888)},
                       {'addr': 'buf:signal+24', 'bytes': struct.pack('<I', flags).hex()}],
            'stubs': stubs, 'imports': imports}


def lifecycle_cases(kind):
    c = lambda cid, **kw: lifecycle_case(kind, cid, **kw)  # noqa: E731
    if kind == 'construct':
        return [c('constructs'), c('over_flags', flags=0xffffffff)]
    if kind in ('poll', 'check'):
        return [c('created'), c('signalled', flags=CREATED | SIGNALLED), c('all_flags', flags=0xffffffff),
                c('not_created', flags=0), c('not_created_signalled', flags=SIGNALLED | WAITED),
                c('waited_only', flags=CREATED | WAITED)]
    out = [c('both_destroyed'), c('mutex_busy', mutex=EBUSY), c('mutex_einval', mutex=EINVAL),
           c('mutex_other', mutex=0x80020002), c('mutex_positive', mutex=3), c('mutex_upper_bits', mutex=0xab00000000000000),
           c('cond_busy', cond=16), c('cond_einval', cond=22), c('cond_other', cond=1), c('cond_sce_ebusy', cond=EBUSY),
           c('cond_upper_bits', cond=0x1000000016), c('both_einval', mutex=EINVAL, cond=22),
           c('other_flags_kept', flags=0xffffffff), c('other_flags_busy', flags=0xffffffff, cond=16)]
    if kind == 'finalize':
        out += [c('not_created_per_vtable', created=False), c('vtable_created_flag_clear', flags=0),
                c('vtable_not_created_flag_set', created=False, flags=0xffffffff)]
    else:
        out += [c('flag_clear', flags=0), c('flag_clear_others_set', flags=0xfffffffe)]
    return out


def create_cases():
    c = create_case
    return [
        c('creates'), c('creates_signalled', argument=1), c('argument_two', argument=2),
        c('argument_0xff', argument=0xff), c('argument_upper_bits', argument=0x103),
        c('flags_cleared_and_kept', flags=0xffffffff), c('waited_cleared', flags=WAITED | SIGNALLED),
        c('already_created', created=True, flags=WAITED, argument=1),
        c('mutex_fails', mutex=0xffffffff), c('mutex_positive', mutex=5), c('mutex_upper_bits', mutex=0x700000000),
        c('init_enomem', init=12), c('init_eagain', init=35), c('init_einval', init=22), c('init_ebusy', init=16),
        c('init_other', init=1), c('init_negative', init=0xffffffff), c('init_between', init=13),
        c('init_above_eagain', init=36), c('init_upper_bits', init=0x1200000000), c('init_eperm', init=11),
    ]


def set_cases():
    c = lambda cid, **kw: case('set', cid, **kw)  # noqa: E731
    return [
        c('set_no_waiter', flags=CREATED, argument=1),
        c('set_with_waiter', flags=CREATED | WAITED, argument=1),
        c('set_already_set_with_waiter', flags=CREATED | SIGNALLED | WAITED, argument=1),
        c('clear_with_waiter', flags=CREATED | SIGNALLED | WAITED, argument=0),
        c('clear', flags=CREATED | SIGNALLED, argument=0),
        c('argument_two', flags=CREATED | WAITED, argument=2),
        c('argument_three', flags=CREATED, argument=3),
        c('argument_upper_bits', flags=CREATED | WAITED, argument=0x101),
        c('argument_0xff', flags=CREATED | WAITED, argument=0xff),
        c('high_flags_kept', flags=0xfffffff9, argument=0),
        c('broadcast_einval', flags=CREATED | WAITED, broadcast=22),
        c('broadcast_other_error', flags=CREATED | WAITED, broadcast=1),
        c('broadcast_sce_einval', flags=CREATED | WAITED, broadcast=EINVAL),
        c('not_created', flags=WAITED),
        c('lock_edeadlk', lock=EDEADLK), c('lock_einval', lock=EINVAL), c('lock_eagain', lock=EAGAIN),
        c('lock_etimedout', lock=ETIMEDOUT, flags=CREATED | WAITED), c('lock_other', lock=0x80020002),
        c('lock_positive', lock=3), c('unlock_eperm', unlock=EPERM), c('unlock_einval', unlock=EINVAL),
        c('unlock_other', unlock=0x80020002), c('unlock_upper_bits_ok', unlock=0xabc0000000000000),
    ]


def wait_cases():
    c = lambda cid, **kw: case('wait', cid, **kw)  # noqa: E731
    return [
        c('waits', flags=CREATED, argument=0xffffffff),
        c('waits_timeout', flags=CREATED, argument=16666, waited_result=0xfffffffd),
        c('already_signalled', flags=CREATED | SIGNALLED, argument=0xffffffff),
        c('waited_flag_already_set', flags=CREATED | WAITED, argument=5, waited_result=7),
        c('high_flags_kept', flags=0xfffffff9, argument=0),
        c('not_created', flags=0),
        c('not_created_signalled', flags=SIGNALLED),
        c('lock_edeadlk', lock=EDEADLK), c('lock_einval', lock=EINVAL), c('lock_etimedout', lock=ETIMEDOUT),
        c('lock_other', lock=0x80020002), c('unlock_eperm', unlock=EPERM), c('unlock_einval', unlock=EINVAL),
        c('unlock_other', unlock=5),
    ]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ('set', 'wait', 'create', *LIFECYCLE):
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    kind = sys.argv[2]
    made = lifecycle_cases(kind) if kind in LIFECYCLE else {'set': set_cases, 'wait': wait_cases,
                                                               'create': create_cases}[kind]()
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
