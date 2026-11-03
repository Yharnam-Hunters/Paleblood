#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the mutex-unlock wrapper at 0x0111acf0.

These check the null fast path, opaque pointer forwarding, low-32-bit kernel results,
and unchanged kernel writes surrounded by canaries. They are constructed inputs,
never recordings from gameplay.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x0111acf0'
HANDLE = 0xfedcba9876543210
WRITTEN_HANDLE = 0x0123456789abcdef


def case(cid, result=0, handle=HANDLE, write=WRITTEN_HANDLE, offset=8, pointer=None):
    """Script one kernel call, or leave it unconsumed when the pointer is null."""
    mapped = pointer is None
    kernel_call = {'name': 'scePthreadMutexUnlock', 'argc': 1,
                   'ret': hex(result & ((1 << 64) - 1))}
    if mapped and write is not None:
        kernel_call['writes'] = [{'arg': 0, 'offset': 0, 'size': 8,
                                 'bytes': struct.pack('<Q', write).hex()}]
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': f'buf:mutex+{offset:#x}' if mapped else pointer},
        'buffers': {'mutex': {'size': 32, 'bytes': ('a5' * offset)
                             + struct.pack('<Q', handle).hex() + ('5a' * (24 - offset))}},
        'imports': [kernel_call],
    }


CASES = [
    case('null_pointer_skips_kernel', result=0x80020016, pointer='0x0'),
    case('success_preserves_kernel_write'),
    case('positive_kernel_error', result=1),
    case('negative_kernel_error', result=0x80020016),
    case('all_low_result_bits_set', result=-1),
    case('upper_result_word_ignored', result=1 << 32),
    case('upper_sign_bit_without_low_error', result=1 << 63),
    case('upper_bits_with_positive_error', result=(1 << 32) | 1),
    case('upper_bits_with_negative_error', result=0x1234567880020016),
    case('success_keeps_original_handle', write=None),
    case('error_keeps_original_handle', result=0x80020016, write=None),
    case('zero_initial_handle', handle=0),
    case('high_bit_initial_handle', handle=1 << 63),
    case('all_bits_initial_handle', handle=(1 << 64) - 1),
    case('unaligned_mutex_storage', offset=9),
    case('opaque_high_pointer_with_zero_low_word', pointer='0x1234567800000000'),
    case('opaque_mixed_pointer_passed_unchanged', pointer='0x1234567887654321'),
    case('opaque_low_pointer_passed_unchanged', pointer='0x1', result=0x80020016),
    case('opaque_high_bit_pointer_passed_unchanged', pointer='0x8000000000000000'),
    case('opaque_all_bits_pointer_passed_unchanged', pointer='0xffffffffffffffff', result=1),
]


def main():
    """Write the constructed fixtures into the caller-selected private directory."""
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
