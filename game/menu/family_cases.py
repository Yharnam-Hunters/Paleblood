#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases and mutants for the menu function families (symbols/families.csv).

tools/verify.py family imports this module: cases(family, address, value) returns the synthetic
cases one member runs (schema 1, docs/VERIFY.md), and MUTANTS names the deliberately wrong
replacements (test/family_mutants.c) each family's cases must catch.

releases_named_resource_held_at_0x70 and releases_named_resource_held_at_0x68: the cases of
0x0238a210 (scaleform_glue_cases.py, named_case) at the member's own address, on a resource whose
members sit at HELD (the flags), HELD + 8 (the handle) and HELD + 0x10 (the name, a wide string):
held or not, with or without a handle, the registry, its entry and a name on the heap, and the
resource's other bytes zero or all ones.
"""
from __future__ import annotations

import struct

REGISTRY = '0x058b3570'
REGISTRY_FIND = '0x013a2a00'
REGISTRY_RELEASE = '0x013a33c0'
HANDLE_RELEASE = '0x01401720'
WIDE_STRING_TEXT, WIDE_STRING_SIZE, WIDE_STRING_CAPACITY = 0x8, 0x18, 0x20
INLINE_CAPACITY = 7   # below 8: the text is inline
HEAP_CAPACITY = 0x17


def word(memory: list, addr: str, value) -> None:
    if isinstance(value, str):
        memory.append({'addr': addr, 'pointer': value})
    else:
        memory.append({'addr': addr, 'bytes': struct.pack('<Q', value).hex()})


def named_case(address: str, held_at: int, cid: str, held=True, handle=True, registry=True, entry=True,
               name_heap=False, fill=0xa5) -> dict:
    name = held_at + 0x10
    buffers = {'resource': {'size': 0xc0, 'bytes': f'{fill:02x}' * 0xc0}, 'handle': {'size': 0x10},
               'registry': {'size': 0x10}, 'entry': {'size': 0x10},
               'name_text': {'size': 0x30, 'bytes': 'invented resource\0'.encode('utf-16-le').hex()}}
    memory = [{'addr': f'buf:resource+0x{held_at:x}', 'bytes': '01' if held else '00'}]
    word(memory, f'buf:resource+0x{held_at + 8:x}', 'handle' if handle else 0)
    word(memory, REGISTRY, 'registry' if registry else 0)
    if name_heap:
        word(memory, f'buf:resource+0x{name + WIDE_STRING_TEXT:x}', 'name_text')
        word(memory, f'buf:resource+0x{name + WIDE_STRING_CAPACITY:x}', HEAP_CAPACITY)
    else:
        memory.append({'addr': f'buf:resource+0x{name + WIDE_STRING_TEXT:x}',
                       'bytes': 'abc\0'.encode('utf-16-le').hex()})
        word(memory, f'buf:resource+0x{name + WIDE_STRING_CAPACITY:x}', INLINE_CAPACITY)
    word(memory, f'buf:resource+0x{name + WIDE_STRING_SIZE:x}', 3)
    stubs = []
    if held:
        if handle:
            stubs.append({'address': HANDLE_RELEASE, 'argc': 1})
        if registry:
            stubs.append({'address': REGISTRY_FIND, 'argc': 2, 'ret': 'buf:entry' if entry else 0,
                          'argmem': [{'arg': 1, 'size': 0x8}]})
            # listed even when no entry is found, so a release of a null entry would show
            stubs.append({'address': REGISTRY_RELEASE, 'argc': 4, 'argu32': [2, 3]})
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:resource'},
            'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': []}


def named_resource_cases(held_at: int):
    def cases(address: str, value: int | None) -> list[dict]:
        def case(cid: str, **kw) -> dict:
            return named_case(address, held_at, cid, **kw)
        return [case('held'), case('not_held', held=False), case('no_handle', handle=False),
                case('no_registry', registry=False), case('not_in_registry', entry=False),
                case('long_name', name_heap=True), case('long_name_not_in_registry', name_heap=True, entry=False),
                case('nothing_but_held', handle=False, registry=False),
                case('zero_filled', fill=0), case('all_bits', fill=0xff)]
    return cases


CASES = {'releases_named_resource_held_at_0x70': named_resource_cases(0x70),
         'releases_named_resource_held_at_0x68': named_resource_cases(0x68)}

MUTANTS = {family: ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'] for family in CASES}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
