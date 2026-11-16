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

frees_8_byte_block_through_0x05ac8288, frees_sized_block_aligned_8_through_0x05ac8288,
allocates_aligned_8_through_0x05ac8288, allocates_with_alignment_through_0x05ac8288,
resets_with_0x02c375e0_and_returns_object, releases_value_at_0x8_with_0x005c14d0_then_clears_kind
and equals_by_kind_and_16_bytes_at_0x10 (allocator_forward_and_equality_families.cpp): the copies
of seven short functions. Their cases are allocator_forward_and_equality_family_cases.py's, at
each member's own address.
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


def _forward(name: str):
    return getattr(_load('allocator_forward_and_equality_family_cases'), name)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'frees_8_byte_block_through_0x05ac8288': _forward('free_8_cases'),
         'frees_sized_block_aligned_8_through_0x05ac8288': _forward('free_sized_cases'),
         'allocates_aligned_8_through_0x05ac8288': _forward('allocate_8_cases'),
         'allocates_with_alignment_through_0x05ac8288': _forward('allocate_cases'),
         'resets_with_0x02c375e0_and_returns_object': _forward('reset_cases'),
         'releases_value_at_0x8_with_0x005c14d0_then_clears_kind': _forward('release_cases'),
         'equals_by_kind_and_16_bytes_at_0x10': _forward('equal_cases')}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'frees_8_byte_block_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_free_8_bytes_aligned_16', 'bb_mutant_free_8_bytes_size_16', 'bb_mutant_free_8_bytes_global_address'],
    'frees_sized_block_aligned_8_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_free_sized_aligned_16', 'bb_mutant_free_sized_fixed_size_8', 'bb_mutant_free_sized_swapped', 'bb_mutant_free_8_bytes_global_address'],
    'allocates_aligned_8_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_allocate_8_aligned_16', 'bb_mutant_allocate_8_result_dropped', 'bb_mutant_allocate_8_global_address'],
    'allocates_with_alignment_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_allocate_fixed_alignment_8', 'bb_mutant_allocate_swapped', 'bb_mutant_allocate_result_dropped'],
    'resets_with_0x02c375e0_and_returns_object': ['bb_mutant_does_nothing', 'bb_mutant_reset_returns_callee_result', 'bb_mutant_reset_skipped'],
    'releases_value_at_0x8_with_0x005c14d0_then_clears_kind': ['bb_mutant_does_nothing', 'bb_mutant_release_value_read_again', 'bb_mutant_release_clears_first', 'bb_mutant_release_keeps_kind'],
    'equals_by_kind_and_16_bytes_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_equal_whole_kind', 'bb_mutant_equal_keys_read_first', 'bb_mutant_equal_table_read_first', 'bb_mutant_equal_whole_answer', 'bb_mutant_equal_short_key', 'bb_mutant_equal_always_compares'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
