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

removes_debug_menu_entry_keyed_by_object, formats_one_of_three_texts_for_slot_0x80 and
releases_counted_member_at_0x30_then_base: their cases are in
game/engine/tests/debug_menu_release_cases.py and their control mutants in
game/engine/tests/debug_menu_release_mutants.c.
"""
from __future__ import annotations

import copy
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name: str):
    spec = importlib.util.spec_from_file_location(f'engine_{name.replace("/", "_")}', os.path.join(HERE, f'{name}.py'))
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


def debug_menu_release_cases(kind: str):
    """The cases of tests/debug_menu_release_cases.py's KIND_cases."""
    def build(address: str, value: int | None) -> list[dict]:
        return getattr(_load('tests/debug_menu_release_cases'), f'{kind}_cases')(address)
    return build


CASES = {
    'initialize_static_wide_string': static_wide_string_cases,
    'removes_debug_menu_entry_keyed_by_object': debug_menu_release_cases('menu_entry'),
    'formats_one_of_three_texts_for_slot_0x80': debug_menu_release_cases('text'),
    'releases_counted_member_at_0x30_then_base': debug_menu_release_cases('counted_release'),
}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Each control mutant below moves one read across the call that changes it
    # (game/engine/tests/debug_menu_release_mutants.c).
    'removes_debug_menu_entry_keyed_by_object': ['bb_mutant_does_nothing', 'bb_mutant_menu_entry_manager_reloaded'],
    'formats_one_of_three_texts_for_slot_0x80': ['bb_mutant_does_nothing', 'bb_mutant_text_slot_before_format'],
    'releases_counted_member_at_0x30_then_base': ['bb_mutant_does_nothing', 'bb_mutant_counted_release_count_not_reloaded'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
