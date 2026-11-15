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
import struct
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


# (object, guard) of each guarded vtable setter; the vtable is whatever the original stores.
STATIC_VTABLE_SETTERS = {'sets_static_vtable_at_0x05ac59b0': (0x05ac59b0, 0x05ac59b8), 'sets_static_vtable_at_0x05ac59c0': (0x05ac59c0, 0x05ac59c8), 'sets_static_vtable_at_0x05ac59d0': (0x05ac59d0, 0x05ac59d8)}


def static_vtable_cases(family: str):
    """The guard 0 (the vtable is stored and the guard set), 1 and 0xff (nothing happens) and
    0x100 (only the low byte is tested: stored); the object starts as a pattern."""
    obj, guard = STATIC_VTABLE_SETTERS[family]

    def cases(address: str, value: int | None) -> list[dict]:
        return [{'schema': 1, 'address': address, 'id': f'guard_{start:x}', 'returns': 'void',
                 'buffers': {},
                 'memory': [{'addr': hex(obj), 'bytes': struct.pack('<Q', 0x5a5a5a5a5a5a5a5a ^ start).hex()},
                            {'addr': hex(guard), 'bytes': struct.pack('<Q', start).hex()}],
                 'imports': [], 'stubs': [],
                 'args': {'rdi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
                for start in (0, 1, 0xff, 0x100)]
    return cases


RELEASE_FUNCTION = '0x00400240'   # a stand-in for slot 0x18 of the member's table, stubbed
RELEASE_SLOT = 0x18


def release_member_cases(address: str, value: int | None) -> list[dict]:
    """No member at +8 (only the vtable is stored), and a member whose table's slot 0x18 is a stub
    comparing the member; the object starts all ones and as a pattern."""
    out = []
    for seed, fill in ((3, 0xff), (5, None)):
        body = bytes(fill if fill is not None else (seed * 11 + i) & 0xff for i in range(0x20))
        for member in (False, True):
            c = {'schema': 1, 'address': address, 'id': f'{"member" if member else "no_member"}_{seed}',
                 'returns': 'void', 'buffers': {'object': {'size': 0x20, 'bytes': body.hex()}},
                 'memory': [{'addr': 'buf:object+0x8', 'bytes': bytes(8).hex()}], 'imports': [], 'stubs': [],
                 'args': {'rdi': 'buf:object', 'rsi': '0x2222222222222222', 'rax': '0x5aa5c33c0ff0e11e'}}
            if member:
                c['buffers']['member'] = {'size': 0x10, 'bytes': bytes((seed + i) & 0xff for i in range(0x10)).hex()}
                c['buffers']['table'] = {'size': 0x20}
                c['memory'] = [{'addr': 'buf:object+0x8', 'pointer': 'member'},
                               {'addr': 'buf:member+0x0', 'pointer': 'table'},
                               {'addr': f'buf:table+0x{RELEASE_SLOT:x}', 'guest': RELEASE_FUNCTION}]
                c['stubs'] = [{'address': RELEASE_FUNCTION, 'argc': 1, 'ret': seed}]
            out.append(c)
    return out


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         **{name: static_vtable_cases(name) for name in STATIC_VTABLE_SETTERS},
         'stores_vtable_0x05541b90_then_releases_member_at_8': release_member_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Nothing stored. (A mutant storing the wrong vtable would need the game image.)
    'sets_static_vtable_at_0x05ac59b0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59c0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59d0': ['bb_mutant_does_nothing'],
    # Nothing stored or called; or the member released but no vtable stored.
    'stores_vtable_0x05541b90_then_releases_member_at_8': ['bb_mutant_does_nothing', 'bb_mutant_releases_member_at_8_only'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
