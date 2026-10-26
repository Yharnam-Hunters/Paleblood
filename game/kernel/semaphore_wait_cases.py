#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Constructed edge fixtures for the semaphore wait wrapper at 0x0111ada0.

The wrapper forwards an opaque handle, including null, without dereferencing it.
Scripted kernel results and optional writes test the wrapper boundary only. These
are synthetic inputs, not live kernel tests or gameplay recordings.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x0111ada0'
KERNEL_CALL = '0x02fbec78'
ARGUMENT_COUNT = 3
MASK64 = (1 << 64) - 1


def case(cid, handle='0x123456789abcdef0', result=0, write=False, extra_args=None):
    """Record formal arguments and retain any scripted kernel side effects."""
    args = {'rdi': handle}
    if extra_args:
        args.update(extra_args)
    operation = {'address': KERNEL_CALL, 'argc': ARGUMENT_COUNT,
                 'ret': hex(result & MASK64)}
    if write:
        operation['argmem'] = [{'arg': 0, 'size': 8}]
        operation['writes'] = [{'arg': 0, 'bytes': '1032547698badcfe'}]
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': args,
        'buffers': {'storage': {'size': 32, 'bytes': 'a5' * 8
                               + struct.pack('<Q', 0x8877665544332211).hex() + '5a' * 16}},
        'imports': [], 'stubs': [operation],
    }


CASES = [
    case('opaque_handle_success'),
    case('null_handle_still_calls_kernel', handle='0x0'),
    case('one_handle', handle='0x1'),
    case('high_word_handle', handle='0xfedcba9800000000'),
    case('high_bit_handle', handle='0x8000000000000000'),
    case('all_handle_bits', handle='0xffffffffffffffff'),
    case('low_word_handle', handle='0x89abcdef'),
    case('positive_kernel_error', result=1),
    case('other_positive_kernel_error', result=2),
    case('negative_kernel_error', result=0x80020016),
    case('highest_low_result_bit_error', result=1 << 31),
    case('all_low_result_bits_error', result=0xffffffff),
    case('upper_return_word_ignored', result=1 << 32),
    case('upper_sign_bit_ignored', result=1 << 63),
    case('all_upper_return_bits_ignored', result=0xffffffff00000000),
    case('upper_bits_with_low_error', result=0x1234567800000001),
    case('null_handle_kernel_error', handle='0x0', result=0x80020016),
    case('bounded_handle_not_dereferenced', handle='buf:storage+0x8'),
    case('kernel_write_survives_success', handle='buf:storage+0x8', write=True),
    case('kernel_write_survives_error', handle='buf:storage+0x8', result=1, write=True),
    case('unaligned_handle_kernel_write', handle='buf:storage+0x9', write=True),
    case('incoming_registers_do_not_change_arguments', extra_args={
        'rsi': '0xffffffffffffffff', 'rdx': '0x1234567800000002',
        'rcx': '0x8000000000000001', 'r8': '0x100000000', 'r9': '0x55',
    }),
]


def main():
    """Write only synthetic fixtures to the caller-selected private directory."""
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
