#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge fixtures for the guarded wide-string initializer at 0x0217ffb0.

An authored allocator lives in writable image scratch after the object/guard. This
lets its info callback inspect or change the allocator singleton through checked
relative accesses, without assuming sandbox or host-stack addresses. The eight-byte
info output is always supplied completely. No gameplay recordings are implied.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = 0x0217ffb0
OBJECT = 0x05aceb90
GUARD = 0x05acebc8
HEAP = 0x05acebd0
ALLOCATOR = 0x05940670
GUARD_GOT = 0x057e3be0
FACTORY = 0x02fa2b10
INFO = 0x00500100
FATAL = 0x024b55b0
REGISTER = 0x02fbe548
HEAP_MESSAGE = 0x04b37dc0
MASK64 = (1 << 64) - 1


def word(value):
    """Encode the complete eight-byte value stored at a fixed guest pointer slot."""
    return struct.pack('<Q', value & MASK64).hex()


def case(cid, guard=0, present=True, flags=0x20, register_result=0,
         info_changes_allocator=False, fatal_changes_allocator=False,
         registration_changes_object=False, registration_changes_guard=False,
         incoming=None, object_byte=0xa5):
    """Expose pre-registration object bytes and the old guard as call observations."""
    fixture = {
        'schema': 1, 'address': f'0x{ADDRESS:08x}', 'id': cid, 'returns': 'void',
        'args': incoming or {},
        'buffers': {
            'guard': {'size': 8, 'bytes': word(0x123456789abcdef0)},
            'heap_vtable': {'size': 0x28},
            'other_heap': {'size': 16, 'bytes': 'c3' * 16},
        },
        'memory': [
            {'addr': hex(GUARD_GOT), 'pointer': 'guard'},
            {'addr': hex(OBJECT), 'bytes': f'{object_byte:02x}' * 0x38},
            {'addr': hex(GUARD), 'bytes': word(guard)},
            {'addr': hex(HEAP), 'pointer': 'heap_vtable'},
            {'addr': hex(HEAP + 8), 'bytes': '6d' * 8},
            {'addr': 'buf:heap_vtable+0x20', 'guest': hex(INFO)},
        ],
        'imports': [], 'stubs': [],
    }
    fixture['memory'].append({'addr': hex(ALLOCATOR), **({'guest': hex(HEAP)} if present else {'bytes': word(0)})})
    if guard & 0xff:
        # Invalid heap/vtable values must never be dereferenced on the initialized path.
        fixture['memory'].append({'addr': hex(ALLOCATOR), 'bytes': word(0xfffffffffffffff0)})
        return fixture

    if not present:
        fixture['stubs'].append({'address': hex(FACTORY), 'argc': 0, 'ret': f'guest:{HEAP:#x}'})
    info = {'address': hex(INFO), 'argc': 3,
            'argmem': [{'arg': 1, 'offset': ALLOCATOR - HEAP, 'size': 8}],
            'writes': [{'arg': 0, 'bytes': f'{flags:02x}' + '7e' * 7}],
            'ret': '0x123456789abcdef0'}
    if info_changes_allocator:
        info['writes'].append({'arg': 1, 'offset': ALLOCATOR - HEAP, 'pointer': 'other_heap'})
    fixture['stubs'].append(info)
    if not flags & 0x20:
        diagnostic = {'address': hex(FATAL), 'argc': 3,
                      'argmem': [{'arg': 2, 'size': len('Tried to create container with incompatible heap.') + 1}],
                      'ret': '0x123456789abcdef0'}
        if fatal_changes_allocator:
            diagnostic['writes'] = [{'arg': 2, 'offset': ALLOCATOR - HEAP_MESSAGE, 'pointer': 'other_heap'}]
        fixture['stubs'].append(diagnostic)
    registration = {'address': hex(REGISTER), 'argc': 3,
                    'argmem': [{'arg': 1, 'size': 0x40}], 'ret': hex(register_result & MASK64)}
    changes = []
    if registration_changes_object:
        changes.append({'arg': 1, 'offset': 8, 'bytes': 'ddee'})
    if registration_changes_guard:
        changes.append({'arg': 1, 'offset': GUARD - OBJECT, 'bytes': word(0xfedcba9876543210)})
    if changes:
        registration['writes'] = changes
    fixture['stubs'].append(registration)
    return fixture


CASES = [
    case('existing_allocator'),
    case('factory_publishes_before_info', present=False),
    case('info_flags_other_bits', flags=0xff),
    case('info_flags_only_valid_bit', flags=0x20),
    case('invalid_heap_reports_and_continues', flags=0),
    case('invalid_heap_unrelated_bits', flags=0xdf),
    case('factory_invalid_heap_still_registers', present=False, flags=0),
    case('info_changes_singleton_retains_captured_allocator', info_changes_allocator=True),
    case('factory_info_changes_singleton_retains_return', present=False, info_changes_allocator=True),
    case('fatal_changes_singleton_retains_captured_allocator', flags=0, fatal_changes_allocator=True),
    case('info_and_fatal_change_singleton', flags=0, info_changes_allocator=True, fatal_changes_allocator=True),
    case('registration_error_ignored', register_result=0xffffffff),
    case('registration_high_bits_ignored', register_result=0x1234567800000000),
    case('registration_nonzero_ignored', register_result=1),
    case('registration_mutates_object_after_initialization', registration_changes_object=True),
    case('registration_mutates_guard_before_final_publication', registration_changes_guard=True),
    case('only_terminator_and_owns_bytes_changed', object_byte=0x7b),
    case('zero_object_still_initializes_capacity', object_byte=0),
    case('all_object_bits_preserved_outside_fields', object_byte=0xff),
    case('input_registers_do_not_supply_arguments', incoming={
        'rdi': '0xffffffffffffffff', 'rsi': '0x1234567800000002',
        'rdx': '0x8000000000000001', 'rcx': '0x100000000', 'r8': '0x42', 'r9': '0x55'}),
]
for value in (1, 2, 0x80, 0xff, 0x10001, 0xffffffffffffffff):
    CASES.append(case(f'guard_low_byte_{value:x}_skips_all', guard=value, present=False))
for value in (0x100, 0x8000000000000000, 0xffffffffffffff00, 0x123456789abcde00):
    CASES.append(case(f'zero_guard_byte_with_upper_bits_{value:x}', guard=value))


def main():
    """Write only authored fixture JSON to the requested private directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (directory / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {directory}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
