#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic close-wrapper wire inputs, without real descriptor or kernel effects."""
import json
from pathlib import Path
import sys

ADDRESS = '0x0111a6b0'
KERNEL_CLOSE = '0x02fbf438'
MASK64 = (1 << 64) - 1
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported',
    'evidence': ['Pinned original 23-byte close wrapper; authenticated sceKernelClose JUMP_SLOT/NID',
                 'Caller loads a full qword descriptor; wrapper tests only EAX and never null-skips'],
    'stack_alignment': 16,
    'arguments': [{'register': 'rdi', 'type': 'uintptr_t opaque descriptor wire value', 'direction': 'input'}],
    'returns': {'type': 'int32_t zero or 0x13 from low32 kernel status'},
    'input_regions': [], 'writable_regions': [],
    'execution_limits': {'timeout_seconds': 5},
}


def case(cid, descriptor=0x1234567887654321, result=0):
    """The numeric descriptor is observed, never treated as readable/writable storage."""
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
            'args': {'rdi': hex(descriptor & MASK64)}, 'buffers': {}, 'imports': [],
            'stubs': [{'address': KERNEL_CLOSE, 'argc': 1, 'ret': hex(result & MASK64)}]}


CASES = [
    case('ordinary_success'),
    case('zero_descriptor_still_calls', descriptor=0),
    case('one_descriptor', descriptor=1),
    case('high_word_descriptor', descriptor=0xfedcba9800000000),
    case('high_sign_descriptor', descriptor=1 << 63),
    case('all_descriptor_bits', descriptor=MASK64),
    case('positive_status_one', result=1),
    case('positive_status_two', result=2),
    case('low_status_sign_bit', result=0x80000000),
    case('all_low_status_bits', result=0xffffffff),
    case('upper_word_ignored', result=1 << 32),
    case('upper_sign_ignored', result=1 << 63),
    case('all_upper_bits_ignored', result=0xffffffff00000000),
    case('upper_bits_low_error', result=0x1234567800000001),
    case('all_return_bits_error', result=MASK64),
    case('zero_descriptor_error', descriptor=0, result=1),
]


def main():
    """Write authored inputs plus ABI into a fresh destination, never overwriting files."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=False)
    for fixture in CASES:
        with (output / (fixture['id'] + '.json')).open('x') as stream:
            stream.write(json.dumps(fixture, indent=1) + '\n')
    with (output / 'abi.json').open('x') as stream:
        stream.write(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
