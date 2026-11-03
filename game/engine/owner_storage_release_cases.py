#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic inputs for owner storage release; allocator callbacks may change owner fields before returning."""
import json
from pathlib import Path
import struct
import sys
ADDRESS = '0x0132c590'
FREE = '0x00500300'
OWNER_EXTENT = 376
ALLOCATOR_FIELD = 8
STORAGE_FIELD = 368
FREE_SLOT = 112
MASK64 = (1 << 64) - 1

def word(value):
    return struct.pack('<Q', value & MASK64).hex()

def preset(memory, address, value):
    if value.startswith('buf:'):
        memory.append({'addr': address, 'pointer': value[4:]})
    else:
        memory.append({'addr': address, 'bytes': word(int(value, 0))})

def case(cid, old='buf:old_storage+0x8', self_allocator=False, owner_offset=16, allocator_offset=8, vtable_offset=0, invalid_allocator=None, invalid_vtable=False, change_storage=False, change_allocator=False, change_vptr=False, change_old_bytes=False, free_result=0, pattern=165, old_pattern=195, incoming=None):
    """Supply formal views and opaque callback operations without deriving results."""
    owner = f'buf:owner+0x{owner_offset:x}'
    allocator = f'buf:allocator+0x{allocator_offset:x}'
    vtable = f'buf:vtable+0x{vtable_offset:x}'
    other_vtable = f'buf:other_vtable+0x{vtable_offset:x}'
    args = {'rdi': owner, 'rsi': '0x0', 'rdx': '0x0', 'rcx': '0x0', 'r8': '0x0', 'r9': '0x0'}
    args.update(incoming or {})
    buffers = {'owner': {'size': 432, 'bytes': f'{pattern:02x}' * 432}, 'allocator': {'size': 48, 'bytes': '6b' * 48}, 'vtable': {'size': 144, 'bytes': 'e7' * 144}, 'other_vtable': {'size': 144, 'bytes': 'd9' * 144}, 'old_storage': {'size': 48, 'bytes': f'{old_pattern:02x}' * 48}}
    memory = []
    preset(memory, f'buf:owner+0x{owner_offset + STORAGE_FIELD:x}', old)
    preset(memory, f'buf:owner+0x{owner_offset + ALLOCATOR_FIELD:x}', invalid_allocator if invalid_allocator is not None else owner if self_allocator else allocator)
    preset(memory, allocator, vtable)
    if self_allocator:
        preset(memory, owner, '0xfedcba9876543210' if invalid_vtable else vtable)
    for name in ('vtable', 'other_vtable'):
        memory.append({'addr': f'buf:{name}+0x{vtable_offset + FREE_SLOT:x}', 'guest': FREE})
    captures = [{'arg': 0, 'size': 16}, {'arg': 0, 'offset': STORAGE_FIELD, 'size': 8}] if self_allocator else [{'arg': 0, 'size': 8}]
    if old.startswith('buf:old_storage'):
        captures.append({'arg': 1, 'size': 16})
    release = {'address': FREE, 'argc': 2, 'ret': hex(free_result & MASK64), 'argmem': captures}
    writes = []
    if change_vptr:
        writes.append({'arg': 0, 'offset': 0, 'pointer': other_vtable[4:]})
    if change_allocator:
        writes.append({'arg': 0, 'offset': ALLOCATOR_FIELD, 'pointer': allocator[4:]})
    if change_storage:
        writes.append({'arg': 0, 'offset': STORAGE_FIELD, 'bytes': word(8608499333015906986)})
    if change_old_bytes:
        writes.append({'arg': 1, 'offset': 4, 'bytes': '0123456789ab'})
    if writes:
        release['writes'] = writes
    fixture = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': args, 'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': [release]}
    validate_contract(fixture, self_allocator)
    return fixture

def validate_contract(fixture, self_allocator):
    """Validate supplied formal extents, without predicting source calls or outputs."""
    first_extent = OWNER_EXTENT if self_allocator else 8
    for script in fixture['stubs']:
        for capture in script.get('argmem', []):
            extent = first_extent if capture['arg'] == 0 else 16
            offset = capture.get('offset', 0)
            if not 0 <= offset <= offset + capture['size'] <= extent:
                raise ValueError('callback capture outside supplied formal view')
        for write in script.get('writes', []):
            extent = first_extent if write['arg'] == 0 else 16
            size = len(bytes.fromhex(write['bytes'])) if 'bytes' in write else 8
            offset = write.get('offset', 0)
            if not 0 <= offset <= offset + size <= extent:
                raise ValueError('callback write outside supplied formal view')
            if write['arg'] == 0 and (size != 8 or offset not in (0, ALLOCATOR_FIELD, STORAGE_FIELD)):
                raise ValueError('callback receiver write lacks exact known field/view')
CASES = [case('ordinary_release'), case('no_storage_valid_allocator', old='0x0'), case('no_storage_null_allocator', old='0x0', invalid_allocator='0x0'), case('no_storage_opaque_allocator', old='0x0', invalid_allocator='0xfedcba9876543210'), case('no_storage_opaque_vptr', old='0x0', self_allocator=True, invalid_vtable=True), case('opaque_old_full_pointer', old='0xabcdef0187654321'), case('opaque_old_low_word_all_bits', old='0x12345678ffffffff'), case('opaque_old_address_one', old='0x1'), case('free_return_nonzero_ignored', free_result=1311768467139281697), case('free_return_all_bits_ignored', free_result=MASK64), case('self_allocator_old_field_snapshot', self_allocator=True), case('self_callback_changes_storage_then_clear', self_allocator=True, change_storage=True), case('self_callback_changes_allocator_preserved', self_allocator=True, change_allocator=True), case('self_callback_changes_vptr_preserved', self_allocator=True, change_vptr=True), case('ordinary_callback_changes_vptr_preserved', change_vptr=True), case('callback_old_bytes_preserved', change_old_bytes=True), case('self_callback_combined_owned_writes', self_allocator=True, change_vptr=True, change_allocator=True, change_storage=True, change_old_bytes=True), case('unaligned_owner_view', owner_offset=17), case('unaligned_allocator_view', allocator_offset=9), case('unaligned_vtable_view', vtable_offset=1), case('all_views_unaligned_self_allocator', owner_offset=17, allocator_offset=9, vtable_offset=1, self_allocator=True), case('unaligned_self_storage_write', owner_offset=17, allocator_offset=9, vtable_offset=1, self_allocator=True, change_storage=True), case('owner_zero_pattern', pattern=0), case('owner_all_bits_pattern', pattern=255), case('incoming_nonargument_registers_ignored', incoming={'rsi': '0xffffffffffffffff', 'rdx': '0x1234567887654321', 'rcx': '0x8000000000000000', 'r8': '0xfeedface', 'r9': '0x7fffffffffffffff'}), case('old_bytes_zero_pattern', old_pattern=0), case('old_bytes_all_bits_pattern', old_pattern=255), case('no_storage_self_allocator', old='0x0', self_allocator=True)]

def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (destination / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic release inputs written to {destination}')
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
