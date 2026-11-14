#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's read-write lock (game/kernel/read_write_lock.cpp), for tools/verify.py run.

usage: read_write_lock_cases.py OUT_DIR construct|delete|destruct|finalize|initialize|write|
    try_write|read|unlock_0247f540|unlock_0247f5c0|unlock_0247f710

The lock object is a buffer: its vtable (a buffer whose third slot names a stub), the system
lock's handle and the initialized flag, with garbage around them. Members that ask whether the
lock is initialized go through that slot; the stub answers, sometimes disagreeing with
the flag, so the virtual call is what counts. The system calls, the C++ delete and the fatal error
(0x024b55b0) are stubbed too. The fatal error never returns in the game; the stubs return, so the
reports that follow in the original's chain run as well.
"""
import json
import os
import sys

FATAL, DELETE = '0x024b55b0', '0x02fbe588'
IS_INITIALIZED = '0x00500200'  # a stub in the vtable's third slot
IS_INITIALIZED_SLOT = 0x10
INIT, DESTROY, RDLOCK, TIMEDRDLOCK = '0x02fc0cd8', '0x02fc0cc8', '0x02fc0d28', '0x02fc0d18'
WRLOCK, TIMEDWRLOCK, TRYWRLOCK, UNLOCK = '0x02fc0cf8', '0x02fc0ce8', '0x02fc0d08', '0x02fc0ca8'
ADDRESSES = {'construct': '0x0247f180', 'delete': '0x0247f190', 'destruct': '0x0247f1f0',
             'finalize': '0x0247f250', 'initialize': '0x0247f2b0',
             'write': '0x0247f3d0', 'try_write': '0x0247f4a0', 'read': '0x0247f640',
             'unlock_0247f540': '0x0247f540', 'unlock_0247f5c0': '0x0247f5c0', 'unlock_0247f710': '0x0247f710'}
RETURNS = {'construct': 'void', 'delete': 'void', 'destruct': 'void'}

MASK = (1 << 64) - 1
EPERM, EDEADLK, ENOMEM, EFAULT, EBUSY = 0x80020001, 0x8002000b, 0x8002000c, 0x8002000e, 0x80020010
EINVAL, EAGAIN, ETIMEDOUT, EIO = 0x80020016, 0x80020023, 0x8002003c, 0x80020005
HIGH = 0xdead_beef_0000_0000  # garbage above the 32-bit result
HANDLE = 0x1122334455667788


def word(value):
    return (value & MASK).to_bytes(8, 'little').hex()


def reports(count):
    return [{'address': FATAL, 'argc': 3, 'ret': '0x0'} for _ in range(count)]


def case(variant, cid, flag=0, virtual=None, stubs=(), args=None):
    """virtual: what the vtable's is-initialized slot answers (None: not called)."""
    flag_byte = f'{flag:02x}'
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS.get(variant, 'i32'),
         'args': {'rdi': 'buf:lock+0x8', **(args or {})},
         'buffers': {
             'lock': {'size': 0x28, 'bytes': 'a5' * 8 + '00' * 8 + word(HANDLE) + flag_byte + '5a' * 15},
             'vtable': {'size': 0x20, 'bytes': 'c3' * 0x20},
         },
         'memory': [{'addr': 'buf:lock+0x8', 'pointer': 'vtable'},
                    {'addr': f'buf:vtable+{IS_INITIALIZED_SLOT:#x}', 'guest': IS_INITIALIZED}], 'imports': [], 'stubs': []}
    if virtual is not None:
        c['stubs'].append({'address': IS_INITIALIZED, 'argc': 1, 'ret': hex(virtual)})
    c['stubs'] += list(stubs)
    return c


def call(address, ret, argc=1):
    return {'address': address, 'argc': argc, 'ret': hex(ret & MASK), 'argmem': [{'arg': 0, 'size': 8}]}


def construct_cases():
    return [case('construct', 'clear'), case('construct', 'was_set', flag=1), case('construct', 'nonboolean', flag=0x80)]


def delete_cases():
    d = lambda cid, flag, ret=0, fatal=0: case('delete', cid, flag,
                                                stubs=([call(DESTROY, ret)] if flag else []) + reports(fatal)
                                                + [{'address': DELETE, 'argc': 1, 'ret': '0x0'}])
    return [d('not_initialized', 0), d('ok', 1), d('ebusy', 1, EBUSY), d('einval', 1, EINVAL, 1), d('other', 1, EIO),
            d('einval_high_bits', 1, HIGH | EINVAL, 1), d('positive', 1, 1)]


def destruct_cases():
    d = lambda cid, flag, ret=0, fatal=0: case('destruct', cid, flag,
                                                stubs=([call(DESTROY, ret)] if flag else []) + reports(fatal))
    return [d('not_initialized', 0), d('ok', 1), d('ebusy', 1, EBUSY), d('einval', 1, EINVAL, 1), d('other', 1, EIO),
            d('ebusy_high_bits', 1, HIGH | EBUSY), d('positive', 1, 1), d('nonboolean_flag', 2)]


def finalize_cases():
    f = lambda cid, virtual, ret=0, fatal=0, flag=1: case('finalize', cid, flag, virtual,
                                                          ([call(DESTROY, ret)] if virtual & 0xff else []) + reports(fatal))
    return [f('not_initialized', 0, flag=0), f('flag_set_but_virtual_says_no', 0), f('ok', 1),
            f('virtual_says_yes_flag_clear', 1, flag=0), f('ebusy', 1, EBUSY), f('einval', 1, EINVAL, 1),
            f('other', 1, EIO), f('ebusy_high_bits', 1, HIGH | EBUSY), f('virtual_upper_bits', 0x100, 0)]


def initialize_cases():
    def i(cid, ret=0, fatal=0, virtual=0, flag=0):
        stubs = [] if virtual & 0xff else [{'address': INIT, 'argc': 3, 'ret': hex(ret & MASK),
                                            'writes': [{'arg': 0, 'bytes': word(HANDLE ^ 0xff)}]}]
        return case('initialize', cid, flag, virtual, stubs + reports(fatal))
    return [i('already', virtual=1, flag=1), i('already_flag_clear', virtual=1, flag=0), i('ok'), i('ok_high_bits', HIGH),
            i('enomem', ENOMEM), i('eagain', EAGAIN), i('eperm', EPERM, 4), i('ebusy', EBUSY, 3), i('einval', EINVAL, 2),
            i('efault', EFAULT, 1), i('edeadlk', EDEADLK), i('eio', EIO), i('positive', 1), i('etimedout', ETIMEDOUT),
            i('above_eagain', EAGAIN + 1), i('below_einval', EINVAL - 1), i('enomem_high_bits', HIGH | ENOMEM),
            i('virtual_upper_bits', virtual=0x100), i('flag_set_virtual_no', flag=1)]


def lock_cases(variant, timed, untimed, chain):
    out = []
    for name, timeout, address in [('untimed', -1, untimed), ('timed', 16_666, timed)]:
        def l(cid, ret, fatal=0, virtual=1):
            return case(variant, f'{name}_{cid}', 1, virtual,
                        reports(0 if virtual & 0xff else 1) + [call(address, ret, 2 if address == timed else 1)] + reports(fatal),
                        {'rsi': hex(timeout & 0xffffffff)})
        out += [l('ok', 0), l('ok_high_bits', HIGH), l('etimedout', ETIMEDOUT), l('etimedout_high_bits', HIGH | ETIMEDOUT),
                l('not_created', 0, virtual=0)]
        out += [l(cid, ret, chain[cid]) for cid, ret in [('einval', EINVAL), ('edeadlk', EDEADLK), ('eagain', EAGAIN),
                                                          ('ebusy', EBUSY), ('eperm', EPERM), ('positive', 1),
                                                          ('below_etimedout', ETIMEDOUT - 1),
                                                          ('above_etimedout', ETIMEDOUT + 1)]]
    for timeout in (0, -2, 0x7fffffff, -0x80000000):
        out.append(case(variant, f'timeout_{timeout & 0xffffffff:#x}', 1, 1, [call(timed, 0, 2)],
                        {'rsi': hex(timeout & 0xffffffff)}))
    return out


WRITE_CHAIN = {'einval': 3, 'edeadlk': 2, 'eagain': 1, 'ebusy': 1, 'eperm': 1, 'positive': 1, 'below_etimedout': 1,
               'above_etimedout': 1}
READ_CHAIN = {'einval': 3, 'edeadlk': 1, 'eagain': 2, 'ebusy': 0, 'eperm': 0, 'positive': 0, 'below_etimedout': 0,
              'above_etimedout': 0}


def try_write_cases():
    def t(cid, ret, fatal=0, virtual=1):
        return case('try_write', cid, 1, virtual, reports(0 if virtual & 0xff else 1) + [call(TRYWRLOCK, ret)] + reports(fatal))
    return [t('ok', 0), t('ok_high_bits', HIGH), t('ebusy', EBUSY), t('ebusy_high_bits', HIGH | EBUSY), t('einval', EINVAL, 2),
            t('edeadlk', EDEADLK, 1), t('eagain', EAGAIN), t('eperm', EPERM), t('positive', 1), t('not_created', 0, virtual=0),
            t('virtual_upper_bits', 0, virtual=0x100)]


def unlock_cases(variant):
    def u(cid, ret, fatal=0, virtual=1):
        return case(variant, cid, 1, virtual, reports(0 if virtual & 0xff else 1) + [call(UNLOCK, ret)] + reports(fatal))
    return [u('ok', 0), u('ok_high_bits', HIGH), u('eperm', EPERM), u('eperm_high_bits', HIGH | EPERM), u('einval', EINVAL, 1),
            u('ebusy', EBUSY), u('positive', 1), u('not_created', 0, virtual=0), u('not_created_eperm', EPERM, virtual=0),
            u('virtual_upper_bits', 0, virtual=0x100)]


VARIANTS = {'construct': construct_cases, 'delete': delete_cases, 'destruct': destruct_cases, 'finalize': finalize_cases,
            'initialize': initialize_cases,
            'write': lambda: lock_cases('write', TIMEDWRLOCK, WRLOCK, WRITE_CHAIN), 'try_write': try_write_cases,
            'read': lambda: lock_cases('read', TIMEDRDLOCK, RDLOCK, READ_CHAIN),
            'unlock_0247f540': lambda: unlock_cases('unlock_0247f540'),
            'unlock_0247f5c0': lambda: unlock_cases('unlock_0247f5c0'),
            'unlock_0247f710': lambda: unlock_cases('unlock_0247f710')}


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
