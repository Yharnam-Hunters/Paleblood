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

reallocates_array_of_8_byte_4_aligned_elements, reallocates_array_of_16_byte_elements,
reallocates_array_of_24_byte_handle_records, erases_range_of_24_byte_handle_records and
resizes_heap_array_of_8_byte_elements: the copies of an engine array's reallocation for three
element types, of a range erase from an array of the third, and of a resize of an array whose buffer comes from the heap at 0x057e7320
(array_resize_families.cpp). Their cases are array_resize_family_cases.py's, at each member's
own address.
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


def _resize(name: str):
    return getattr(_load('array_resize_family_cases'), name)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'reallocates_array_of_8_byte_4_aligned_elements': _resize('reallocation_cases')(8),
         'reallocates_array_of_16_byte_elements': _resize('reallocation_cases')(16),
         'reallocates_array_of_24_byte_handle_records': _resize('handle_record_cases'),
         'erases_range_of_24_byte_handle_records': _resize('erase_cases'),
         'resizes_heap_array_of_8_byte_elements': _resize('heap_resize_cases')}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'reallocates_array_of_8_byte_4_aligned_elements': ['bb_mutant_does_nothing', 'bb_mutant_array64_reserve_8_aligned', 'bb_mutant_array64_4_aligned_reserve_keeps_old_buffer'],
    'reallocates_array_of_16_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array128_reserve_as_8_byte_elements', 'bb_mutant_array128_reserve_keeps_old_buffer'],
    'reallocates_array_of_24_byte_handle_records': ['bb_mutant_does_nothing', 'bb_mutant_handle_records_snapshot_ends', 'bb_mutant_handle_records_reload_copy_end', 'bb_mutant_handle_records_free_early', 'bb_mutant_handle_records_without_destroy'],
    'erases_range_of_24_byte_handle_records': ['bb_mutant_does_nothing', 'bb_mutant_erase_records_reload_assign_end', 'bb_mutant_erase_records_snapshot_end', 'bb_mutant_erase_records_whole_snapshot_begin', 'bb_mutant_erase_records_without_destroy'],
    'resizes_heap_array_of_8_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_heap_array_grows_to_count', 'bb_mutant_heap_array_never_shrinks', 'bb_mutant_heap_array_rounds_to_8', 'bb_mutant_heap_array_allocates_for_array'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
