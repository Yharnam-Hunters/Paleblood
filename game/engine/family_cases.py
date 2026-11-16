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

initializes_owner_block_six_pointers_to_0x05ac5cac,
initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc,
initializes_owner_block_first_pointer_to_0x05ac74a3, destroys_pair_owner_with_vtable_0x0570fb90,
destroys_entry_storage_with_vtable_0x057045e0 and releases_counted_object_at_0x10
(block_init_and_owned_release_families.cpp): the copies of six short functions. Their cases are
block_init_and_owned_release_family_cases.py's, at each member's own address.
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


def _block_init(name: str):
    return getattr(_load('block_init_and_owned_release_family_cases'), name)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'initializes_owner_block_six_pointers_to_0x05ac5cac': _block_init('block_cases'),
         'initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc': _block_init('block_cases'),
         'initializes_owner_block_first_pointer_to_0x05ac74a3': _block_init('block_cases'),
         'destroys_pair_owner_with_vtable_0x0570fb90': _block_init('pair_cases'),
         'destroys_entry_storage_with_vtable_0x057045e0': _block_init('storage_cases'),
         'releases_counted_object_at_0x10': _block_init('counted_cases')}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'initializes_owner_block_six_pointers_to_0x05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_init_block_five_pointers', 'bb_mutant_init_block_returns_one'],
    'initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc': ['bb_mutant_does_nothing', 'bb_mutant_init_block_pair_swapped', 'bb_mutant_init_block_returns_one'],
    'initializes_owner_block_first_pointer_to_0x05ac74a3': ['bb_mutant_does_nothing', 'bb_mutant_init_block_one_keeps_0x70', 'bb_mutant_init_block_returns_one'],
    'destroys_pair_owner_with_vtable_0x0570fb90': ['bb_mutant_does_nothing', 'bb_mutant_pair_owner_second_early', 'bb_mutant_pair_owner_first_cleared_late', 'bb_mutant_pair_owner_no_final_clear', 'bb_mutant_pair_owner_second_alone'],
    'destroys_entry_storage_with_vtable_0x057045e0': ['bb_mutant_does_nothing', 'bb_mutant_entry_storage_skips_zero_count', 'bb_mutant_entry_storage_wide_bytes', 'bb_mutant_entry_storage_clears_0x20_late', 'bb_mutant_entry_storage_keeps_count'],
    'releases_counted_object_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_counted_0x10_field_once', 'bb_mutant_counted_0x10_counts_once', 'bb_mutant_counted_0x10_weak_zero', 'bb_mutant_counted_0x10_keeps_pointer'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
