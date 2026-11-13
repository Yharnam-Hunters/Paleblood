#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's PthreadMutex.cpp (game/kernel/pthread_mutex.cpp), for tools/verify.py run.

usage: pthread_mutex_cases.py OUT_DIR initialize|destroy|lock|try_lock|unlock

Every system result the original tells apart, the codes on either side of its range checks, a
positive result and results with high bits above the 32 the original reads. The system calls and
the fatal error (0x024b55b0) are stubbed. The fatal error never returns in the game; the stubs
return, so the reports that follow in the original's chain run too.
"""
import json
import os
import sys

FATAL = '0x024b55b0'
ATTR_INIT, ATTR_SETTYPE, MUTEX_INIT, ATTR_DESTROY = '0x02fbfea8', '0x02fbfeb8', '0x02fbfec8', '0x02fbfed8'
DESTROY, LOCK, UNLOCK, TRYLOCK, TIMEDLOCK = '0x02fbfee8', '0x02fbfef8', '0x02fbff08', '0x02fc0cb8', '0x02fc0d88'
ADDRESSES = {'initialize': '0x02483fe0', 'destroy': '0x024840b0', 'lock': '0x02484110',
             'try_lock': '0x024841d0', 'unlock': '0x02484290'}

MASK = (1 << 64) - 1
EPERM, EDEADLK, ENOMEM, EBUSY = 0x80020001, 0x8002000b, 0x8002000c, 0x80020010
EINVAL, EAGAIN, ETIMEDOUT, EIO = 0x80020016, 0x80020023, 0x8002003c, 0x80020005
HIGH = 0xdead_beef_0000_0000  # garbage above the 32-bit result
MUTEX_HANDLE = 0x1122334455667788
ATTRIBUTE = 0x0f1e2d3c4b5a6978


def word(value):
    return (value & MASK).to_bytes(8, 'little').hex()


def reports(count):
    return [{'address': FATAL, 'argc': 3, 'ret': '0x0'} for _ in range(count)]


def base(function, cid, args):
    return {'schema': 1, 'address': ADDRESSES[function], 'id': cid, 'returns': 'i32', 'args': args,
            'buffers': {'mutex': {'size': 24, 'bytes': 'a5' * 8 + word(MUTEX_HANDLE) + '5a' * 8}},
            'memory': [], 'imports': [], 'stubs': []}


def call(address, ret, argc=1, mutex=True):
    stub = {'address': address, 'argc': argc, 'ret': hex(ret & MASK)}
    if mutex:
        stub['argmem'] = [{'arg': 0, 'size': 8}]
    return stub


def simple(function, cid, ret, fatal=0, address=None, timeout=None):
    args = {'rdi': 'buf:mutex+0x8'}
    if timeout is not None:
        args['rsi'] = hex(timeout & 0xffffffff)
    c = base(function, cid, args)
    c['stubs'] = [call(address, ret, 2 if address == TIMEDLOCK else 1)] + reports(fatal)
    return c


def initialize(cid, ret=0, fatal=0, stage_rets=(0, 0, 0)):
    c = base('initialize', cid, {'rdi': 'buf:mutex+0x8'})
    attr_init, settype, attr_destroy = stage_rets
    stubs = [
        {'address': ATTR_INIT, 'argc': 1, 'ret': hex(attr_init & MASK)},
        {'address': ATTR_SETTYPE, 'argc': 2, 'ret': hex(settype & MASK), 'argmem': [{'arg': 0, 'size': 8}]},
        {'address': MUTEX_INIT, 'argc': 3, 'ret': hex(ret & MASK),
         'argmem': [{'arg': 0, 'size': 8}, {'arg': 1, 'size': 8}], 'writes': [{'arg': 0, 'bytes': word(MUTEX_HANDLE ^ 1)}]},
        {'address': ATTR_DESTROY, 'argc': 1, 'ret': hex(attr_destroy & MASK), 'argmem': [{'arg': 0, 'size': 8}],
         'writes': [{'arg': 0, 'bytes': word(0)}]},
    ]
    # The attribute lives on the stack: the system writes it before it is read.
    stubs[0]['writes'] = [{'arg': 0, 'bytes': word(ATTRIBUTE)}]
    stubs[1]['writes'] = [{'arg': 0, 'bytes': word(ATTRIBUTE ^ 0xff)}]
    c['stubs'] = stubs + reports(fatal)
    return c


def initialize_cases():
    return [
        initialize('ok'),
        initialize('ok_high_bits', ret=HIGH),
        initialize('enomem', ret=ENOMEM),
        initialize('eagain', ret=EAGAIN),
        initialize('einval', ret=EINVAL, fatal=2),
        initialize('ebusy_unknown', ret=EBUSY, fatal=1),
        initialize('eio_unknown', ret=EIO, fatal=1),
        initialize('positive_unknown', ret=1, fatal=1),
        initialize('enomem_high_bits', ret=HIGH | ENOMEM),
        initialize('attribute_errors_ignored', stage_rets=(EINVAL, EINVAL, EINVAL)),
    ]


def destroy_cases():
    d = lambda cid, ret, fatal=0: simple('destroy', cid, ret, fatal, DESTROY)
    return [d('ok', 0), d('ok_high_bits', HIGH), d('ebusy', EBUSY), d('einval', EINVAL, 2),
            d('eperm_unknown', EPERM, 1), d('edeadlk_unknown', EDEADLK, 1), d('positive_unknown', 1, 1),
            d('ebusy_high_bits', HIGH | EBUSY), d('just_above_ebusy', EBUSY + 1, 1)]


def lock_cases():
    cases = []
    for name, timeout, address in [('untimed', -1, LOCK), ('timed', 16_666, TIMEDLOCK)]:
        l = lambda cid, ret, fatal=0: simple('lock', f'{name}_{cid}', ret, fatal, address, timeout)
        cases += [l('ok', 0), l('ok_high_bits', HIGH), l('etimedout', ETIMEDOUT), l('edeadlk', EDEADLK, 4),
                  l('einval', EINVAL, 3), l('eagain', EAGAIN, 2), l('ebusy', EBUSY, 1), l('eperm', EPERM, 1),
                  l('positive', 1, 1), l('below_etimedout', ETIMEDOUT - 1, 1), l('above_etimedout', ETIMEDOUT + 1, 1),
                  l('etimedout_high_bits', HIGH | ETIMEDOUT)]
    for timeout in (0, -2, 0x7fffffff, -0x80000000):
        cases.append(simple('lock', f'timeout_{timeout & 0xffffffff:#x}', 0, 0, TIMEDLOCK, timeout))
    return cases


def try_lock_cases():
    t = lambda cid, ret, fatal=0: simple('try_lock', cid, ret, fatal, TRYLOCK)
    return [t('ok', 0), t('ok_high_bits', HIGH), t('ebusy', EBUSY), t('edeadlk', EDEADLK, 4), t('einval', EINVAL, 3),
            t('eagain', EAGAIN, 2), t('etimedout', ETIMEDOUT, 1), t('eperm', EPERM, 1), t('positive', 1, 1),
            t('below_eagain', EAGAIN - 1, 1), t('above_eagain', EAGAIN + 1, 1), t('ebusy_high_bits', HIGH | EBUSY)]


def unlock_cases():
    u = lambda cid, ret, fatal=0: simple('unlock', cid, ret, fatal, UNLOCK)
    return [u('ok', 0), u('ok_high_bits', HIGH), u('eperm', EPERM), u('einval', EINVAL, 1), u('ebusy', EBUSY, 1),
            u('positive', 1, 1), u('eperm_high_bits', HIGH | EPERM), u('above_eperm', EPERM + 1, 1)]


VARIANTS = {'initialize': initialize_cases, 'destroy': destroy_cases, 'lock': lock_cases,
            'try_lock': try_lock_cases, 'unlock': unlock_cases}


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
