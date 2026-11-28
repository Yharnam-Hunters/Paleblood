#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate FMOD current-thread adapter cases for the original call and its boundary."""
import json
from pathlib import Path
import sys


def fixture(name, receiver, output):
    return {
        'schema': 1, 'id': name, 'address': '0x011186b0', 'returns': 'i32',
        'args': {'rdi': receiver, 'rsi': output},
        'buffers': {'out': {'size': 64, 'bytes': 'a5' * 64},
                    'other': {'size': 64, 'bytes': '3c' * 64}},
    }


def cases():
    """Observe eight-byte output through the real helper and pointer forwarding at its boundary."""
    rows = []
    for suffix, receiver, offset in [('separate', 'buf:other', 8), ('null', 0, 0),
                                     ('alias', 'buf:out+16', 16), ('last', 'buf:other+8', 56)]:
        for handle in (0, 1, 0xffffffff, 0x100000000, 0x12345678fedcba98, 1 << 63,
                       0xffffffffffffffff):
            case = fixture(f'real-{suffix}-{handle:x}', receiver, f'buf:out+{offset}')
            case['imports'] = [{'name': 'scePthreadSelf', 'argc': 0, 'ret': hex(handle)}]
            rows.append(case)
    # A second scripted handle makes an accidental repeated query observable.
    case = fixture('query-once', 'buf:other', 'buf:out+8')
    case['imports'] = [{'name': 'scePthreadSelf', 'argc': 0, 'ret': hex(value)}
                       for value in (0x1122334455667788, 0xfedcba9876543210)]
    rows.append(case)
    for name, receiver, output in [('distinct', 'buf:other', 'buf:out+24'),
                                   ('aliased', 'buf:out+8', 'buf:out+8'),
                                   ('null-out', 'buf:other', 0)]:
        for result in (0, 1, 0x80000000, 0xffffffff, 0xfedcba9876543210):
            case = fixture(f'boundary-{name}-{result:x}', receiver, output)
            call = {'address': '0x0111a8a0', 'argc': 1, 'ret': hex(result)}
            if output:
                call['writes'] = [{'arg': 0, 'bytes': '8070605040302010'}]
            case['stubs'] = [call]
            rows.append(case)
    return rows


def main():
    """Write one synthetic capture per case."""
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({r['id'] for r in rows}) == len(rows)
    for row in rows:
        (output / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
