#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's condition signal (game/engine/condition_signal.cpp), for
tools/verify.py run.

usage: condition_signal_cases.py OUT_DIR set|wait

Each case builds a condition signal (`signal`: a vtable word, a mutex handle, a condition and the
flags under test). The mutex lock and unlock and pthread_cond_broadcast are scripted imports with
the results under test; the condition wait (0x02483e80) and the error reports are stubs, as many
reports as the case makes (worked out here).
"""
import json
import os
import struct
import sys

FATAL_ERROR, CONDITION_WAIT = '0x024b55b0', '0x02483e80'
SET, WAIT = '0x0247e780', '0x0247e8f0'
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
    if len(sys.argv) != 3 or sys.argv[2] not in ('set', 'wait'):
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    made = set_cases() if sys.argv[2] == 'set' else wait_cases()
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
