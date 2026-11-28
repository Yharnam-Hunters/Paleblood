#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic cases for the FMOD recording-open callback."""
import json
from pathlib import Path
import struct
import sys


def boundary(name, context, record, status, writes=()):
    call = {'address': '0x0111c050', 'argc': 2, 'ret': hex(status)}
    if writes:
        call['writes'] = list(writes)
    return {
        'schema': 1, 'id': name, 'address': '0x0111b3b0', 'returns': 'i32',
        'args': {'rdi': context, 'rsi': record, 'rdx': '0x2345678911223344'},
        'buffers': {'owner': {'size': 192, 'bytes': 'a5' * 192},
                    'record': {'size': 80, 'bytes': '3c' * 80}},
        'stubs': [call],
    }


def open_port(name, slot, users, *, login=0, handle=17, block=0x12345000,
              count=0, thread=0):
    owner = bytearray(b'\xa5' * 1792)
    record = bytearray(b'\x3c' * 128)
    struct.pack_into('<I', owner, 0x6a8, count)
    struct.pack_into('<Q', owner, 0x60, 0x1234567082345678)
    struct.pack_into('<i', record, 24, slot)
    return {
        'schema': 1, 'id': name, 'address': '0x0111b3b0', 'returns': 'i32',
        'args': {'rdi': 'buf:owner+56', 'rsi': 'buf:record'},
        'buffers': {'owner': {'size': len(owner), 'bytes': owner.hex()},
                    'record': {'size': len(record), 'bytes': record.hex()},
                    'services': {'size': 32, 'bytes': '00' * 32}},
        'memory': [{'addr': '0x056d6ae8', 'pointer': 'services'},
                   {'addr': 'buf:services+8', 'bytes': '7856341200000000'}],
        'imports': [{'name': 'sceUserServiceGetLoginUserIdList', 'argc': 1, 'ret': login,
                     'writes': [{'arg': 0, 'bytes': struct.pack('<4i', *users).hex()}]},
                    {'name': 'sceAudioInOpen', 'argc': 6, 'ret': handle}],
        'stubs': [{'address': '0x011aaea0', 'argc': 5, 'ret': block},
                  {'address': '0x01118420', 'argc': 10, 'argi': [0, 1, 2, 3, 5, 9],
                   'argu32': [4, 6, 8], 'argu8': [7], 'ret': thread}],
    }


def cases():
    """Check forwarding, null-aware adjustment, results, aliases and real port opening."""
    rows = []
    pointers = [
        ('separate', 'buf:owner+88', 'buf:record+16'),
        ('alias', 'buf:owner+88', 'buf:owner+32'),
        ('null-context', 0, 'buf:record'),
        ('null-record', 'buf:owner+56', 0),
        ('both-null', 0, 0),
        ('underflow', 1, '0x100000041'),
        ('bias', 56, '0x2468ace000000001'),
        ('high', '0x1234567800000058', '0x3344556688000020'),
        ('last-word', '0xffffffffffffffff', '0xffffffff00000008'),
    ]
    for label, context, record in pointers:
        for status in (0, 1, 0x100, 0x80000000, 0xffffffff, 0xfedcba9876543210):
            rows.append(boundary(f'{label}-{status:x}', context, record, status))
    writes = ({'arg': 0, 'bytes': '1032547698badcfe'},
              {'arg': 1, 'bytes': '8877665544332211'})
    for label, record in [('separate', 'buf:record+8'), ('same', 'buf:owner+8'),
                          ('overlap', 'buf:owner+12')]:
        rows.append(boundary('writes-' + label, 'buf:owner+64', record, 0x13579bdf, writes))
    once = boundary('open-once', 'buf:owner+64', 'buf:record+8', 0x12345678)
    once['stubs'].append({'address': '0x0111c050', 'argc': 2, 'ret': '0x87654321'})
    rows.append(once)
    users = (17, -1, 35, 41)
    for login in (0x80960002, 0x80960001, 0x80000000, 0xffffffff):
        rows.append(open_port(f'login-{login:x}', 0, users, login=login))
    rows.append(open_port('no-users', 0, (-1,) * 4))
    for slot in (-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12):
        rows.append(open_port(f'audio-open-{slot}', slot, users, handle=-1))
    rows.append(open_port('last-user', 2, (-1, -1, -1, 0), handle=-2147483648))
    rows.append(open_port('allocation-failed', 2, users, handle=0, block=0))
    for count in (0, 1, 2, 0xffffffff):
        rows.append(open_port(f'open-count-{count:x}', 7, users, count=count))
    for status in (1, 0x100, 0x80000000, 0xffffffff):
        rows.append(open_port(f'thread-error-{status:x}', 3, users, thread=status))
    return rows


def main():
    """Write one synthetic case per file."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
