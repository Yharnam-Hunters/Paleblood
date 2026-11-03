#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic terminated strings for the engine strlen; no expected-result oracle."""
import json
from pathlib import Path
import sys

ADDRESS = '0x011c47f0'
IGNORED_REGISTERS = ('rsi', 'rdx', 'rcx', 'r8', 'r9')
ABI = {
    'calling_convention': 'sysv_amd64',
    'confidence': 'supported',
    'stack_alignment': 16,
    'returns': {'type': 'uint64_t'},
    'arguments': [
        {'register': 'rdi', 'type': 'readable byte sequence through the first NUL',
         'direction': 'input'},
    ] + [
        {'register': register, 'type': 'uint64_t ignored GP seed; not a source formal',
         'direction': 'input'}
        for register in IGNORED_REGISTERS
    ],
    'source_formal_slots': ['rdi'],
    'input_regions': ['data'],
    'writable_regions': [],
    'evidence': [
        'The complete original body reads consecutive bytes until a NUL.',
        'A 32-bit subtraction gives the length; writing EAX zeroes upper RAX.',
        'Other incoming GP registers are ignored, rather than source formals.',
    ],
    'execution_limits': {'timeout_seconds': 5},
    'observation_limits': (
        'Synthetic inputs compare final memory and the return value; arbitrary native '
        'reads and temporary restored writes are not instrumented. No gameplay is claimed.'
    ),
}


def case(case_id, payload, offset=17, size=640, incoming=None):
    """Place one terminated input among sentinels; all supplied bytes are compared."""
    if not isinstance(payload, bytes) or b'\0' not in payload:
        raise ValueError('a terminated byte string is required')
    if offset < 0 or offset + len(payload) > size:
        raise ValueError('the payload must fit the supplied buffer')
    data = bytearray([0xa5] * size)
    data[offset:offset + len(payload)] = payload
    args = {
        'rdi': f'buf:data+0x{offset:x}', 'rsi': '0x8000000000000001',
        'rdx': '0x76543210fedcba98', 'rcx': '0xffffffffffffffff',
        'r8': '0x80000000ffffffff', 'r9': '0x123456789abcdef0',
    }
    if set(incoming or {}) - set(IGNORED_REGISTERS):
        raise ValueError('ignored seeds cannot replace the string pointer')
    args.update(incoming or {})
    return {
        'schema': 1, 'address': ADDRESS, 'id': case_id, 'returns': 'i64',
        'args': args, 'buffers': {'data': {'size': size, 'bytes': data.hex()}},
        'memory': [], 'imports': [], 'stubs': [],
    }


def cases():
    fixtures = [
        case('empty', b'\0tail'),
        case('single', b'Q\0tail'),
        case('ordinary', b'AbCdeF012_-\0tail'),
        case('ascii_edges', b'@AZ[az{`byBY\0tail'),
        case('high_bytes', b'\x80\xff\x81\xfe\x7fAZaz\0tail'),
        case('all_nonzero_bytes', bytes(range(1, 256)) + b'\0'),
        case('reverse_nonzero_bytes', bytes(range(255, 0, -1)) + b'\0'),
        case('exact_empty', b'\0', offset=0, size=1),
        case('exact_span', b'aZ\x80zA\xff\0', offset=0, size=7),
        case('ignored_zero', b'AzbY\0tail',
             incoming={reg: '0x0' for reg in IGNORED_REGISTERS}),
        case('ignored_mapped', b'AzbY\0tail',
             incoming={reg: 'buf:data+0x7' for reg in IGNORED_REGISTERS}),
    ]
    for length in (2, 3, 7, 15, 16, 17, 31, 32, 63, 64, 127, 256, 273):
        payload = bytes((index % 255) + 1 for index in range(length)) + b'\0'
        fixtures.append(case(f'length_{length:03d}', payload))
    for offset in (0, 1, 3, 7, 15, 31):
        fixtures.append(case(f'unaligned_{offset:02x}', b'aBZy\x80z\xff\0tail', offset))
    return fixtures


def main():
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=False)
    fixtures = cases()
    for fixture in fixtures:
        (destination / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    (destination / 'abi.json').write_text(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(fixtures)} authored input states written to {destination}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
