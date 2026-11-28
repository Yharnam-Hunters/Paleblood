#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic cases for the FMOD record-driver count callback."""
import json
from pathlib import Path
import struct
import sys


def fixture(name, users, *, initial=0, status=0, callback_count=None, offset=8,
            receiver='buf:other'):
    count = bytearray([0xa5] * 32)
    struct.pack_into('<I', count, offset, initial)
    output = f'buf:count+{offset}'
    writes = [{'arg': 0, 'bytes': struct.pack('<4i', *users).hex()}]
    if callback_count is not None:
        writes.append({'addr': output, 'bytes': struct.pack('<I', callback_count).hex()})
    return {
        'schema': 1, 'address': '0x0111b320', 'id': name, 'returns': 'i32',
        'args': {'rdi': receiver, 'rsi': output},
        'buffers': {'count': {'size': len(count), 'bytes': count.hex()},
                    'other': {'size': 32, 'bytes': '00' * 32}},
        'imports': [{'name': 'sceUserServiceGetLoginUserIdList', 'argc': 1,
                     'ret': hex(status), 'writes': writes}],
    }


def cases():
    """Exercise all slot masks and both directions of each status and count gate."""
    rows = []
    ids = [0, -2, 0x7fffffff, -0x80000000]
    for mask in range(16):
        users = [user if mask & (1 << index) else -1 for index, user in enumerate(ids)]
        for status in (0, 1):
            rows.append(fixture(f'mask-{mask:x}-status-{status}', users, status=status))
    for initial in (1, 0x100, 0x80000000, 0x7fffffff, 0xffffffff):
        rows.append(fixture(f'already-{initial:x}', ids, initial=initial, status=0x80960002,
                            callback_count=13))
    for status in (0x7fffffff, 0x80000000, 0x80960001, 0x80960002, 0x80960003,
                   0xffffffff, 0x100000000, 0x180960002, 0xffffffff00000001):
        rows.append(fixture(f'service-{status:x}', ids, status=status, callback_count=11))
    for count in (1, 0x7ffffffd, 0xfffffffe, 0xffffffff):
        rows.append(fixture(f'callback-count-{count:x}', [-1, 0, -1, -1],
                            callback_count=count))
    rows.extend([
        fixture('no-users-after-write', [-1] * 4, callback_count=0x12345678),
        fixture('duplicate-users', [7] * 4),
        fixture('receiver-aliases-output', ids, offset=28, receiver='buf:count+28'),
        fixture('null-receiver', ids, offset=0, receiver=0),
    ])
    return rows


def main():
    """Write one synthetic capture per case."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
