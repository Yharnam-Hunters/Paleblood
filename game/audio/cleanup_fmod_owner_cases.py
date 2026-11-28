#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic AJM cleanup calls and observable pool operations."""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x01118e90'
AJM_CONTEXT = '0x05519b04'
KERNEL_CONTEXT = '0x056d6ae8'
POOL_FREE = '0x011ab3a0'
CANCELLED = 0x80930017
INVALID = 0xffffffff


def dword(addr, value):
    return {'addr': addr, 'bytes': struct.pack('<I', value).hex()}


def fixture(name, *, batch=True, batch_id=0x80000010, generation=0xffffffff,
            expected_generation=0xffffffff, flags=0, cancel=0, wait=0, destroy=0,
            first=True, second=True, cancel_writes=(), wait_writes=(),
            destroy_writes=(), first_free_writes=(), alias_buffers=False):
    """Keep even normally skipped callees observable when a wrong version calls them."""
    owner = bytearray((37 * n + 9) & 255 for n in range(0x2b0))
    for offset in (0x80, 0x228, 0x260):
        struct.pack_into('<Q', owner, offset, 0)
    struct.pack_into('<I', owner, 0x1ec, 0xfacade01)
    struct.pack_into('<I', owner, 0x268, expected_generation)
    owner[0x290] = flags
    memory = [dword(AJM_CONTEXT, 0xcafe0123), {'addr': KERNEL_CONTEXT, 'pointer': 'context'},
              {'addr': 'buf:context+8', 'pointer': 'pool'}]
    for enabled, offset, target in [(batch, 0x260, 'slot'), (first, 0x80, 'first'),
                                     (second, 0x228, 'first' if alias_buffers else 'second')]:
        if enabled:
            memory.append({'addr': f'buf:owner+{offset}', 'pointer': target})
    imports = []
    for symbol, result, writes, count in [('sceAjmBatchCancel', cancel, cancel_writes, 2),
                                          ('sceAjmBatchWait', wait, wait_writes, 3),
                                          ('sceAjmInstanceDestroy', destroy, destroy_writes, 2)]:
        is_wait = symbol == 'sceAjmBatchWait'
        imports.append({'name': symbol, 'argc': 4 if is_wait else count, 'argi': [3] if is_wait else [],
                        'argu32': list(range(count)), 'ret': hex(result), 'writes': list(writes)})
    buffers = {'owner': {'size': len(owner), 'bytes': owner.hex()},
               'slot': {'size': 16, 'bytes': struct.pack('<IIII', batch_id, generation, 23, 47).hex()},
               'context': {'size': 32}, 'pool': {'size': 32}, 'secondpool': {'size': 32},
               'first': {'size': 16}, 'second': {'size': 16}}
    stubs = [{'address': POOL_FREE, 'argc': 4, 'argi': [0, 1, 2], 'argu32': [3],
              'writes': list(writes)} for writes in (first_free_writes, (), ())]
    return {'schema': 1, 'id': name, 'address': ADDRESS, 'returns': 'i32',
            'args': {'rdi': 'buf:owner'}, 'buffers': buffers, 'memory': memory,
            'imports': imports, 'stubs': stubs}


def cases():
    rows = []
    for gate, options in [('missing-slot', {'batch': False}), ('invalid-id', {'batch_id': INVALID}),
                          ('stale-generation', {'expected_generation': 0x7fffffff}),
                          ('matching', {}), ('zero-id', {'batch_id': 0}), ('zero-generation',
                          {'generation': 0, 'expected_generation': 0})]:
        for mask in range(4):
            rows.append(fixture(f'{gate}-buffers-{mask}', first=bool(mask & 1), second=bool(mask & 2), **options))
    for flags in (0, 1, 2, 4, 8, 0xfb, 0xff):
        for result in (0, 1, CANCELLED, CANCELLED - 1, CANCELLED + 1, 1 << 32):
            rows.append(fixture(f'flags-{flags}-wait-{result:x}', flags=flags, wait=result))
    for result in (1, 0x80000000, CANCELLED, 1 << 32):
        rows.append(fixture(f'cancel-{result:x}', cancel=result))
        rows.append(fixture(f'destroy-{result:x}', destroy=result))
    zero_pointer = {'bytes': '00' * 8}
    rows += [fixture('cancel-invalidates-slot', cancel_writes=[dword('buf:slot', INVALID)]),
             fixture('cancel-changes-id-and-context', cancel_writes=[dword('buf:slot', 17), dword(AJM_CONTEXT, 81)]),
             fixture('cancel-detaches-owner-slot', cancel_writes=[{'addr': 'buf:owner+0x260', **zero_pointer}]),
             fixture('wait-updates-generation-and-instance', wait_writes=[dword('buf:slot+4', 27),
                     dword('buf:owner+0x1ec', 62), dword(AJM_CONTEXT, 131)]),
             fixture('destroy-releases-first', destroy_writes=[{'addr': 'buf:owner+0x80', **zero_pointer}]),
             fixture('first-free-clears-second', first_free_writes=[{'addr': 'buf:owner+0x228', **zero_pointer}]),
             fixture('first-free-changes-pool', first_free_writes=[{'addr': 'buf:context+8', 'pointer': 'secondpool'}]),
             fixture('first-free-replaces-both-pointers', first_free_writes=[{'addr': 'buf:owner+0x80', 'pointer': 'second'},
                     {'addr': 'buf:owner+0x228', 'pointer': 'first'}]), fixture('same-buffers', alias_buffers=True),
             fixture('wait-stores-error', wait_writes=[{'arg': 3, 'offset': 0, 'bytes': '11223344'}]),
             fixture('destroy-error-after-wait', destroy=0x80000001, wait_writes=[dword('buf:slot+4', 77)]),
             fixture('cancel-error-with-writes', cancel=1, cancel_writes=[dword('buf:slot', 0)]),
             fixture('wait-error-with-writes', wait=1, wait_writes=[dword('buf:slot+4', 81)])]
    return rows


def main():
    """Write reproducible fixtures into the requested directory."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic AJM cleanup cases')


if __name__ == '__main__':
    main()
