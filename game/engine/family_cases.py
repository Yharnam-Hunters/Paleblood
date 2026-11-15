#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases and mutants for the engine function families (symbols/families.csv).

tools/verify.py family imports this module: cases(family, address, value) returns the synthetic
cases one member runs (schema 1, docs/VERIFY.md), and MUTANTS names the deliberately wrong
replacements (test/family_mutants.c) each family's cases must catch.

initialize_static_wide_string: the copies of 0x0217ffb0's guarded static initializer (a relative
shape: the same code reaching the same guard, string, allocator and functions). Every member runs
the cases of game/engine/static_wide_string_init_cases.py at its own address: the guard set or
clear, an allocator present or made by the factory, the heap info flags with and without the
valid bit, the info, diagnostic and registration callbacks changing the allocator, the object or
the guard, and registration results that are ignored.
"""
from __future__ import annotations

import copy
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name: str):
    spec = importlib.util.spec_from_file_location(f'engine_{name}', os.path.join(HERE, f'{name}.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def static_wide_string_cases(address: str, value: int | None) -> list[dict]:
    out = []
    for case in _load('static_wide_string_init_cases').CASES:
        case = copy.deepcopy(case)
        case['address'] = address
        out.append(case)
    return out


RELEASE_COUNTED = '0x027ee1e0'
DESTROY_BASE = '0x006f92f0'


OBJECT_ALLOCATOR = '0x057e7320'    # the global that points at the allocator
ALLOCATOR_FREE = '0x00400280'      # a stand-in for the allocator's slot 0x68, stubbed


def member_release_case(case_id: str, address: str, member: str | int, count: int | None, seed: int,
                        freed: bool = False) -> dict:
    """A holder whose +0xc0 is MEMBER: 0, a tagged value, or 'counted' (a buffer whose dword at
    +0x20 is COUNT). The release function (when the masked count is not 0) and the base
    destructor are stubs comparing their object and the bytes behind it (the vtable already
    stored, the count already decremented)."""
    holder = bytearray((seed * 13 + i) & 0xff for i in range(0xd0))
    holder[0xc0:0xc8] = (0 if member == 'counted' else member).to_bytes(8, 'little')
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'holder': {'size': 0xd0, 'bytes': holder.hex()}},
            'memory': [], 'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:holder', 'rsi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
    if member == 'counted':
        counted = bytearray((seed * 7 + i) & 0xff for i in range(0x28))
        counted[0x20:0x24] = count.to_bytes(4, 'little')
        case['buffers']['counted'] = {'size': 0x28, 'bytes': counted.hex()}
        case['memory'].append({'addr': 'buf:holder+0xc0', 'pointer': 'counted'})
        if count & 0x3fffff:
            case['stubs'].append({'address': RELEASE_COUNTED, 'argc': 1, 'ret': seed,
                                  'argmem': [{'arg': 0, 'size': 0x28}]})
    case['stubs'].append({'address': DESTROY_BASE, 'argc': 1, 'ret': 0,
                          'argmem': [{'arg': 0, 'offset': 0xc0, 'size': 8}]})
    if freed:
        # The deleting twin: then the allocator's slot 0x68, comparing the allocator and the holder.
        case['returns'] = 'i64'
        case['buffers'].update({'allocator': {'size': 0x20, 'bytes': bytes((seed * 3 + i) & 0xff for i in range(0x20)).hex()},
                                'allocator_table': {'size': 0x80}})
        case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                           {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                           {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
        case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
    return case


def member_release_cases(address: str, value: int | None, freed: bool = False) -> list[dict]:
    """No member; a tagged one; a counted one with the count 5, 0x400000 (masked 0: no release)
    and 0xffc00001 (masked 1). FREED: the deleting twin's cases."""
    return [member_release_case('none', address, 0, None, 3, freed),
            member_release_case('tagged', address, 0x123457, None, 5, freed),
            member_release_case('counted', address, 'counted', 5, 7, freed),
            member_release_case('counted_masked_zero', address, 'counted', 0x400000, 11, freed),
            member_release_case('counted_high_bits', address, 'counted', 0xffc00001, 13, freed)]


def member_release_free_cases(address: str, value: int | None) -> list[dict]:
    return member_release_cases(address, value, freed=True)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'releases_member_at_0xc0_then_destroys_base': member_release_cases,
         'releases_member_at_0xc0_then_destroys_and_frees': member_release_free_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # No call and no write. (A mutant reaching the vtable or the base destructor would need the
    # game image, which the mutant library does not see.)
    'releases_member_at_0xc0_then_destroys_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_member_at_0xc0_then_destroys_and_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
