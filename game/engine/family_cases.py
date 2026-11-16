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

dispatches_on_slot_match_with_member_0x68/_0x70 and visits_base_pairs_and_entries_at_0x70/_0x78:
four scaleform families, two pairs that differ only in one member offset; their cases are in
game/engine/tests/scaleform_visit_dispatch_cases.py and their control mutants in
game/engine/tests/scaleform_visit_dispatch_mutants.c.
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


def scaleform_cases(kind: str, offset: int):
    """The cases of tests/scaleform_visit_dispatch_cases.py's KIND_cases for one member offset."""
    def build(address: str, value: int | None) -> list[dict]:
        return getattr(_load('tests/scaleform_visit_dispatch_cases'), f'{kind}_cases')(address, offset)
    return build


CASES = {
    'initialize_static_wide_string': static_wide_string_cases,
    'dispatches_on_slot_match_with_member_0x68': scaleform_cases('dispatch', 0x68),
    'dispatches_on_slot_match_with_member_0x70': scaleform_cases('dispatch', 0x70),
    'visits_base_pairs_and_entries_at_0x70': scaleform_cases('visit', 0x70),
    'visits_base_pairs_and_entries_at_0x78': scaleform_cases('visit', 0x78),
}

# Each scaleform control moves one read across the call or store that can change it
# (game/engine/tests/scaleform_visit_dispatch_mutants.c).
SLOT_FLAWS = ['first_slot_before_resolve', 'handle_not_reloaded', 'slots_after_resolve', 'slots_kept_for_second', 'kind_kept_for_second', 'second_slot_before_resolve']
VISIT_FLAWS = ['entries_before_calls', 'entries_read_once', 'count_every_entry']

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    'dispatches_on_slot_match_with_member_0x68': ['bb_mutant_does_nothing'] + [f'bb_mutant_slot_dispatch_0x68_{flaw}' for flaw in SLOT_FLAWS],
    'dispatches_on_slot_match_with_member_0x70': ['bb_mutant_does_nothing'] + [f'bb_mutant_slot_dispatch_0x70_{flaw}' for flaw in SLOT_FLAWS],
    'visits_base_pairs_and_entries_at_0x70': ['bb_mutant_does_nothing'] + [f'bb_mutant_visit_0x70_{flaw}' for flaw in VISIT_FLAWS],
    'visits_base_pairs_and_entries_at_0x78': ['bb_mutant_does_nothing'] + [f'bb_mutant_visit_0x78_{flaw}' for flaw in VISIT_FLAWS],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
