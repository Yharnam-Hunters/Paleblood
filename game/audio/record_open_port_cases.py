#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate deterministic recording-port open comparisons."""
import itertools
import json
from pathlib import Path
import struct
import sys

OWNER = 128
RECORD = 2000
PORT_INDEX = 24
INPUT_HANDLES = 0x24c
CAPTURE_BUFFERS = 0x2d0
WRITE_OFFSETS = 0x3d0
POSITIONS = 0x450
CAPACITIES = 0x4d0
ACTIVE_PORTS = 0x6a8
SERVICE_CONTEXT = 0x60


def write(offset, value, width=4, buffer='arena'):
    return {'addr': f'buf:{buffer}+{offset}', 'bytes': value.to_bytes(width, 'little').hex()}


def case(name, index=1, users=(17, -1, 35, 41), *, login=0, handle=17,
         allocation=0x123456780000f123, count=1, start=0, alias=None, effects=None):
    arena = bytearray(b'\xa5' * 2240)
    effects = effects or {}
    for port in range(-4, 13):
        for table in (INPUT_HANDLES, WRITE_OFFSETS, POSITIONS, CAPACITIES):
            struct.pack_into('<I', arena, OWNER + table + 4 * port, 0x80001000 + port)
        struct.pack_into('<Q', arena, OWNER + CAPTURE_BUFFERS + 8 * port, 0x1234567800001000 + port)
    struct.pack_into('<Q', arena, OWNER + SERVICE_CONTEXT, 0x2468ace00000001b)
    struct.pack_into('<I', arena, OWNER + ACTIVE_PORTS, count)
    index_address = RECORD + PORT_INDEX if alias is None else OWNER + alias
    record = index_address - PORT_INDEX
    struct.pack_into('<i', arena, index_address, index)
    login_call = {'name': 'sceUserServiceGetLoginUserIdList', 'argc': 1, 'ret': login,
                  'writes': [{'arg': 0, 'bytes': struct.pack('<4i', *users).hex()}]}
    device = {'name': 'sceAudioInOpen', 'argc': 6, 'argi': [], 'argu32': list(range(6)), 'ret': handle}
    allocate = {'address': '0x011aaea0', 'argc': 5, 'argi': [0, 2], 'argu32': [1, 3, 4], 'ret': allocation}
    thread = {'address': '0x01118420', 'argc': 10, 'argi': [0, 1, 2, 3, 5, 9],
              'argu32': [4, 6, 8], 'argu8': [7], 'ret': start}
    for call, key in [(login_call, 'login'), (device, 'open'), (allocate, 'allocate'), (thread, 'start')]:
        if key in effects:
            call.setdefault('writes', []).extend(effects[key])
    return {'schema': 1, 'id': name, 'address': '0x0111c050', 'returns': 'i32',
            'args': {'rdi': f'buf:arena+{OWNER}', 'rsi': f'buf:arena+{record}'},
            'buffers': {'arena': {'size': len(arena), 'bytes': arena.hex()},
                        'services': {'size': 32, 'bytes': '00' * 32}},
            'memory': [{'addr': '0x056d6ae8', 'pointer': 'services'},
                       {'addr': 'buf:services+8', 'bytes': '0908070605040302'}],
            'imports': [login_call, device], 'stubs': [allocate, thread]}


def cases():
    """Exercise all login masks, port type choices, failure exits, wraparound and aliases."""
    rows = []
    for mask in range(16):
        users = tuple(user if mask & (1 << i) else -1 for i, user in enumerate([0, 17, -2, 71]))
        for index in range(-2, 14):
            rows.append(case(f'users-{mask:x}-port-{index}', index, users))
    for status in (0, 1, 0x7fffffff, 0x80960001, 0x80960002, 0x80960003,
                   0x80000000, 0xffffffff, 0x1234567800000000, 0x1234567880960002):
        rows.append(case(f'login-{status:x}', login=status))
    for index in (-0x80000000, 0x7fffffff):
        rows.append(case(f'unavailable-{index}', index, login=0x80960002))
    for handle in (0, 1, 0x100, 0x7fffffff, 0x80000000, 0xffffffff, 0xffffffff00000000):
        rows.append(case(f'handle-{handle:x}', handle=handle))
    for block in (0, 1, 0xffffffff, 1 << 32, 0x8000000000000000):
        rows.append(case(f'block-{block:x}', allocation=block))
    for count, status in itertools.product((0, 1, 2, 0x100, 0x80000000, 0xffffffff),
                                            (0, 1, 0x100, 0x80000000, 0xffffffff, 1 << 32)):
        rows.append(case(f'thread-{count:x}-{status:x}', count=count, start=status))
    index_word = RECORD + PORT_INDEX
    for event in ('login', 'open', 'allocate'):
        rows.append(case(f'{event}-changes-port', effects={event: [write(index_word, 5)]}))
    rows.append(case('write-offset-changes-index', 3, alias=WRITE_OFFSETS + 4 * 3))
    rows.append(case('published-handle-changes-index', 1, handle=3, alias=INPUT_HANDLES + 4))
    rows.append(case('published-buffer-changes-index', 1, allocation=2, alias=CAPTURE_BUFFERS + 8))
    rows.append(case('open-replaces-memory-pool', effects={'open': [write(8, 0xfedcba9876543210, 8, 'services')]}))
    rows.append(case('allocation-changes-thread-argument', count=0,
                     effects={'allocate': [write(OWNER + SERVICE_CONTEXT, 0xfedcba9876543210, 8)]}))
    rows.append(case('allocation-starts-thread', count=7, effects={'allocate': [write(OWNER + ACTIVE_PORTS, 0)]}))
    rows.append(case('allocation-skips-thread', count=0, effects={'allocate': [write(OWNER + ACTIVE_PORTS, 5)]}))
    rows.append(case('start-changes-count', count=0, effects={'start': [write(OWNER + ACTIVE_PORTS, 19)]}))
    rows.append(case('start-error-preserves-count', count=0, start=0x100,
                     effects={'start': [write(OWNER + ACTIVE_PORTS, 19)]}))
    rows.append(case('allocation-zero-effects', allocation=0,
                     effects={'allocate': [write(index_word, 5), write(OWNER + ACTIVE_PORTS, 8)]}))
    rows.append(case('open-error-effects', handle=0x80000000,
                     effects={'open': [write(index_word, 2), write(OWNER + ACTIVE_PORTS, 4)]}))
    rows.append(case('distinct-callback-indices', count=0, effects={
        'login': [write(index_word, 5)], 'open': [write(index_word, 2)],
        'allocate': [write(index_word, 4)], 'start': [write(OWNER + ACTIVE_PORTS, 0xffffffff)]}))
    return rows


def main():
    """Write each comparison input under its unique ID."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
