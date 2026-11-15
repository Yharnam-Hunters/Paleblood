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


MEMBER_RELEASE_0X10 = '0x02450560'
MEMBER_OPERATOR_DELETE = '0x02fbe588'
MEMBER_RELEASE_0X01401720 = '0x01401720'
MEMBER_FREE_SIZED = '0x02fa1c20'
MEMBER_OBJECT_ALLOCATOR = '0x05ac8288'


def holder(seed: int, size: int = 0x30) -> dict:
    return {'size': size, 'bytes': bytes((seed * 19 + i) & 0xff for i in range(size)).hex()}


def member_case(address: str, case_id: str, offset: int, set_member: bool, seed: int, returns: str = 'void') -> dict:
    """A holder whose member at OFFSET is a buffer (SET_MEMBER) or null."""
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': returns,
            'buffers': {'holder': holder(seed)}, 'memory': [], 'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:holder', 'rsi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
    if set_member:
        case['buffers']['member'] = holder(seed + 1, 0x20)
        case['memory'].append({'addr': f'buf:holder+0x{offset:x}', 'pointer': 'member'})
    else:
        case['memory'].append({'addr': f'buf:holder+0x{offset:x}', 'bytes': '00' * 8})
    return case


def vtable_release_cases(deletes: bool):
    """The member at +0x10 set (released with a stub comparing it, then cleared) or null; the base
    destructor (and, DELETES, operator delete) are stubs comparing the holder and its first 0x18
    bytes at the call (the table address stored, the member cleared). The base destructor is an
    empty function (returns_nothing) and runs as it is, unstubbed."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, set_member, seed in (('member', True, 3), ('no_member', False, 5)):
            case = member_case(address, case_id, 0x10, set_member, seed)
            if set_member:
                case['stubs'].append({'address': MEMBER_RELEASE_0X10, 'argc': 1, 'ret': seed})
            if deletes:
                case['stubs'].append({'address': MEMBER_OPERATOR_DELETE, 'argc': 1, 'ret': 0})
            out.append(case)
        return out
    return cases


def member_at_cases(offset: int):
    """The member at OFFSET set (released through 0x01401720, a stub comparing it, then cleared)
    or null."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, set_member, seed in (('member', True, 3), ('no_member', False, 5)):
            case = member_case(address, case_id, offset, set_member, seed)
            if set_member:
                case['stubs'].append({'address': MEMBER_RELEASE_0X01401720, 'argc': 1, 'ret': seed})
            out.append(case)
        return out
    return cases


def sized_free_cases(address: str, value: int | None) -> list[dict]:
    """The destructor (an empty function, returns_nothing, run unstubbed), then the sized free (a stub comparing the
    allocator the global points at, the object, the size and the alignment), whose result comes
    back."""
    out = []
    for seed in (3, 5):
        case = member_case(address, f'object_{seed}', 0x10, False, seed, 'i64')
        case['memory'] = []
        case['buffers']['allocator'] = holder(seed + 2, 0x20)
        case['memory'].append({'addr': MEMBER_OBJECT_ALLOCATOR, 'pointer': 'allocator'})
        case['stubs'] = [{'address': MEMBER_FREE_SIZED, 'argc': 4, 'ret': seed << 40 | 0xf3ee}]
        out.append(case)
    return out


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'stores_vtable_releases_0x10_then_destroys': vtable_release_cases(False),
         'stores_vtable_releases_0x10_destroys_then_deletes': vtable_release_cases(True),
         'releases_member_at_0x20_through_0x01401720': member_at_cases(0x20),
         'releases_member_at_0x18_through_0x01401720': member_at_cases(0x18),
         'destroys_then_frees_0x10_bytes_through_0x05ac8288': sized_free_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    'stores_vtable_releases_0x10_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_releases_0x10_destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_member_at_0x20_through_0x01401720': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_member_at_0x18_through_0x01401720': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_then_frees_0x10_bytes_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
