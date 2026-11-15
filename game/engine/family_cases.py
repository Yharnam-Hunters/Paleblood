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


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         **{name: static_vtable_cases(name) for name in STATIC_VTABLE_SETTERS}}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Nothing stored. (A mutant storing the wrong vtable would need the game image.)
    'sets_static_vtable_at_0x05ac59b0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59c0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59d0': ['bb_mutant_does_nothing'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
