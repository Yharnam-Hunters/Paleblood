#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic recording-port close cases, including callback changes of port."""
import json
from pathlib import Path
import struct
import sys


OWNER = 64
RECORD = 1888
INPUT_HANDLES = 0x24c
CAPTURE_BUFFERS = 0x2d0
ACTIVE_COUNT = 0x6a8
PORT_INDEX = 24


def word(offset, value, width=4):
    return {'addr': f'buf:arena+{offset}', 'bytes': value.to_bytes(width, 'little').hex()}


def case(name, slot=1, count=2, *, occupied=True, stop=0, close=0, effects=None,
         alias_count=False):
    arena = bytearray(b'\xa5' * 2048)
    record = OWNER + ACTIVE_COUNT - PORT_INDEX if alias_count else RECORD
    for i in range(-3, 13):
        struct.pack_into('<I', arena, OWNER + INPUT_HANDLES + 4*i, (0x8a000000 + i) & 0xffffffff)
        block = 0x1234567800001000 + i * 64 if occupied else 0
        struct.pack_into('<Q', arena, OWNER + CAPTURE_BUFFERS + 8*i, block)
    struct.pack_into('<i', arena, record + PORT_INDEX, slot)
    struct.pack_into('<I', arena, OWNER + ACTIVE_COUNT, count)
    effects = effects or {}
    audio = {'name': 'sceAudioInClose', 'argc': 1, 'argi': [], 'argu32': [0],
             'ret': close, 'writes': effects.get('close', [])}
    thread = {'address': '0x011185a0', 'argc': 1, 'ret': stop, 'writes': effects.get('stop', [])}
    free = {'address': '0x011ab3a0', 'argc': 4, 'ret': '0xfedcba9876543210',
            'writes': effects.get('free', [])}
    return {'schema': 1, 'id': name, 'address': '0x0111c240', 'returns': 'i32',
            'args': {'rdi': f'buf:arena+{OWNER}', 'rsi': f'buf:arena+{record}'},
            'buffers': {'arena': {'size': len(arena), 'bytes': arena.hex()},
                        'services': {'size': 32, 'bytes': '7c' * 32}},
            'memory': [{'addr': '0x056d6ae8', 'pointer': 'services'},
                       {'addr': 'buf:services+8', 'bytes': '8877665544332211'}],
            'imports': [audio], 'stubs': [thread, free]}


def cases():
    """Check close, wrapping counts, stop results, release arguments and re-reads."""
    rows = []
    for slot in (-2, -1, 0, 1, 5, 11):
        for count in (0, 1, 2, 0x80000000, 0xffffffff):
            for occupied in (False, True):
                rows.append(case(f'port-{slot}-count-{count:x}-block-{int(occupied)}',
                                 slot, count, occupied=occupied))
    for status in (1, 0x100, 0x80000000, 0xffffffff, 0x100000000):
        rows.append(case(f'stop-{status:x}', count=1, stop=status))
        rows.append(case(f'ignore-close-{status:x}', close=status))
    index = RECORD + PORT_INDEX
    count_field = OWNER + ACTIVE_COUNT
    current = OWNER + CAPTURE_BUFFERS + 8
    pool = {'addr': 'buf:services+8', 'bytes': '0908070605040302'}
    rows.append(case('close-changes-index', effects={'close': [word(index, 5)]}))
    rows.append(case('close-starts-stop', effects={'close': [word(count_field, 1)]}))
    rows.append(case('close-skips-stop', count=1, effects={'close': [word(count_field, 2)]}))
    rows.append(case('close-clears-block', effects={'close': [word(current, 0, 8)]}))
    rows.append(case('stop-changes-index', count=1, effects={'stop': [word(index, 3)]}))
    rows.append(case('stop-changes-pool', count=1, effects={'stop': [pool]}))
    rows.append(case('stop-installs-block', count=1, occupied=False,
                     effects={'stop': [word(current, 0xf00dcafe, 8)]}))
    rows.append(case('failed-stop-effects', count=1, stop=0x100,
                     effects={'stop': [word(index, 5), word(count_field, 9)]}))
    rows.append(case('release-changes-index', effects={'free': [word(index, 3)]}))
    rows.append(case('release-replaces-block', effects={'free': [word(current, 0x11223344, 8)]}))
    rows.append(case('all-callbacks', count=7,
                     effects={'close': [word(index, 2), word(count_field, 1)],
                              'stop': [word(index, 3), pool], 'free': [word(index, 4)]}))
    for count in (1, 2, 5):
        rows.append(case(f'index-is-count-{count}', slot=count, count=count, alias_count=True))
    return rows


def main():
    """Write a separate fixture for each synthetic case."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
