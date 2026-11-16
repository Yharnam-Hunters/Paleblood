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

fills_owner_block_with_two_addresses and fills_owner_block_with_one_address
(owner_block_families.cpp), updates_and_calls_slot_0x38_unless_refused,
adopts_buffer_at_0x10_after_slot_0x78, destroys_with_0x0139bf90_then_deletes,
clears_0x28_and_passes_0x18_to_0x027286b0 and passes_float_to_0x01b5f7f0_when_0x010edc70_agrees
(forwarding_method_families.cpp): the copies of seven short functions. Their cases are
owner_block_and_forwarding_family_cases.py's, at each member's own address.
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


def _forwarding(name: str):
    return getattr(_load('owner_block_and_forwarding_family_cases'), name)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'fills_owner_block_with_two_addresses': _forwarding('block_cases'),
         'fills_owner_block_with_one_address': _forwarding('block_cases'),
         'updates_and_calls_slot_0x38_unless_refused': _forwarding('update_cases'),
         'adopts_buffer_at_0x10_after_slot_0x78': _forwarding('adopt_cases'),
         'destroys_with_0x0139bf90_then_deletes': _forwarding('delete_cases'),
         'clears_0x28_and_passes_0x18_to_0x027286b0': _forwarding('clear_cases'),
         'passes_float_to_0x01b5f7f0_when_0x010edc70_agrees': _forwarding('float_cases')}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'fills_owner_block_with_two_addresses': ['bb_mutant_does_nothing', 'bb_mutant_owner_block_one_address', 'bb_mutant_owner_block_keeps_0x70', 'bb_mutant_owner_block_returns_one'],
    'fills_owner_block_with_one_address': ['bb_mutant_does_nothing', 'bb_mutant_owner_block_two_addresses', 'bb_mutant_owner_block_keeps_0x70', 'bb_mutant_owner_block_returns_one'],
    'updates_and_calls_slot_0x38_unless_refused': ['bb_mutant_does_nothing', 'bb_mutant_update_check_whole_register', 'bb_mutant_update_table_read_first', 'bb_mutant_update_stops_without_target'],
    'adopts_buffer_at_0x10_after_slot_0x78': ['bb_mutant_does_nothing', 'bb_mutant_adopt_buffer_read_first', 'bb_mutant_adopt_frees_buffer_itself', 'bb_mutant_adopt_fixed_free_function'],
    'destroys_with_0x0139bf90_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_destroy_without_delete', 'bb_mutant_delete_without_destroy', 'bb_mutant_delete_then_destroy'],
    'clears_0x28_and_passes_0x18_to_0x027286b0': ['bb_mutant_does_nothing', 'bb_mutant_clear_after_passing', 'bb_mutant_clear_passes_null_target'],
    'passes_float_to_0x01b5f7f0_when_0x010edc70_agrees': ['bb_mutant_does_nothing', 'bb_mutant_float_check_whole_register', 'bb_mutant_float_passed_unconditionally'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
