#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic cases for the FMOD recording-close callback."""
import json
from pathlib import Path
import struct
import sys


def boundary(name, context, record, status, writes=()):
    call = {'address': '0x0111c240', 'argc': 2, 'ret': hex(status)}
    if writes:
        call['writes'] = list(writes)
    return {
        'schema': 1, 'id': name, 'address': '0x0111b3d0', 'returns': 'i32',
        'args': {'rdi': context, 'rsi': record, 'rdx': '0x2345678911223344'},
        'buffers': {'owner': {'size': 192, 'bytes': 'a5' * 192},
                    'record': {'size': 80, 'bytes': '3c' * 80}},
        'stubs': [call],
    }


def close_port(name, slot, count, stop_result, *, close_result=0, alias=False):
    owner = bytearray(1792)
    record = bytearray(48)
    record_offset = 32 if alias else 0
    struct.pack_into('<i', owner if alias else record, record_offset + 24, slot)
    struct.pack_into('<I', owner, 0x24c + 4 * slot, 0x1700 + slot)
    struct.pack_into('<I', owner, 0x6a8, count)
    return {
        'schema': 1, 'id': name, 'address': '0x0111b3d0', 'returns': 'i32',
        'args': {'rdi': 'buf:owner+56', 'rsi': 'buf:owner+32' if alias else 'buf:record'},
        'buffers': {'owner': {'size': len(owner), 'bytes': owner.hex()},
                    'record': {'size': len(record), 'bytes': record.hex()}},
        'imports': [{'name': 'sceAudioInClose', 'argc': 1, 'ret': hex(close_result)}],
        'stubs': [{'address': '0x011185a0', 'argc': 1, 'ret': hex(stop_result)}],
    }


def cases():
    """Check forwarding, null-aware adjustment, results, aliases and real port closure."""
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
    for slot in (-1, 0, 2):
        for count in (0, 1, 2, 0xffffffff):
            rows.append(close_port(f'port-{slot}-count-{count:x}', slot, count, 0))
    for status in (1, 0x100, 0x80000000, 0xffffffff):
        rows.append(close_port(f'stop-error-{status:x}', 2, 1, status, alias=True))
    rows.append(close_port('ignore-close-error', 0, 2, 0, close_result=0xffffffff))
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
