#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate signed recording-input selection, aliasing and error cases."""
import json
from pathlib import Path
import struct
import sys


def case(label, driver=1, users=(0x12345678, -1, -2, 0), *, login=0,
         user_output=16, type_output=28, effects=False, owner=0, fill=0xa5):
    def pointer(offset):
        return '0x0' if offset is None else f'buf:arena+{offset}'

    writes = [{'arg': 0, 'bytes': struct.pack('<4i', *users).hex()}]
    if effects:
        writes.extend([{'addr': 'buf:arena+16', 'bytes': '083929ac'},
                       {'addr': 'buf:arena+28', 'bytes': 'f7123489'}])
    return {'schema': 1, 'id': label, 'address': '0x0111bc90', 'returns': 'i32',
            'args': {'rdi': hex(owner), 'rsi': hex(driver % (1 << 64)),
                     'rdx': pointer(user_output), 'rcx': pointer(type_output)},
            'buffers': {'arena': {'size': 64, 'bytes': bytes([fill] * 64).hex()}},
            'imports': [{'name': 'sceUserServiceGetLoginUserIdList', 'argc': 1,
                         'ret': login, 'writes': writes}], 'stubs': []}


def cases():
    """Cover every login-slot mask and both the signed and physical output boundaries."""
    rows = []
    for mask in range(16):
        users = tuple(user if mask & (1 << i) else -1
                      for i, user in enumerate((0, -2, 0x12345678, -0x80000000)))
        for driver in range(-2, 14):
            rows.append(case(f'mask-{mask:x}-index-{driver}', driver, users))
    for status in (0, 1, 0x100, 0x7fffffff, 0x80000000, 0x80960001, 0x80960002,
                   0x80960003, 0xffffffff, 1 << 32, 0xabcd123480960002, 0xff00000000000001):
        rows.append(case(f'login-{status:x}', login=status, effects=True))
    for value in (-0x80000000, -0x7fffffff, 0x7fffffff, 0x1234567800000001, 0x12345678ffffffff):
        rows.append(case(f'wide-{value}', value, owner=0x123456789abcdef0))
    for driver in (-2, 0, 1, 2, 3, 5):
        for offset in (-3, -2, -1, 0, 1, 2, 3):
            rows.append(case(f'overlap-{driver}-{offset}', driver, type_output=16 + offset))
    for driver in (-1, 0, 1, 2, 8, 9):
        rows.append(case(f'login-writes-{driver}', driver, effects=True))
        rows.append(case(f'bounded-{driver}', driver, user_output=60, type_output=0, fill=0x7b))
    rows.append(case('negative-no-type-buffer', -1, type_output=None))
    rows.append(case('unavailable-no-outputs', login=0x80960002, user_output=None, type_output=None))
    rows.append(case('no-users-no-outputs', users=(-1,) * 4, user_output=None, type_output=None))
    rows.append(case('out-of-range-no-outputs', driver=12, user_output=None, type_output=None))
    return rows


def main():
    """Write each complete synthetic input under its unique ID."""
    path = Path(sys.argv[1])
    path.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len(rows) == len({r['id'] for r in rows})
    for r in rows:
        (path/(r['id'] + '.json')).write_text(json.dumps(r, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
