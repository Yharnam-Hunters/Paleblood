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

destroys_heap_buffer_owner_directly, destroys_heap_buffer_owner_through_base_at_0x38,
destroys_heap_buffer_owner_through_base_at_0x48 and frees_to_global_heap: the copies of a
destructor body entered at the owner or through one of two bases, and of a free to the heap the
global at 0x057e7320 points at (heap_buffer_owner_families.cpp). Their cases are
heap_buffer_owner_family_cases.py's, at each member's own address.
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


def _owner(name: str):
    return getattr(_load('heap_buffer_owner_family_cases'), name)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'destroys_heap_buffer_owner_directly': _owner('directly_cases'),
         'destroys_heap_buffer_owner_through_base_at_0x38': _owner('through_base_at_0x38_cases'),
         'destroys_heap_buffer_owner_through_base_at_0x48': _owner('through_base_at_0x48_cases'),
         'frees_to_global_heap': _owner('free_cases')}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'destroys_heap_buffer_owner_directly': ['bb_mutant_does_nothing', 'bb_mutant_heap_buffer_owner_handle_snapshot_directly', 'bb_mutant_heap_buffer_owner_vtable_late_directly', 'bb_mutant_heap_buffer_owner_whole_state_directly', 'bb_mutant_heap_buffer_owner_no_release_directly'],
    'destroys_heap_buffer_owner_through_base_at_0x38': ['bb_mutant_does_nothing', 'bb_mutant_heap_buffer_owner_handle_snapshot_base_0x38', 'bb_mutant_heap_buffer_owner_vtable_late_base_0x38', 'bb_mutant_heap_buffer_owner_whole_state_base_0x38', 'bb_mutant_heap_buffer_owner_no_release_base_0x38'],
    'destroys_heap_buffer_owner_through_base_at_0x48': ['bb_mutant_does_nothing', 'bb_mutant_heap_buffer_owner_handle_snapshot_base_0x48', 'bb_mutant_heap_buffer_owner_vtable_late_base_0x48', 'bb_mutant_heap_buffer_owner_whole_state_base_0x48', 'bb_mutant_heap_buffer_owner_no_release_base_0x48'],
    'frees_to_global_heap': ['bb_mutant_does_nothing', 'bb_mutant_global_heap_frees_null'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
