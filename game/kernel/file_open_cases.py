#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored open/fstat wire inputs; no real kernel implementation is modeled.

Only the fstat formal output can receive callback writes. It is a borrowed,
16-aligned120-byte stack extent; its incoming contents are never captured. Every
scripted success supplies the four consumed bytes at+0x48. The116 unread bytes
have no assumed layout or initialization. Guard-failure/nonreturning behavior and
successful underwrites remain outside this returning fixture set.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x0111a620'
OPEN = '0x02fbf3f8'
FSTAT = '0x02fbfe48'
GUARD_SLOT = '0x057e3be0'
MASK64 = (1 << 64) - 1
STATUS_BYTES = 120
WORD_OFFSET = 0x48
PATH_BYTES = 64
OUTPUT_BYTES = 32
GUARD_BYTES = 32
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported',
    'evidence': ['Pinned original129-byte body and authenticated open/fstat/guest-guard relocations',
                 'Opaque120-byte stack extent,16-aligned start, sole supplied DWORD+0x48 consumed on success',
                 'Captured guest guard pointer/pointee; ordered4then8 publication before same-pointee check'],
    'stack_alignment': 16,
    'arguments': [
        {'register': 'rdi', 'type': 'const char* path wire value', 'direction': 'input'},
        {'register': 'rsi', 'type': 'uintptr_t ignored incoming register', 'direction': 'input'},
        {'register': 'rdx', 'type': 'uintptr_t ignored incoming register', 'direction': 'input'},
        {'register': 'rcx', 'type': 'void* unaligned-capable4-byte output', 'direction': 'output'},
        {'register': 'r8', 'type': 'void* unaligned-capable8-byte descriptor output', 'direction': 'output'},
    ],
    'returns': {'type': 'int32_t'},
    'return_notes': 'Zero success,0x17 open error,0x21 status error; guest guard failure is nonreturning',
    'input_regions': ['path', 'word_out', 'descriptor_out', 'guard'],
    'writable_regions': ['word_out', 'descriptor_out', 'path', 'guard'],
    'writable_guest_pointer_slots': [GUARD_SLOT],
    'guest_slot_write_contract': 'Only the explicit8-byte R8 output-alias case writes GUARD_SLOT; callbacks never write it',
    'execution_limits': {'timeout_seconds': 5},
    'scope': 'Returning synthetic paths; no failure-import substitute or fstat-success underwrite',
}


def word32(value):
    return struct.pack('<I', value & 0xffffffff)


def word64(value):
    return struct.pack('<Q', value & MASK64)


def case(cid, opened=7, status=0, selected=0x12345678, calls_status=True,
         word_out='buf:word_out+0x8', descriptor_out='buf:descriptor_out+0x8',
         incoming=None, path_pointer=None, status_word=True, extras=(), guard=0x8f3a59c71260b4de):
    """Supply bounded operation inputs without computing expected wrapper outputs."""
    path = b'\xa5' * 8 + b'owned-file.bin\0'
    args = {'rdi': path_pointer if path_pointer is not None else 'buf:path+0x8',
            'rsi': '0x0', 'rdx': '0x0', 'rcx': word_out, 'r8': descriptor_out}
    if incoming:
        args.update(incoming)
    opened_call = {'address': OPEN, 'argc': 3, 'ret': hex(opened & MASK64)}
    if path_pointer is None:
        opened_call['argmem'] = [{'arg': 0, 'size': len(b'owned-file.bin\0')}]
    operations = [opened_call]
    if calls_status:
        status_call = {'address': FSTAT, 'argc': 2, 'ret': hex(status & MASK64)}
        writes = list(extras)
        if status_word:
            writes.append({'arg': 1, 'offset': WORD_OFFSET, 'bytes': word32(selected).hex()})
        if writes:
            status_call['writes'] = writes
        operations.append(status_call)
    fixture = {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32', 'args': args,
        'buffers': {
            'path': {'size': PATH_BYTES, 'bytes': (path + b'\x5a' * (PATH_BYTES - len(path))).hex()},
            'word_out': {'size': OUTPUT_BYTES, 'bytes': 'c7' * OUTPUT_BYTES},
            'descriptor_out': {'size': OUTPUT_BYTES, 'bytes': 'e5' * OUTPUT_BYTES},
            'guard': {'size': GUARD_BYTES, 'bytes': (b'\x6b' * 8 + word64(guard) + b'\x9d' * 16).hex()},
        },
        'memory': [{'addr': GUARD_SLOT, 'pointer': 'guard+0x8'}],
        'imports': [], 'stubs': operations,
    }
    validate_contract(fixture)
    return fixture


def validate_contract(fixture):
    """Validate formal footprints and initialized-word supply, not output arithmetic."""
    operations = fixture['stubs']
    if len(operations) not in (1, 2) or operations[0]['address'] != OPEN:
        raise ValueError('one open and optionally one status callback are declared')
    if operations[0].get('writes'):
        raise ValueError('the path is a read-only callback argument')
    if len(operations) == 2:
        operation = operations[1]
        if operation['address'] != FSTAT or operation['argc'] != 2 or operation.get('argmem'):
            raise ValueError('status output has two formal arguments and no incoming capture')
        supplied = set()
        for write in operation.get('writes', []):
            data = bytes.fromhex(write['bytes'])
            start = write.get('offset', 0)
            if write['arg'] != 1 or start < 0 or start + len(data) > STATUS_BYTES:
                raise ValueError('status callback write exceeds its120-byte formal output')
            supplied.update(range(start, start + len(data)))
        if int(operation['ret'], 0) & 0xffffffff == 0 and not set(range(WORD_OFFSET, WORD_OFFSET + 4)) <= supplied:
            raise ValueError('successful status must supply its consumed word')
    for register, size in [('rcx', 4), ('r8', 8)]:
        value = fixture['args'][register]
        if value.startswith('buf:'):
            name, _, offset = value[4:].partition('+')
            start = int(offset or '0', 0)
            if start < 0 or start + size > fixture['buffers'][name]['size']:
                raise ValueError('wrapper output exceeds its declared buffer')
        elif value.startswith('guest:'):
            if register != 'r8' or value != 'guest:' + GUARD_SLOT:
                raise ValueError('only the explicit8-byte descriptor output can alias the guest guard slot')


CASES = [
    case('ordinary_open_and_status'),
    case('zero_descriptor_still_queries', opened=0),
    case('largest_nonnegative_descriptor', opened=0x7fffffff),
    case('open_upper_word_ignored_zero', opened=0xffffffff00000000),
    case('open_high_sign_low_positive', opened=0x8000000000000007),
    case('open_high_word_low_max_positive', opened=0xfedcba987fffffff),
    case('open_low_sign_bit_is_error', opened=0x80000000, calls_status=False),
    case('open_positive_high_word_low_minus_one', opened=0x12345678ffffffff, calls_status=False),
    case('open_minus_one', opened=MASK64, calls_status=False),
    case('open_error_keeps_valid_outputs', opened=0x80000005, calls_status=False),
    case('open_error_accepts_null_outputs', opened=MASK64, calls_status=False,
         word_out='0x0', descriptor_out='0x0'),
    case('open_error_accepts_opaque_outputs', opened=MASK64, calls_status=False,
         word_out='0xfedcba9876543210', descriptor_out='0x1234567887654321'),
    case('status_positive_one_with_supplied_word', status=1),
    case('status_low_negative_with_supplied_word', status=0x80000000),
    case('status_low_minus_one_with_supplied_word', status=MASK64),
    case('status_upper_word_ignored_zero', status=0x100000000),
    case('status_high_sign_low_zero', status=0x8000000000000000),
    case('status_high_word_low_one', status=0x1234567800000001),
    case('status_error_accepts_null_outputs', status=1, status_word=False,
         word_out='0x0', descriptor_out='0x0'),
    case('status_error_accepts_opaque_outputs', status=MASK64, status_word=False,
         word_out='0xfedcba9876543210', descriptor_out='0x1234567887654321'),
    case('status_error_needs_no_initialized_word', status=1, status_word=False),
    case('selected_word_zero', selected=0),
    case('selected_word_all_bits', selected=0xffffffff),
    case('selected_word_high_bit', selected=0x80000000),
    case('incoming_flags_and_mode_ignored', incoming={'rsi': '0xfedcba9876543210', 'rdx': '0x1234567887654321'}),
    case('unaligned_word_output', word_out='buf:word_out+0x9'),
    case('unaligned_descriptor_output', descriptor_out='buf:descriptor_out+0xb'),
    case('both_outputs_unaligned', word_out='buf:word_out+0x9', descriptor_out='buf:descriptor_out+0xb'),
    case('exact_output_overlap', word_out='buf:word_out+0x8', descriptor_out='buf:word_out+0x8'),
    case('descriptor_overlaps_word_plus_two', word_out='buf:word_out+0x8', descriptor_out='buf:word_out+0xa'),
    case('descriptor_overlaps_word_minus_two', word_out='buf:word_out+0xa', descriptor_out='buf:word_out+0x8'),
    case('word_output_aliases_path', word_out='buf:path+0x8'),
    case('descriptor_output_aliases_path', descriptor_out='buf:path+0x8'),
    case('both_outputs_alias_path', word_out='buf:path+0x8', descriptor_out='buf:path+0x8'),
    case('status_ignored_prefix_and_tail_supplied', extras=(
        {'arg': 1, 'offset': 0, 'bytes': '1020304050607080'},
        {'arg': 1, 'offset': 112, 'bytes': '90a0b0c0d0e0f000'})),
    case('complete_status_extent_supplied', extras=({'arg': 1, 'bytes': bytes(range(STATUS_BYTES)).hex()},)),
    case('status_error_complete_extent_supplied', status=1, status_word=False,
         extras=({'arg': 1, 'bytes': bytes(range(STATUS_BYTES)).hex()},)),
    case('guard_zero_is_valid', guard=0),
    case('guard_all_bits_are_opaque', guard=MASK64),
    case('word_output_aliases_guard_unchanged', selected=0x1260b4de, word_out='buf:guard+0x8'),
    case('descriptor_output_aliases_guard_unchanged', opened=7, guard=7, descriptor_out='buf:guard+0x8'),
    case('both_outputs_temporarily_change_then_restore_guard', opened=7, guard=7,
         word_out='buf:guard+0x8', descriptor_out='buf:guard+0x8'),
    case('descriptor_output_replaces_guest_guard_pointer_slot', descriptor_out='guest:' + GUARD_SLOT),
    case('null_path_is_forwarded_on_open_error', opened=MASK64, calls_status=False, path_pointer='0x0'),
    case('opaque_path_is_forwarded_on_open_error', opened=MASK64, calls_status=False, path_pointer='0x1234567887654321'),
]


def main():
    """Write only authored inputs and ABI into a new destination."""
    if len(sys.argv) != 2:
        raise SystemExit(f'usage: {Path(__file__).name} OUT_DIR')
    output = Path(sys.argv[1]); output.mkdir(parents=True, exist_ok=False)
    for fixture in CASES:
        with (output / (fixture['id'] + '.json')).open('x') as stream:
            stream.write(json.dumps(fixture, indent=1) + '\n')
    with (output / 'abi.json').open('x') as stream:
        stream.write(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(CASES)} authored inputs written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
