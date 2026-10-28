#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic read-wrapper inputs: bounded formal buffer effects and exact DWORD publication.

Descriptors remain opaque numeric values. Large/contradictory return words and
opaque zero-count buffer pointers are forwarding probes, not real syscall claims.
"""
import json
from pathlib import Path
import sys

ADDRESS = '0x0111a6e0'
KERNEL_READ = '0x02fbf408'
MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1
DATA_BYTES = 64
OUTPUT_BYTES = 24
PAYLOAD = bytes.fromhex('1032547698badcfe1122334455667788')
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported',
    'evidence': ['Pinned original 60-byte body; authenticated sceKernelRead JUMP_SLOT/NID',
                 'EDX zero-extension, full RAX sign/comparison, and post-call scalar DWORD output store'],
    'stack_alignment': 16,
    'arguments': [
        {'register': 'rdi', 'type': 'uintptr_t opaque descriptor wire value', 'direction': 'input'},
        {'register': 'rsi', 'type': 'void* formal data output wire value', 'direction': 'output'},
        {'register': 'rdx', 'type': 'uint32_t requested bytes; upper32 ignored', 'direction': 'input'},
        {'register': 'rcx', 'type': 'void* unaligned-capable DWORD count output', 'direction': 'output'},
    ],
    'returns': {'type': 'int32_t zero, 0x13 error, or 0x16 incomplete count'},
    'input_regions': ['data', 'out'], 'writable_regions': ['data', 'out'],
    'execution_limits': {'timeout_seconds': 5},
}


def case(cid, descriptor=0x1234567887654321, count=16, result=16,
         write_bytes=0, output='buf:out+0x8', buffer_pointer=None):
    """Only formal data arg1 can receive scripted writes; wrapper output stays mapped."""
    requested = count & MASK32
    operation = {'address': KERNEL_READ, 'argc': 3, 'ret': hex(result & MASK64)}
    if buffer_pointer is None and requested:
        operation['argmem'] = [{'arg': 1, 'size': min(requested, len(PAYLOAD))}]
    if write_bytes:
        assert buffer_pointer is None and 0 < write_bytes <= min(requested, len(PAYLOAD))
        operation['writes'] = [{'arg': 1, 'bytes': PAYLOAD[:write_bytes].hex()}]
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': hex(descriptor & MASK64),
                 'rsi': buffer_pointer if buffer_pointer is not None else 'buf:data+0x8',
                 'rdx': hex(count & MASK64), 'rcx': output},
        'buffers': {'data': {'size': DATA_BYTES, 'bytes': 'c7' * DATA_BYTES},
                    'out': {'size': OUTPUT_BYTES, 'bytes': 'e5' * OUTPUT_BYTES}},
        'imports': [], 'stubs': [operation],
    }


CASES = [
    case('ordinary_exact'),
    case('zero_descriptor_still_calls', descriptor=0),
    case('high_word_descriptor', descriptor=0xfedcba9800000000),
    case('all_descriptor_bits', descriptor=MASK64),
    case('zero_count_exact', count=0, result=0),
    case('one_count_exact', count=1, result=1),
    case('high_count_bit_zero_extends', count=0x80000000, result=0),
    case('all_low_count_bits_zero_extend', count=MASK32, result=0),
    case('count_upper_word_ignored', count=0xabcdef1200000010),
    case('all_count_bits_upper_ignored', count=MASK64, result=MASK32),
    case('zero_result_is_incomplete', result=0),
    case('partial_result', result=7),
    case('oversized_positive_result', result=17),
    case('positive_result_low_sign_bit', result=0x80000000),
    case('positive_high_word_low_count_equal', result=0x100000010),
    case('positive_int64_max', result=(1 << 63) - 1),
    case('negative_int64_min', result=1 << 63),
    case('negative_high_word_low_zero', result=0xffffffff00000000),
    case('negative_minus_one', result=MASK64),
    case('negative_mixed_low_count_equal', result=0x8000000000000010),
    case('kernel_buffer_write_exact', write_bytes=16),
    case('kernel_buffer_write_partial', result=7, write_bytes=7),
    case('kernel_buffer_write_then_error', result=MASK64, write_bytes=16),
    case('output_aliases_buffer', output='buf:data+0x10', write_bytes=16),
    case('output_aliases_buffer_start', output='buf:data+0x8', write_bytes=16),
    case('unaligned_count_output', output='buf:out+0x9', result=7),
    case('opaque_buffer_zero_count_forwarded', count=0, result=0,
         buffer_pointer='0xfedcba9876543210'),
    case('null_buffer_zero_count_forwarded', count=0, result=0, buffer_pointer='0x0'),
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
