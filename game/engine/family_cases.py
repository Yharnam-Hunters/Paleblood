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


def slot_table_cases(address: str, value: int | None) -> list[dict]:
    """A table buffer of 0x90 bytes (a pattern; the bytes past +0x80 must stay) and two owners;
    every written byte and the result are compared."""
    out = []
    for seed, owner in ((3, '0x123456789abcdef0'), (5, '0xfedcba9876543210')):
        out.append({'schema': 1, 'address': address, 'id': f'table_{seed}', 'returns': 'i64',
                    'buffers': {'table': {'size': 0x90, 'bytes': bytes((seed * 29 + i) & 0xff for i in range(0x90)).hex()}},
                    'memory': [], 'imports': [], 'stubs': [],
                    'args': {'rdi': owner, 'rsi': 'buf:table', 'rax': '0x5aa5c33c0ff0e11e'}})
    return out


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'fills_3_slots_with_05ac5cac': slot_table_cases,
         'fills_1_slot_with_05ac5e6b': slot_table_cases,
         'fills_1_slot_with_05ac5cac': slot_table_cases,
         'fills_4_slots_with_05ac5cac': slot_table_cases,
         'fills_5_slots_with_05ac5cac': slot_table_cases,
         'fills_1_slot_with_05acd45c': slot_table_cases,
         'fills_1_slot_with_05acd1bc': slot_table_cases,
         'fills_2_slots_with_05acd45c': slot_table_cases,
         'fills_2_slots_with_05ac5cac': slot_table_cases,
         'fills_1_slot_with_05acee04': slot_table_cases,
         'fills_1_slot_with_05ac5dfd': slot_table_cases,
         'fills_1_slot_with_05ac5f5b': slot_table_cases,
         'fills_2_slots_05ac74a5_05ac74a3': slot_table_cases,
         'fills_2_slots_05ac74a7_05ac5cac': slot_table_cases,
         'fills_2_slots_05ac74a4_05ac74a3': slot_table_cases,
         'fills_2_slots_05ac74a9_05ac5cac': slot_table_cases,
}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # No write. (A mutant with the wrong pointers would need the game image.)
    'fills_3_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5e6b': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_4_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_5_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05acd45c': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05acd1bc': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_with_05acd45c': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05acee04': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5dfd': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5f5b': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a5_05ac74a3': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a7_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a4_05ac74a3': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a9_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
