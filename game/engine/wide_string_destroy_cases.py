#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored edge cases for the original wide-string destructor; no gameplay inputs."""
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x02a44140'
FREE = 0x00500200
OBJECT_START = 0x10
ALLOCATOR_START = 0x50
MASK64 = (1 << 64) - 1


def word(value):
    """Preserve all supplied 64-bit bits without signed conversion."""
    return struct.pack('<Q', value & MASK64).hex()


def case(cid, capacity=8, owns=1, size=13, null_heap=False, mutation=None,
         result=0, pattern=0xa5, incoming=None):
    """Keep an authored allocator beside the object for bounded callback snapshots."""
    data = bytearray([pattern] * 0xa0)
    struct.pack_into('<Q', data, OBJECT_START + 0x18, size & MASK64)
    struct.pack_into('<Q', data, OBJECT_START + 0x20, capacity & MASK64)
    data[OBJECT_START + 0x30] = owns
    args = {'rdi': f'buf:string+0x{OBJECT_START:x}'}
    args.update(incoming or {})
    fixture = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
               'args': args,
               'buffers': {'string': {'size': len(data), 'bytes': data.hex()},
                           'heap': {'size': 48, 'bytes': '6d' * 48},
                           'other_allocator': {'size': 16, 'bytes': '7e' * 16},
                           'vtable': {'size': 0x78, 'bytes': 'c3' * 0x78}},
               'memory': [], 'imports': [], 'stubs': []}
    if capacity < 8:
        fixture['memory'].append({'addr': f'buf:string+0x{OBJECT_START + 0x28:x}',
                                  'bytes': word(0xfffffffffffffff0)})
        return fixture

    fixture['memory'] += [
        {'addr': f'buf:string+0x{OBJECT_START + 8:x}',
         **({'bytes': word(0)} if null_heap else {'pointer': 'heap'})},
        {'addr': f'buf:string+0x{OBJECT_START + 0x28:x}',
         'pointer': f'string+0x{ALLOCATOR_START:x}'},
        {'addr': f'buf:string+0x{ALLOCATOR_START:x}', 'pointer': 'vtable'},
        {'addr': 'buf:vtable+0x70', 'guest': hex(FREE)},
    ]
    free = {'address': hex(FREE), 'argc': 2, 'ret': hex(result & MASK64),
            'argmem': [{'arg': 0, 'offset': OBJECT_START - ALLOCATOR_START, 'size': 0x38}]}
    if not null_heap:
        free['argmem'].append({'arg': 1, 'size': 48})
    edits = []
    object_relative = OBJECT_START - ALLOCATOR_START
    if mutation == 'all_reset_fields':
        for offset, value in [(8, 0x123456789abcdef0), (0x18, 0xfedcba9876543210), (0x20, MASK64)]:
            edits.append({'arg': 0, 'offset': object_relative + offset, 'bytes': word(value)})
    if mutation == 'preserved_fields':
        edits += [{'arg': 0, 'offset': object_relative, 'bytes': word(0x9876543210abcdef)},
                  {'arg': 0, 'offset': object_relative + 0x28, 'pointer': 'other_allocator'},
                  {'arg': 0, 'offset': object_relative + 0x30, 'bytes': '80d1d2d3d4d5d6d7'}]
    if mutation == 'heap_bytes':
        if null_heap:
            raise ValueError('heap callback needs a declared heap')
        edits.append({'arg': 1, 'offset': 6, 'bytes': '123456789abc'})
    if edits:
        free['writes'] = edits
    fixture['stubs'].append(free)
    return fixture


CASES = [case('heap_boundary_8'), case('heap_capacity_9', capacity=9),
         case('heap_capacity_large', capacity=0x100000007),
         case('heap_capacity_sign_bit', capacity=0x8000000000000000),
         case('heap_capacity_all_bits', capacity=MASK64),
         case('heap_null_still_calls_free', null_heap=True),
         case('heap_callback_returns_negative_ignored', result=MASK64),
         case('heap_callback_returns_nonzero_ignored', result=0x1234567887654321),
         case('free_before_reset_and_callback_reset_fields', mutation='all_reset_fields'),
         case('callback_preserved_fields_survive', mutation='preserved_fields'),
         case('callback_heap_writes_survive', mutation='heap_bytes'),
         case('null_heap_callback_reset_fields', null_heap=True, mutation='all_reset_fields'),
         case('zero_object_pattern', pattern=0), case('all_object_bits', pattern=0xff),
         case('high_size_bits_reset', size=MASK64),
         case('nonargument_registers_irrelevant', incoming={'rsi': '0xffffffffffffffff',
             'rdx': '0x1234567800000001', 'rcx': '0x80000000', 'r8': '0x100000007', 'r9': '0xa5'})]
for capacity in (0, 1, 6, 7):
    CASES.append(case(f'inline_capacity_{capacity}_no_allocator_access', capacity=capacity, size=MASK64))
for owns in (0, 2, 0x80, 0xff):
    CASES.append(case(f'heap_owns_{owns:02x}_ignored', owns=owns))
    CASES.append(case(f'inline_owns_{owns:02x}_preserved', capacity=7, owns=owns))


def main():
    """Write only synthetic fixtures into the selected directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=False)
    for fixture in CASES:
        (directory / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} authored cases written to {directory}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
