#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate deterministic original-code edge fixtures for event_flag_write."""
import json
import os
import struct
import sys


ADDRESS = '0x017cfcc0'
GROUP_SIZE = 16
ABI = {
    'calling_convention': 'sysv_amd64',
    'confidence': 'supported',
    'evidence': [
        'symbols/verification.csv:event_flag_write edge result (14 original-instruction cases)',
        'game/event/flag_write.cpp: replacement prototype and argument widths',
    ],
    'stack_alignment': 16,
    'arguments': [
        {'register': 'rdi', 'type': 'FlagStore *', 'direction': 'inout'},
        {'register': 'rsi', 'type': 'uint32_t', 'direction': 'input'},
        {'register': 'rdx', 'type': 'uint32_t', 'direction': 'input'},
    ],
    'returns': {'type': 'void'},
    'input_regions': ['store', 'header', 'root', 'terminal'],
    'writable_regions': ['bits', 'pages'],
    'execution_limits': {'timeout_seconds': 20, 'unscripted_call_limit': 1000},
}


def u32(value):
    return struct.pack('<I', value & 0xffffffff).hex()


def u64(value):
    return struct.pack('<Q', value & 0xffffffffffffffff).hex()


def make_case(case_id, flag_id, enabled, *, root_group=5, left_group=None,
              right_group=None, kind=2, storage_index=0, initial=b'\0' * 8, stride=8):
    case = {
        'schema': 1,
        'address': ADDRESS,
        'id': case_id,
        'abi': ABI,
        'returns': 'void',
        'args': {'rdi': 'buf:store', 'rsi': hex(flag_id), 'rdx': hex(enabled)},
        'buffers': {
            'store': {'size': 0x40},
            'header': {'size': 0x20},
            'root': {'size': 0x38},
            'terminal': {'size': 0x20},
            'bits': {'size': 32, 'bytes': initial.hex()},
            'pages': {'size': 64},
        },
        'memory': [
            {'addr': 'buf:store+0x1c', 'bytes': u32(GROUP_SIZE)},
            {'addr': 'buf:store+0x20', 'bytes': u32(stride)},
            {'addr': 'buf:store+0x28', 'pointer': 'pages'},
            {'addr': 'buf:store+0x38', 'pointer': 'header'},
            {'addr': 'buf:header+8', 'pointer': 'root'},
            {'addr': 'buf:terminal+0x19', 'bytes': '01'},
            {'addr': 'buf:root+0x19', 'bytes': '00'},
            {'addr': 'buf:root+0x20', 'bytes': u32(root_group)},
            {'addr': 'buf:root+0x28', 'bytes': u32(kind)},
            {'addr': 'buf:root+0x30', 'bytes': u64(storage_index) if kind == 1 else '00' * 8},
        ],
    }
    mem = case['memory']
    if kind == 2:
        mem.append({'addr': 'buf:root+0x30', 'pointer': 'bits'})
    # Both children terminate unless a case asks to visit a second keyed node.
    mem += [
        {'addr': 'buf:root', 'pointer': 'terminal'},
        {'addr': 'buf:root+0x10', 'pointer': 'terminal'},
    ]
    if left_group is not None:
        case['buffers']['left'] = {'size': 0x38}
        mem += [
            {'addr': 'buf:root', 'pointer': 'left'},
            {'addr': 'buf:left', 'pointer': 'terminal'},
            {'addr': 'buf:left+0x10', 'pointer': 'terminal'},
            {'addr': 'buf:left+0x19', 'bytes': '00'},
            {'addr': 'buf:left+0x20', 'bytes': u32(left_group)},
            {'addr': 'buf:left+0x28', 'bytes': u32(2)},
            {'addr': 'buf:left+0x30', 'pointer': 'bits'},
        ]
    if right_group is not None:
        case['buffers']['right'] = {'size': 0x38}
        mem += [
            {'addr': 'buf:root+0x10', 'pointer': 'right'},
            {'addr': 'buf:right', 'pointer': 'terminal'},
            {'addr': 'buf:right+0x10', 'pointer': 'terminal'},
            {'addr': 'buf:right+0x19', 'bytes': '00'},
            {'addr': 'buf:right+0x20', 'bytes': u32(right_group)},
            {'addr': 'buf:right+0x28', 'bytes': u32(2)},
            {'addr': 'buf:right+0x30', 'pointer': 'bits'},
        ]
    return case


CASES = [
    make_case('flag_id_zero', 0, 1, root_group=0),
    make_case('set_bit_zero', 80, 1),
    make_case('set_nonzero_boolean', 85, 0x100),
    make_case('clear_bit', 85, 0, initial=b'\xff' * 8),
    make_case('last_bit_of_byte', 87, 1),
    make_case('first_bit_of_second_byte', 95, 1),
    make_case('group_not_present_below', 79, 1),
    make_case('group_not_present_above', 96, 1),
    make_case('find_left_child', 48, 1, left_group=3),
    make_case('find_right_child', 112, 1, right_group=7),
    make_case('strided_storage', 80, 1, kind=1, storage_index=2),
    make_case('unsupported_storage_kind', 80, 1, kind=0),
    make_case('null_direct_storage', 80, 1, kind=2, initial=b'\0' * 8),
    make_case('maximum_flag_id', 0xffffffff, 1, root_group=0x0fffffff),
]


def main():
    if len(sys.argv) != 2:
        print(f'usage: {sys.argv[0]} OUT_DIR', file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for case in CASES:
        if case['id'] == 'null_direct_storage':
            case['memory'] = [m for m in case['memory'] if not (m['addr'] == 'buf:root+0x30' and 'pointer' in m)]
        if case['id'] == 'strided_storage':
            case['memory'].append({'addr': 'buf:pages+16', 'bytes': '00' * 32})
        with open(os.path.join(sys.argv[1], case['id'] + '.json'), 'w', encoding='utf-8') as f:
            json.dump(case, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
