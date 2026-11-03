#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic file-operation inputs with scripted kernel callbacks, bounded outputs, descriptor errors and aliases."""
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
WORD_OFFSET = 72
PATH_BYTES = 64
OUTPUT_BYTES = 32
GUARD_BYTES = 32

def word32(value):
    return struct.pack('<I', value & 4294967295)

def word64(value):
    return struct.pack('<Q', value & MASK64)

def case(cid, opened=7, status=0, selected=305419896, calls_status=True, word_out='buf:word_out+0x8', descriptor_out='buf:descriptor_out+0x8', incoming=None, path_pointer=None, status_word=True, extras=()):
    """Supply bounded operation inputs without computing expected wrapper outputs."""
    path = b'\xa5' * 8 + b'owned-file.bin\x00'
    args = {'rdi': path_pointer if path_pointer is not None else 'buf:path+0x8', 'rsi': '0x0', 'rdx': '0x0', 'rcx': word_out, 'r8': descriptor_out}
    if incoming:
        args.update(incoming)
    opened_call = {'address': OPEN, 'argc': 3, 'ret': hex(opened & MASK64)}
    if path_pointer is None:
        opened_call['argmem'] = [{'arg': 0, 'size': len(b'owned-file.bin\x00')}]
    operations = [opened_call]
    if calls_status:
        status_call = {'address': FSTAT, 'argc': 2, 'ret': hex(status & MASK64)}
        writes = list(extras)
        if status_word:
            writes.append({'arg': 1, 'offset': WORD_OFFSET, 'bytes': word64(selected).hex()})
        if writes:
            status_call['writes'] = writes
        operations.append(status_call)
    fixture = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32', 'args': args, 'buffers': {'path': {'size': PATH_BYTES, 'bytes': (path + b'Z' * (PATH_BYTES - len(path))).hex()}, 'word_out': {'size': OUTPUT_BYTES, 'bytes': 'c7' * OUTPUT_BYTES}, 'descriptor_out': {'size': OUTPUT_BYTES, 'bytes': 'e5' * OUTPUT_BYTES}, 'guard': {'size': GUARD_BYTES, 'bytes': (b'k' * 8 + word64(10320660207614604510) + b'\x9d' * 16).hex()}}, 'memory': [{'addr': GUARD_SLOT, 'pointer': 'guard+0x8'}], 'imports': [], 'stubs': operations}
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
        if int(operation['ret'], 0) & 4294967295 == 0 and (not set(range(WORD_OFFSET, WORD_OFFSET + 8)) <= supplied):
            raise ValueError('successful status must supply the eight-byte file size')
    for register, size in [('rcx', 4), ('r8', 8)]:
        value = fixture['args'][register]
        if value.startswith('buf:'):
            name, _, offset = value[4:].partition('+')
            start = int(offset or '0', 0)
            if start < 0 or start + size > fixture['buffers'][name]['size']:
                raise ValueError('wrapper output exceeds its declared buffer')
CASES = [case('ordinary_open_and_status'), case('zero_descriptor_still_queries', opened=0), case('largest_nonnegative_descriptor', opened=2147483647), case('open_upper_word_ignored_zero', opened=18446744069414584320), case('open_high_sign_low_positive', opened=9223372036854775815), case('open_high_word_low_max_positive', opened=18364758544655319039), case('open_low_sign_bit_is_error', opened=2147483648, calls_status=False), case('open_positive_high_word_low_minus_one', opened=1311768469162688511, calls_status=False), case('open_minus_one', opened=MASK64, calls_status=False), case('open_error_keeps_valid_outputs', opened=2147483653, calls_status=False), case('open_error_accepts_null_outputs', opened=MASK64, calls_status=False, word_out='0x0', descriptor_out='0x0'), case('open_error_accepts_opaque_outputs', opened=MASK64, calls_status=False, word_out='0xfedcba9876543210', descriptor_out='0x1234567887654321'), case('status_positive_one_with_supplied_word', status=1), case('status_low_negative_with_supplied_word', status=2147483648), case('status_low_minus_one_with_supplied_word', status=MASK64), case('status_upper_word_ignored_zero', status=4294967296), case('status_high_sign_low_zero', status=9223372036854775808), case('status_high_word_low_one', status=1311768464867721217), case('status_error_accepts_null_outputs', status=1, status_word=False, word_out='0x0', descriptor_out='0x0'), case('status_error_accepts_opaque_outputs', status=MASK64, status_word=False, word_out='0xfedcba9876543210', descriptor_out='0x1234567887654321'), case('status_error_needs_no_initialized_word', status=1, status_word=False), case('selected_word_zero', selected=0), case('selected_word_all_bits', selected=4294967295), case('selected_word_high_bit', selected=2147483648), case('incoming_flags_and_mode_ignored', incoming={'rsi': '0xfedcba9876543210', 'rdx': '0x1234567887654321'}), case('unaligned_word_output', word_out='buf:word_out+0x9'), case('unaligned_descriptor_output', descriptor_out='buf:descriptor_out+0xb'), case('both_outputs_unaligned', word_out='buf:word_out+0x9', descriptor_out='buf:descriptor_out+0xb'), case('exact_output_overlap', word_out='buf:word_out+0x8', descriptor_out='buf:word_out+0x8'), case('descriptor_overlaps_word_plus_two', word_out='buf:word_out+0x8', descriptor_out='buf:word_out+0xa'), case('descriptor_overlaps_word_minus_two', word_out='buf:word_out+0xa', descriptor_out='buf:word_out+0x8'), case('word_output_aliases_path', word_out='buf:path+0x8'), case('descriptor_output_aliases_path', descriptor_out='buf:path+0x8'), case('both_outputs_alias_path', word_out='buf:path+0x8', descriptor_out='buf:path+0x8'), case('status_ignored_prefix_and_tail_supplied', extras=({'arg': 1, 'offset': 0, 'bytes': '1020304050607080'}, {'arg': 1, 'offset': 112, 'bytes': '90a0b0c0d0e0f000'})), case('complete_status_extent_supplied', extras=({'arg': 1, 'bytes': bytes(range(STATUS_BYTES)).hex()},)), case('status_error_complete_extent_supplied', status=1, status_word=False, extras=({'arg': 1, 'bytes': bytes(range(STATUS_BYTES)).hex()},)), case('null_path_is_forwarded_on_open_error', opened=MASK64, calls_status=False, path_pointer='0x0'), case('opaque_path_is_forwarded_on_open_error', opened=MASK64, calls_status=False, path_pointer='0x1234567887654321')]

def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        raise SystemExit(f'usage: {Path(__file__).name} OUT_DIR')
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        with (output / (fixture['id'] + '.json')).open('w') as stream:
            stream.write(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} authored inputs written to {output}')
    return 0
CASES.extend([case('size_above_32_bits', selected=0x123456789abcdef0),
              case('negative_size_low_word', selected=0xffffffffffffffff),
              case('size_high_sign_low_zero', selected=0x8000000000000000)])

if __name__ == '__main__':
    raise SystemExit(main())
