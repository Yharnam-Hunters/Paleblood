#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic authored allocator-consumer wires, not real allocator behavior.

The observed caller's void ABI is the verification boundary. Three supplied
callbacks are opaque inputs, including an alternative allocation target used to
observe vptr reloads; unused callback scripts are permitted. No expected output
or arithmetic model is computed. Owner/self-allocator aliases combine the known
owner extent and allocator prefix without claiming real constructor validity.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x0132c6c0'
FREE = '0x00500200'
ALLOCATE = '0x00500220'
ALTERNATE_ALLOCATE = '0x00500240'
OWNER_EXTENT = 0x178
ALLOCATOR_FIELD = 0x8
STORAGE_FIELD = 0x170
FREE_SLOT = 0x70
ALLOCATE_SLOT = 0x58
MASK64 = (1 << 64) - 1
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16,
    'evidence': ['Canonical81 bytes/25 instructions and saved Program exact-body/entry-reference query',
                 'Existing opaque engine::Allocator free+0x70 and allocate+0x58 call interfaces',
                 'Sole known caller overwrites RAX before use; parent accepted observed void scope'],
    'arguments': [{'register': 'rdi', 'type': 'void* owner view, unaligned allowed', 'direction': 'inout'},
                  {'register': 'rsi', 'type': 'uint32_t byte count, upper register bits ignored', 'direction': 'input'},
                  {'register': 'rdx', 'type': 'ignored incoming register seed, not a source formal', 'direction': 'input'},
                  {'register': 'rcx', 'type': 'ignored incoming register seed, not a source formal', 'direction': 'input'},
                  {'register': 'r8', 'type': 'ignored incoming register seed, not a source formal', 'direction': 'input'},
                  {'register': 'r9', 'type': 'ignored incoming register seed, not a source formal', 'direction': 'input'}],
    'entry_register_seed_notes': 'Additional declared register entries below are ignored incoming seeds, not extra source function formals',
    'returns': {'type': 'void'},
    'input_regions': ['owner', 'allocator', 'other_allocator', 'vtable', 'other_vtable', 'old_storage', 'new_storage'],
    'writable_regions': ['owner', 'old_storage'],
    'execution_limits': {'timeout_seconds': 5},
    'scope': 'Synthetic consumer call wires only; unknown computed callers/rawRAX use and real allocator implementations excluded',
    'alias_contract': 'Self-allocator overlays known allocator prefix at owner0; owner+8 points to self; known owner extent0x178',
    'producer_contract': 'Opaque supplied callbacks may read/write only declared bounded formal aliases; no arbitrary global writes',
    'unused_callback_scripts': 'Primary and alternative allocation callbacks are supplied independently; observed original decides which is used',
}


def word(value):
    return struct.pack('<Q', value & MASK64).hex()


def preset(memory, address, value):
    if value.startswith('buf:'):
        memory.append({'addr': address, 'pointer': value[4:]})
    else:
        memory.append({'addr': address, 'bytes': word(int(value, 0))})


def case(cid, count=0xc34, old='buf:old_storage+0x8', result='buf:new_storage+0x8',
         alternate_result='0xfedcba9876543210', self_allocator=False,
         owner_offset=0x10, allocator_offset=0x8, vtable_offset=0,
         invalid_allocator=None, invalid_vtable=False, redirect_allocator=False,
         switch_vptr=False, free_changes_storage=False, free_changes_bytes=False,
         allocate_changes_storage=False, allocate_changes_allocator=False,
         free_result=0, pattern=0xa5, incoming=None):
    """Supply initial views and callback operations without deriving wrapper results."""
    owner = f'buf:owner+0x{owner_offset:x}'
    allocator = f'buf:allocator+0x{allocator_offset:x}'
    other_allocator = f'buf:other_allocator+0x{allocator_offset:x}'
    vtable = f'buf:vtable+0x{vtable_offset:x}'
    other_vtable = f'buf:other_vtable+0x{vtable_offset:x}'
    args = {'rdi': owner, 'rsi': hex(count & MASK64), 'rdx': '0x0', 'rcx': '0x0', 'r8': '0x0', 'r9': '0x0'}
    args.update(incoming or {})
    buffers = {'owner': {'size': 0x1b0, 'bytes': f'{pattern:02x}' * 0x1b0},
               'allocator': {'size': 0x30, 'bytes': '6b' * 0x30},
               'other_allocator': {'size': 0x30, 'bytes': '8d' * 0x30},
               'vtable': {'size': 0x90, 'bytes': 'e7' * 0x90},
               'other_vtable': {'size': 0x90, 'bytes': 'd9' * 0x90},
               'old_storage': {'size': 0x30, 'bytes': 'c3' * 0x30},
               'new_storage': {'size': 0x30, 'bytes': 'f1' * 0x30}}
    memory = []
    preset(memory, f'buf:owner+0x{owner_offset + STORAGE_FIELD:x}', old)
    preset(memory, f'buf:owner+0x{owner_offset + ALLOCATOR_FIELD:x}',
           invalid_allocator if invalid_allocator is not None else owner if self_allocator else allocator)
    preset(memory, allocator, vtable)
    preset(memory, other_allocator, other_vtable)
    if self_allocator:
        preset(memory, owner, '0xfedcba9876543210' if invalid_vtable else vtable)
    for name, start, allocate in [('vtable', vtable_offset, ALLOCATE),
                                  ('other_vtable', vtable_offset, ALTERNATE_ALLOCATE)]:
        memory += [{'addr': f'buf:{name}+0x{start + FREE_SLOT:x}', 'guest': FREE},
                   {'addr': f'buf:{name}+0x{start + ALLOCATE_SLOT:x}', 'guest': allocate}]

    owner_capture = [{'arg': 0, 'size': 16}, {'arg': 0, 'offset': STORAGE_FIELD, 'size': 8}]
    prefix_capture = [{'arg': 0, 'size': 8}]
    release = {'address': FREE, 'argc': 2, 'ret': hex(free_result & MASK64),
               'argmem': list(owner_capture if self_allocator else prefix_capture)}
    if old.startswith('buf:old_storage'):
        release['argmem'].append({'arg': 1, 'size': 16})
    free_writes = []
    if redirect_allocator:
        free_writes.append({'arg': 0, 'offset': ALLOCATOR_FIELD, 'pointer': other_allocator[4:]})
    if switch_vptr:
        free_writes.append({'arg': 0, 'offset': 0, 'pointer': other_vtable[4:]})
    if free_changes_storage:
        free_writes.append({'arg': 0, 'offset': STORAGE_FIELD, 'bytes': word(0x777788889999aaaa)})
    if free_changes_bytes:
        free_writes.append({'arg': 1, 'offset': 4, 'bytes': '0123456789ab'})
    if free_writes:
        release['writes'] = free_writes

    primary = {'address': ALLOCATE, 'argc': 3, 'ret': result,
               'argmem': list(owner_capture if self_allocator else prefix_capture)}
    alternative = {'address': ALTERNATE_ALLOCATE, 'argc': 3, 'ret': alternate_result,
                   'argmem': list(owner_capture if self_allocator and switch_vptr else prefix_capture)}
    allocation_writes = []
    if allocate_changes_storage:
        allocation_writes.append({'arg': 0, 'offset': STORAGE_FIELD, 'bytes': word(0x5555666677778888)})
    if allocate_changes_allocator:
        allocation_writes.append({'arg': 0, 'offset': ALLOCATOR_FIELD, 'pointer': other_allocator[4:]})
    if allocation_writes:
        primary['writes'] = allocation_writes
    fixture = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
               'args': args, 'buffers': buffers, 'memory': memory, 'imports': [],
               'stubs': [release, primary, alternative]}
    validate_contract(fixture, self_allocator, switch_vptr)
    return fixture


def validate_contract(fixture, self_allocator, switch_vptr):
    """Check declared formal-view extents only; do not compute expected behavior."""
    for script in fixture['stubs']:
        owner_view = self_allocator and (script['address'] != ALTERNATE_ALLOCATE or switch_vptr)
        first_extent = OWNER_EXTENT if owner_view else 8
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
            if write['arg'] == 0 and offset not in (0, ALLOCATOR_FIELD, STORAGE_FIELD):
                raise ValueError('callback owner write lacks known field/view')


CASES = [
    case('ordinary_replacement'),
    case('allocate_without_old_storage', old='0x0'),
    case('release_only_zero_count', count=0),
    case('zero_zero_null_allocator', count=0, old='0x0', invalid_allocator='0x0'),
    case('zero_zero_opaque_allocator', count=0, old='0x0', invalid_allocator='0xfedcba9876543210'),
    case('zero_zero_opaque_vptr', count=0, old='0x0', self_allocator=True, invalid_vtable=True),
    case('upper_count_bits_low_zero_release_only', count=0x1234567800000000),
    case('upper_count_bits_low_zero_no_calls', count=0x8000000000000000, old='0x0', invalid_allocator='0x0'),
    case('upper_count_bits_low_one', count=0xfedcba9800000001, old='0x0'),
    case('count_low_sign_bit', count=0x80000000),
    case('count_low_all_bits', count=0xffffffff),
    case('count_all_register_bits', count=MASK64),
    case('upper_count_bits_ordinary_low_word', count=0x1234567800000c34),
    case('count_one', count=1),
    case('count_sixteen', count=16),
    case('allocation_returns_null', result='0x0'),
    case('allocation_returns_high_bits', result='0xfedcba9876543210'),
    case('allocation_returns_low_word_all_bits', result='0x12345678ffffffff'),
    case('old_storage_is_opaque', old='0xabcdef0187654321'),
    case('free_result_all_bits_ignored', count=0, free_result=MASK64),
    case('free_result_nonzero_ignored', free_result=0x1234567887654321),
    case('self_allocator_release_only', count=0, self_allocator=True),
    case('self_allocator_observes_intermediate_clear', self_allocator=True),
    case('free_changes_storage_then_wrapper_clears', self_allocator=True, free_changes_storage=True),
    case('free_redirects_allocator', self_allocator=True, redirect_allocator=True),
    case('free_switches_vptr', self_allocator=True, switch_vptr=True),
    case('allocation_storage_write_overwritten', self_allocator=True, allocate_changes_storage=True),
    case('allocation_allocator_write_preserved', self_allocator=True, allocate_changes_allocator=True),
    case('free_storage_bytes_preserved', free_changes_bytes=True),
    case('unaligned_owner_view', owner_offset=0x11),
    case('unaligned_allocator_view', allocator_offset=0x9),
    case('unaligned_vtable_view', vtable_offset=1),
    case('all_views_unaligned_self_allocator', owner_offset=0x11, allocator_offset=0x9,
         vtable_offset=1, self_allocator=True),
    case('unaligned_self_redirects_allocator', owner_offset=0x11, allocator_offset=0x9,
         vtable_offset=1, self_allocator=True, redirect_allocator=True),
    case('owner_zero_pattern', pattern=0),
    case('owner_all_bits_pattern', pattern=0xff),
    case('incoming_nonargument_registers_ignored', incoming={'rdx': '0xffffffffffffffff',
         'rcx': '0x1234567887654321', 'r8': '0x8000000000000000', 'r9': '0xfeedface'})
]


def main():
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=False)
    for fixture in CASES:
        (destination / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    (destination / 'abi.json').write_text(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(CASES)} authored consumer wire cases written to {destination}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
