#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Constructed edge fixtures for the semaphore constructor at 0x0111ad10.

The public API initializes an eight-byte opaque pointer on success. Every successful
stub writes that complete object; no fixture reads unspecified prior stack bytes.
Calls record all six arguments and the original name's bytes, without replacing its
read-only storage. These are synthetic wrapper cases, not live kernel or gameplay tests.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x0111ad10'
CREATE_SEMAPHORE = '0x02fbec58'
NAME_BYTES = 15
HANDLE = 0x123456789abcdef0


def word(value):
    """Encode one complete opaque pointer value for the scripted API output."""
    return struct.pack('<Q', value & ((1 << 64) - 1)).hex()


def case(cid, handle=HANDLE, result=0, write_handle=True, output='buf:out+0x8',
         initial_output=0x8877665544332211, extra_args=None):
    """Keep the public output separate from the local pointer supplied to the API."""
    args = {'rdi': output}
    if extra_args:
        args.update(extra_args)
    operation = {
        'address': CREATE_SEMAPHORE, 'argc': 6,
        'argmem': [{'arg': 1, 'size': NAME_BYTES}],
        'ret': hex(result & ((1 << 64) - 1)),
    }
    if write_handle:
        operation['writes'] = [{'arg': 0, 'bytes': word(handle)}]
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': args,
        'buffers': {
            'out': {'size': 24, 'bytes': 'a5' * 8 + word(initial_output) + '5a' * 8},
        },
        'imports': [],
        'stubs': [operation],
    }


CASES = [
    case('opaque_pointer_success'),
    case('zero_opaque_pointer', handle=0),
    case('one_opaque_pointer', handle=1),
    case('low_word_pointer', handle=0x89abcdef),
    case('high_word_pointer', handle=0xfedcba9800000000),
    case('high_bit_pointer', handle=1 << 63),
    case('all_pointer_bits', handle=(1 << 64) - 1),
    case('success_replaces_zero_output', initial_output=0),
    case('success_replaces_all_output_bits', initial_output=(1 << 64) - 1),
    case('unaligned_output_store', output='buf:out+0x9'),
    case('positive_api_error_after_local_write', result=1),
    case('negative_api_error_after_local_write', result=0x80020016),
    case('highest_low_return_bit_error', result=1 << 31),
    case('all_low_return_bits_error', result=0xffffffff),
    case('api_error_without_local_write', result=0x80020016, write_handle=False),
    case('null_output_api_error', result=0x80020016, output='0x0'),
    case('null_output_api_error_without_local_write', result=1,
         write_handle=False, output='0x0'),
    case('high_output_pointer_api_error', result=1, output='0xffffffff00000000'),
    case('upper_return_bits_ignored', result=1 << 32),
    case('all_upper_return_bits_ignored', result=0xffffffff00000000),
    case('upper_return_bits_with_low_error', result=0x1234567800000001),
    case('incoming_registers_do_not_change_fixed_arguments', extra_args={
        'rsi': '0xffffffffffffffff', 'rdx': '0x1234567800000002',
        'rcx': '0x8000000000000001', 'r8': '0x100000000', 'r9': '0x55',
    }),
]


def main():
    """Write the synthetic fixture set into the caller-selected private directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (output / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
