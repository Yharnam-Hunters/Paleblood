#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's debug print (game/engine/debug_print.cpp), for tools/verify.py run.

usage: debug_print_cases.py OUT_DIR print|print_list

The print lock is either already there (the instance pointer set to a buffer whose vtable is the
print lock's real one) or built in its static storage. Its lock and unlock (0x02a3a970,
0x02a3aa00) are stubbed, as is the system mutex creation (0x02483fe0); the C++ static guard's
acquire and release and vprintf are scripted imports. vprintf's format is compared, not its
va_list, whose address differs between the two runs. Cases cover printing enabled or not, forced
or not, an "enabled" byte other than 1; building the lock with the light mutex already there or
not, with its creation failing, with the guards' first byte clear (acquired or not), and an
instance that appears while the light mutex is being built (the second check finds it).
"""
import json
import os
import sys

PRINT, PRINT_LIST = '0x024df920', '0x024dfb30'
LOCK, UNLOCK, SYSTEM_MUTEX_CREATE = '0x02a3a970', '0x02a3aa00', '0x02483fe0'
PRINT_LOCK_VTABLE = '0x057b3c20'
INSTANCE, STORAGE, STORAGE_GUARD = '0x05aa5488', '0x05aa5490', '0x05aa54e8'
LIGHT_MUTEX, LIGHT_CONSTRUCTED, LIGHT_GUARD = '0x05aa5720', '0x05aa573f', '0x05aa5740'
PRINT_LOCK_SIZE, LIGHT_MUTEX_SIZE = 0x50, 0x18
ENABLED = 0x48
LIGHT_HANDLE = 0x8
INSTANCE_FROM_LIGHT_HANDLE = int(INSTANCE, 16) - (int(LIGHT_MUTEX, 16) + LIGHT_HANDLE)


def lock_stubs():
    return [{'address': LOCK, 'argc': 2, 'ret': 0}, {'address': UNLOCK, 'argc': 1, 'ret': 0}]


def case(variant, cid, force, existing=None, constructed=1, light_guard=1, storage_guard=1,
         create_result=0, light_acquire=None, storage_acquire=None, appears=False):
    """`existing`: the lock buffer's enabled byte (None: no lock yet). The guards' first byte, and
    what acquire returns when it is clear (None: not called)."""
    buffers = {'format': {'size': 16, 'bytes': b'%s %d\n'.hex()},
               'text': {'size': 8, 'bytes': b'abc'.hex()},
               'arguments': {'size': 24, 'bytes': '10000000300000000000000000000000' + '00' * 8}}
    memory, stubs, imports = [], [], []
    storage_bytes = 'a5' * PRINT_LOCK_SIZE
    memory.append({'addr': STORAGE, 'bytes': storage_bytes})
    memory.append({'addr': LIGHT_MUTEX, 'bytes': 'a5' * LIGHT_MUTEX_SIZE})
    memory.append({'addr': LIGHT_CONSTRUCTED, 'bytes': f'{constructed:02x}'})
    memory.append({'addr': LIGHT_GUARD, 'bytes': f'{light_guard:02x}' + '00' * 7})
    memory.append({'addr': STORAGE_GUARD, 'bytes': f'{storage_guard:02x}' + '00' * 7})
    if existing is not None:
        lock = bytearray(b'\x00' * PRINT_LOCK_SIZE)
        lock[ENABLED] = existing
        buffers['lock'] = {'size': PRINT_LOCK_SIZE, 'bytes': bytes(lock).hex()}
        memory.append({'addr': 'buf:lock+0x0', 'guest': PRINT_LOCK_VTABLE})
        memory.append({'addr': INSTANCE, 'pointer': 'lock'})
    else:
        memory.append({'addr': INSTANCE, 'bytes': '00' * 8})
        if light_acquire is not None:
            imports.append({'name': '__cxa_guard_acquire', 'argc': 1, 'ret': light_acquire})
            if light_acquire:
                imports.append({'name': '__cxa_guard_release', 'argc': 1})
        if not constructed or light_acquire:
            # An acquired guard clears the "constructed" byte, so the light mutex is built.
            entry = {'address': SYSTEM_MUTEX_CREATE, 'argc': 0, 'ret': create_result}
            if appears:
                # The creation (given the light mutex's handle) sets the instance meanwhile.
                buffers['lock'] = {'size': PRINT_LOCK_SIZE, 'bytes': '00' * ENABLED + '01'}
                memory.append({'addr': 'buf:lock+0x0', 'guest': PRINT_LOCK_VTABLE})
                entry['writes'] = [{'arg': 0, 'offset': INSTANCE_FROM_LIGHT_HANDLE, 'pointer': 'lock'}]
            stubs.append(entry)
        if not appears:
            if storage_acquire is not None:
                imports.append({'name': '__cxa_guard_acquire', 'argc': 1, 'ret': storage_acquire})
                if storage_acquire:
                    imports.append({'name': '__cxa_guard_release', 'argc': 1})
            # The mutex's initialize creates the system mutex unless it is already created.
            stubs.append({'address': SYSTEM_MUTEX_CREATE, 'argc': 0, 'ret': create_result})
    stubs += lock_stubs()
    printed = force or (existing if existing is not None else 1)
    if printed:
        imports.append({'name': 'vprintf', 'argc': 1, 'ret': 6})
    if variant == 'print':
        args = {'rdi': force, 'rsi': 'buf:format', 'rdx': 'buf:text', 'rcx': 7}
        address = PRINT
    else:
        args = {'rdi': force, 'rsi': 'buf:format', 'rdx': 'buf:arguments'}
        address = PRINT_LIST
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': args,
            'buffers': buffers, 'memory': memory, 'imports': imports, 'stubs': stubs}


def cases(variant):
    c = lambda cid, *a, **k: case(variant, cid, *a, **k)
    return [
        c('enabled', 0, existing=1),
        c('disabled', 0, existing=0),
        c('disabled_forced', 1, existing=0),
        c('enabled_forced', 1, existing=1),
        c('enabled_byte_other_than_one', 0, existing=0x80),
        c('build_lock', 0),
        c('build_lock_forced', 1),
        c('build_lock_and_light_mutex', 0, constructed=0),
        c('light_mutex_creation_fails', 0, constructed=0, create_result=-1),
        c('build_lock_creation_fails', 0, create_result=0x80020023),
        c('guards_acquired', 0, constructed=1, light_guard=0, storage_guard=0, light_acquire=1, storage_acquire=1),
        c('guards_not_acquired', 0, constructed=1, light_guard=0, storage_guard=0, light_acquire=0, storage_acquire=0),
        c('light_guard_acquired_clears_constructed', 0, constructed=1, light_guard=0, light_acquire=1),
        c('instance_appears_meanwhile', 0, constructed=0, appears=True),
    ]


VARIANTS = {'print': lambda: cases('print'), 'print_list': lambda: cases('print_list')}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in VARIANTS:
        print(__doc__, file=sys.stderr)
        return 2
    out = VARIANTS[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
