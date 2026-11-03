#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Constructed edge fixtures for the recursive-mutex constructor at 0x0111abb0.

The original syscall thunks are scripted as function stubs so argmem can inspect each
opaque attribute-variable input, in addition to recording its arguments and writes.
This covers only the wrapper under a successful attribute-init output contract; it
does not implement allocation or system mutexes and contains no gameplay recordings.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x0111abb0'
ALLOCATE = '0x011aab50'
RELEASE = '0x011ab3a0'
ATTR_INIT = '0x02fbfea8'
ATTR_SETTYPE = '0x02fbfeb8'
MUTEX_INIT = '0x02fbfec8'
ATTR_DESTROY = '0x02fbfed8'
CONTEXT_POINTER = '0x056d6ae8'
STATIC_STORAGE = '0x0586d470'
ATTRIBUTE_HANDLE = 0x123456789abcdef0
MUTEX_HANDLE = 0xfedcba9876543210
STAGES = ('attr_init', 'attr_settype', 'mutex_init', 'attr_destroy')


def word(value):
    """Encode one complete opaque 64-bit machine value."""
    return struct.pack('<Q', value & ((1 << 64) - 1)).hex()


def case(cid, flag=0, allocation=True, failure=None, result=0x80020016,
         high_result_stage=None, changed_attributes=False, output='buf:out+0x8'):
    """Script constructor stages without assuming any prior bytes in its stack locals."""
    use_static = flag & 0xff != 0
    storage = 'guest:' + STATIC_STORAGE if use_static else 'buf:storage+0x8'
    fixture = {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': output, 'rsi': hex(flag & ((1 << 64) - 1))},
        'buffers': {
            'out': {'size': 24, 'bytes': 'a5' * 24},
            'storage': {'size': 24, 'bytes': ('b6' * 8) + word(0x1122334455667788) + ('c7' * 8)},
            'context': {'size': 16},
            'allocator': {'size': 16, 'bytes': 'd8' * 16},
        },
        'memory': [
            {'addr': CONTEXT_POINTER, 'bytes': '00' * 8} if use_static
            else {'addr': CONTEXT_POINTER, 'pointer': 'context'},
            {'addr': 'buf:context+0x8', 'pointer': 'allocator'},
            {'addr': '0x0586d468', 'bytes': 'e9' * 8},
            {'addr': STATIC_STORAGE, 'bytes': word(0x1122334455667788)},
            {'addr': '0x0586d478', 'bytes': 'fa' * 8},
        ],
        'imports': [],
        # An erroneous cleanup becomes an extra observed call, rather than executing the
        # allocator's original release implementation on a synthetic allocator object.
        'stubs': [{'address': RELEASE, 'argc': 4, 'ret': 0}],
    }
    if not use_static:
        fixture['stubs'].append({
            'address': ALLOCATE, 'argc': 6, 'ret': storage if allocation else '0x0',
            'argmem': [{'arg': 0, 'size': 4}],
            'writes': [{'arg': 0, 'offset': 0, 'bytes': '6b' * 4}],
        })
        if not allocation:
            return fixture

    attribute = ATTRIBUTE_HANDLE
    operations = [
        ('attr_init', {'address': ATTR_INIT, 'argc': 1,
                       'writes': [{'arg': 0, 'bytes': word(attribute)}]}),
        ('attr_settype', {'address': ATTR_SETTYPE, 'argc': 2,
                          'argmem': [{'arg': 0, 'size': 8}]}),
        ('mutex_init', {'address': MUTEX_INIT, 'argc': 3,
                        'argmem': [{'arg': 0, 'size': 8}, {'arg': 1, 'size': 8}],
                        'writes': [{'arg': 0, 'bytes': word(MUTEX_HANDLE)}]}),
        ('attr_destroy', {'address': ATTR_DESTROY, 'argc': 1,
                          'argmem': [{'arg': 0, 'size': 8}],
                          'writes': [{'arg': 0, 'bytes': word(0)}]}),
    ]
    if changed_attributes:
        # All stages receive the same attribute variable, rather than a stale copied handle.
        operations[1][1]['writes'] = [{'arg': 0, 'bytes': word(0xfedcba9876543210)}]
        operations[2][1]['writes'].append({'arg': 1, 'bytes': word(0x8000000000000001)})
    for stage, operation in operations:
        returned = result if failure == stage else (1 << 32 if high_result_stage == stage else 0)
        operation['ret'] = hex(returned & ((1 << 64) - 1))
        fixture['stubs'].append(operation)
        if returned & 0xffffffff:
            break
    return fixture


CASES = [
    case('dynamic_success'),
    case('static_success', flag=1),
    case('static_nonboolean_flag', flag=2),
    case('static_low_byte_max', flag=0xff),
    case('dynamic_upper_byte_only', flag=0x100),
    case('static_upper_and_low_bytes', flag=0x101),
    case('dynamic_high_register_bits_only', flag=0xffffffff00000000),
    case('static_all_flag_bits', flag=(1 << 64) - 1),
    case('allocation_failure', allocation=False),
    case('null_output_allocation_failure', allocation=False, output='0x0'),
    case('null_output_kernel_error', flag=1, failure='attr_init', output='0x0'),
    case('publication_aliases_dynamic_storage', output='buf:storage+0x8'),
    case('null_output_final_attribute_error', flag=1, failure='attr_destroy', output='0x0'),
    case('opaque_attribute_changes_preserved', changed_attributes=True),
]
for stage in STAGES:
    CASES.extend([
        case('dynamic_' + stage + '_error', failure=stage),
        case('static_' + stage + '_positive_error', flag=1, failure=stage, result=1),
        case(stage + '_upper_return_bits_ignored', high_result_stage=stage),
    ])


def main():
    """Write bounded constructed cases into the caller-selected private directory."""
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
