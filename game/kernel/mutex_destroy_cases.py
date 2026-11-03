#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the mutex-destruction wrapper at 0x0111ac80.

These exercise import-result truncation, the low-byte storage flag, release ordering,
pointer width and untouched canaries. System calls and allocator calls are scripted;
the fixtures are constructed inputs, never genuine gameplay recordings.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x0111ac80'
CONTEXT_POINTER = '0x056d6ae8'
RELEASE_STORAGE = '0x011ab3a0'
HANDLE = 0xfedcba9876543210


def case(cid, flag=0, result=0, handle=HANDLE, write=True, null_context=False,
         pointer='buf:mutex+0x8', release_result=0):
    """Build a bounded fixture with independently canaried mutex storage and allocator state."""
    fixture = {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': pointer, 'rsi': hex(flag & ((1 << 64) - 1))},
        'buffers': {
            'mutex': {'size': 24, 'bytes': ('a5' * 8) + struct.pack('<Q', handle).hex() + ('5a' * 8)},
            'context': {'size': 16},
            'allocator': {'size': 16, 'bytes': 'b6' * 16},
        },
        'memory': [
            {'addr': CONTEXT_POINTER, 'bytes': '00' * 8} if null_context
            else {'addr': CONTEXT_POINTER, 'pointer': 'context'},
            {'addr': 'buf:context+0x8', 'pointer': 'allocator'},
        ],
        'imports': [{'name': 'scePthreadMutexDestroy', 'argc': 1, 'ret': hex(result & ((1 << 64) - 1))}],
        'stubs': [],
    }
    if write:
        fixture['imports'][0]['writes'] = [{'arg': 0, 'offset': 0, 'size': 8, 'bytes': '00' * 8}]
    if result & 0xffffffff == 0 and flag & 0xff == 0:
        fixture['stubs'].append({
            'address': RELEASE_STORAGE, 'argc': 4, 'ret': hex(release_result & ((1 << 64) - 1)),
            'writes': [{'arg': 0, 'offset': 0, 'bytes': 'c7' * 4}],
        })
    return fixture


CASES = [
    case('dynamic_success'),
    case('static_success', flag=1, null_context=True),
    case('static_nonboolean_flag', flag=2, null_context=True),
    case('static_low_byte_max', flag=0xff, null_context=True),
    case('dynamic_upper_byte_only', flag=0x100),
    case('static_upper_and_low_bytes', flag=0x101, null_context=True),
    case('dynamic_high_register_bits_only', flag=0xffffffff00000000),
    case('static_all_flag_bits', flag=(1 << 64) - 1, null_context=True),
    case('positive_kernel_error', result=1, null_context=True),
    case('negative_kernel_error', result=0x80020016, null_context=True),
    case('error_keeps_original_handle', result=0x80020016, write=False, null_context=True),
    case('static_kernel_error', flag=1, result=-1, null_context=True),
    case('only_low_result_word_matters', result=1 << 32),
    case('release_result_ignored', release_result=0x80020016),
    case('null_pointer_error', result=0x80020016, write=False, null_context=True, pointer='0x0'),
    case('opaque_high_pointer_passed_unchanged', flag=1, write=False, null_context=True,
         pointer='0x1234567887654321'),
    case('zero_initial_handle', handle=0),
]


def main():
    """Write only constructed fixtures to the caller-selected private output directory."""
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
