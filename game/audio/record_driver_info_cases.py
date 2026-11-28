#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate recording-driver user, name and identifier comparisons."""
import json
from pathlib import Path
import struct
import sys


def case(label, driver=1, users=(11, -1, -2, 23), *, name=32, identifier=128,
         login=0, username=0, length=48, initial=b'old', service_name=b'user',
         observe=True, append_writes=False, append_return=0, unused_owner=0):
    arena = bytearray([0xa5] * 192)
    if name is not None:
        arena[name:name + len(initial) + 1] = initial + b'\0'
    login_call = {'name': 'sceUserServiceGetLoginUserIdList', 'argc': 1, 'ret': login,
                  'writes': [{'arg': 0, 'bytes': struct.pack('<4i', *users).hex()}]}
    username_call = {'name': 'sceUserServiceGetUserName', 'argc': 3,
                     'argi': [1, 2], 'argu32': [0], 'ret': username}
    if service_name is not None:
        username_call['writes'] = [{'arg': 1, 'bytes': (service_name + b'\0').hex()}]
    append = {'address': '0x011c4870', 'argc': 2, 'ret': append_return}
    if append_writes:
        append['writes'] = [{'addr': f'buf:arena+{identifier}', 'bytes': '55' * 16}]
    def ptr(offset):
        return '0x0' if offset is None else f'buf:arena+{offset}'
    return {'schema': 1, 'id': label, 'address': '0x0111bd50', 'returns': 'i32',
            'args': {'rdi': hex(unused_owner), 'rsi': hex(driver % (1 << 64)),
                     'rdx': ptr(name), 'rcx': hex(length % (1 << 64)), 'r8': ptr(identifier)},
            'buffers': {'arena': {'size': len(arena), 'bytes': arena.hex()}},
            'imports': [login_call, username_call], 'stubs': [append] if observe else []}


def cases():
    """Exercise sparse login lists, signed selection, optional outputs and callback effects."""
    rows = []
    for mask in range(16):
        users = tuple(user if mask & (1 << i) else -1
                      for i, user in enumerate((0, 0x12345678, -2, 0x7fffffff)))
        for driver in range(-1, 13):
            rows.append(case(f'users-{mask:x}-driver-{driver}', driver, users))
    for result in (0, 1, 0x7fffffff, 0x80000000, 0x80960001, 0x80960002,
                   0x80960003, 0xffffffff, 1 << 32, 0xfedcba9880960002):
        rows.append(case(f'login-{result:x}', login=result))
        rows.append(case(f'username-{result:x}', username=result))
    rows.append(case('username-nonzero-high-byte', username=0x100))
    for length in (-0x80000000, -1, 0, 1, 19, 0x7fffffff, 0x1122334400000050):
        rows.append(case(f'length-{length}', length=length))
    for driver in (-0x80000000, 0x7fffffff, 0x2468acef00000001):
        rows.append(case(f'wide-driver-{driver}', driver))
    for driver in (-1, 0, 1, 2):
        for name in (None, 32):
            for identifier in (None, 128):
                rows.append(case(f'outputs-{driver}-{name}-{identifier}', driver, name=name,
                                 identifier=identifier, unused_owner=0x1234567800000011))
        for label, prefix in [('empty', b''), ('service', b'Mic'), ('existing', None)]:
            rows.append(case(f'real-append-{driver}-{label}',
                             driver, service_name=prefix, observe=False))
        for delta in (-7, 0, 1, 6, 12):
            rows.append(case(f'overlap-{driver}-{delta}', driver, identifier=32 + delta,
                             observe=False))
        rows.append(case(f'append-overwrites-id-{driver}', driver, identifier=131,
                         append_writes=True, append_return=0x123456781234abcd))
    for delta in (-2, 0, 1, 5):
        rows.append(case(f'username-error-overlap-{delta}', identifier=32 + delta,
                         username=1, service_name=b'failed'))
    rows.append(case('identifier-last-sixteen', identifier=176))
    return rows


def main():
    """Write the fixtures under their unique IDs."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len(rows) == len({row['id'] for row in rows})
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
