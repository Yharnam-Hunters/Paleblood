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


MEMBER_DESTRUCTOR = '0x005c9010'
BASE_DESTRUCTOR = '0x005696e0'
MEMBER_RELEASE = '0x0045a4c0'
OBJECT_ALLOCATOR = '0x057e7320'    # the global that points at the allocator
ALLOCATOR_FREE = '0x00400280'      # a stand-in for the allocator's slot 0x68, stubbed
CLASS_HEAD = 0x70                  # the bytes before the member at +0x70: the tables and +0x68


def object_buffer(seed: int, size: int = 0x100) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}


def member_destroying_cases(member: int, adjust: int, frees: bool, releases: bool):
    """An object (thunks get it at +ADJUST) whose member at +MEMBER goes to the member destructor,
    then the object to the base destructor (stubs comparing their argument; for RELEASES the
    destructor also sees the object's first 0x70 bytes still untouched, and the base destructor
    sees the two tables stored); RELEASES: the member at +0x68 set (released with a stub comparing
    it) or null. FREES: then slot 0x68 of the allocator the global points at, a stub comparing the
    allocator and the object, whose result comes back."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, seed, released in (('released', 3, True), ('no_release', 5, False)):
            if not releases:
                case_id = f'object_{seed}'
            case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i64' if frees else 'void',
                    'buffers': {'object': object_buffer(seed)}, 'memory': [], 'imports': [],
                    'stubs': [{'address': MEMBER_DESTRUCTOR, 'argc': 1, 'ret': seed},
                              {'address': BASE_DESTRUCTOR, 'argc': 1, 'ret': seed + 1}],
                    'args': {'rdi': f'buf:object+0x{adjust:x}', 'rsi': hex(0x1111111111111111 * (seed & 0xf)),
                             'rax': '0x5aa5c33c0ff0e11e'}}
            if releases:
                case['stubs'][0]['argmem'] = [{'arg': 0, 'offset': -member, 'size': CLASS_HEAD}]
                case['stubs'][1]['argmem'] = [{'arg': 0, 'offset': 0, 'size': CLASS_HEAD}]
                case['stubs'].append({'address': MEMBER_RELEASE, 'argc': 1, 'ret': seed + 2})
                if released:
                    case['buffers']['released'] = object_buffer(seed + 7, 0x20)
                    case['memory'].append({'addr': 'buf:object+0x68', 'pointer': 'released'})
                else:
                    case['memory'].append({'addr': 'buf:object+0x68', 'bytes': '00' * 8})
            if frees:
                case['buffers'].update({'allocator': object_buffer(seed + 1, 0x20), 'allocator_table': {'size': 0x80}})
                case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                                   {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                                   {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
                case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
            out.append(case)
        return out
    return cases


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'destroys_member_0x68_then_base': member_destroying_cases(0x68, 0x0, False, False),
         'destroys_member_0x68_and_base_then_frees': member_destroying_cases(0x68, 0x0, True, False),
         'adjusts_0x20_destroys_member_0x68_then_base': member_destroying_cases(0x68, 0x20, False, False),
         'adjusts_0x20_destroys_member_0x68_and_base_then_frees': member_destroying_cases(0x68, 0x20, True, False),
         'adjusts_0x68_destroys_member_0x68_then_base': member_destroying_cases(0x68, 0x68, False, False),
         'adjusts_0x68_destroys_member_0x68_and_base_then_frees': member_destroying_cases(0x68, 0x68, True, False),
         'destroys_member_0x70_releases_0x68_then_base': member_destroying_cases(0x70, 0x0, False, True),
         'destroys_member_0x70_releases_0x68_and_base_then_frees': member_destroying_cases(0x70, 0x0, True, True),
         'adjusts_0x20_destroys_member_0x70_releases_0x68_then_base': member_destroying_cases(0x70, 0x20, False, True),
         'adjusts_0x20_destroys_member_0x70_releases_0x68_and_base_then_frees': member_destroying_cases(0x70, 0x20, True, True),
         'adjusts_0x70_destroys_member_0x70_releases_0x68_then_base': member_destroying_cases(0x70, 0x70, False, True),
         'adjusts_0x70_destroys_member_0x70_releases_0x68_and_base_then_frees': member_destroying_cases(0x70, 0x70, True, True),
}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    'destroys_member_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_member_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x68_destroys_member_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x68_destroys_member_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_member_0x70_releases_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_member_0x70_releases_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x70_releases_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x70_releases_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x70_destroys_member_0x70_releases_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x70_destroys_member_0x70_releases_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
