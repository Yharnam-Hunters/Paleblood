#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored seek-wrapper wire cases; no real kernel or gameplay results are embedded.

The kernel descriptor is an integer wire value, never a writable pointer. The only
scripted effect is the callee return; all three full GP arguments are observed.
"""
import json
from pathlib import Path
import sys

ADDRESS = '0x0111a720'
KERNEL_SEEK = '0x02fbf428'
MASK64 = (1 << 64) - 1
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported',
    'evidence': ['Pinned original 22-byte body at 0x0111a720; sceKernelLseek NID and JUMP_SLOT',
                 'Tail caller 0x011a4280 forwards a qword from object+0x210; ESI is zero-extended'],
    'stack_alignment': 16,
    'arguments': [
        {'register': 'rdi', 'type': 'uintptr_t opaque descriptor wire value', 'direction': 'input'},
        {'register': 'rsi', 'type': 'uint32_t absolute offset; upper32 ignored', 'direction': 'input'},
        # Initial noise registers are observed inputs, not additional logical parameters.
        {'register': 'rdx', 'type': 'uint64_t ignored incoming caller-saved register', 'direction': 'input'},
        {'register': 'rcx', 'type': 'uint64_t ignored incoming caller-saved register', 'direction': 'input'},
        {'register': 'r8', 'type': 'uint64_t ignored incoming caller-saved register', 'direction': 'input'},
        {'register': 'r9', 'type': 'uint64_t ignored incoming caller-saved register', 'direction': 'input'},
    ],
    'returns': {'type': 'int32_t: 0 or 0x13 from imported full64 result sign'},
    'input_regions': [], 'writable_regions': [],
    'execution_limits': {'timeout_seconds': 5},
}


def case(cid, descriptor=0x1234567887654321, offset=0x12345678, result=0, incoming=None):
    """Observe the original's exact argument widths and signed 64-bit result test."""
    args = {'rdi': hex(descriptor & MASK64), 'rsi': hex(offset & MASK64),
            'rdx': '0x0', 'rcx': '0x0', 'r8': '0x0', 'r9': '0x0'}
    if incoming:
        args.update(incoming)
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': args, 'buffers': {}, 'imports': [],
        'stubs': [{'address': KERNEL_SEEK, 'argc': 3, 'ret': hex(result & MASK64)}],
    }


CASES = [
    case('ordinary_success'),
    case('zero_descriptor_still_calls', descriptor=0),
    case('descriptor_one', descriptor=1),
    case('descriptor_high_word', descriptor=0xfedcba9800000000),
    case('descriptor_high_sign_bit', descriptor=1 << 63),
    case('descriptor_all_bits', descriptor=MASK64),
    case('descriptor_low_word', descriptor=0xffffffff),
    case('descriptor_mixed_words', descriptor=0x8000000012345678),
    case('offset_zero', offset=0),
    case('offset_one', offset=1),
    case('offset_signed_max', offset=0x7fffffff),
    case('offset_high_bit_zero_extends', offset=0x80000000),
    case('offset_all_low_bits_zero_extends', offset=0xffffffff),
    case('offset_upper_word_ignored', offset=0xabcdef1200000000),
    case('offset_mixed_upper_word_ignored', offset=0x1234567887654321),
    case('offset_all_bits', offset=MASK64),
    case('positive_result_one', result=1),
    case('positive_result_low_sign_bit', result=0x80000000),
    case('positive_result_all_low_bits', result=0xffffffff),
    case('positive_result_upper_word', result=1 << 32),
    case('positive_result_int64_max', result=(1 << 63) - 1),
    case('negative_result_int64_min', result=1 << 63),
    case('negative_result_high_word_low_zero', result=0xffffffff00000000),
    case('negative_result_minus_one', result=MASK64),
    case('negative_result_mixed_low_positive', result=0xabcdef1200000001),
    case('zero_descriptor_negative_result', descriptor=0, result=MASK64),
    case('discarded_incoming_argument_registers', incoming={
        'rdx': '0xffffffffffffffff', 'rcx': '0x1122334455667788',
        'r8': '0x8000000000000000', 'r9': '0xdeadbeefcafebabe'}),
    case('all_width_boundaries_together', descriptor=MASK64, offset=MASK64,
         result=0xffffffff00000000, incoming={'rdx': '0x12345678ffffffff'}),
]


def main():
    """Write only authored cases and ABI into a fresh caller-selected directory."""
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
