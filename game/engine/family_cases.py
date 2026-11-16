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

forwards_to_referenced_node_slot_0x28, posts_event_9_when_accepted, syncs_selected_id_and_index,
selects_entry_id_by_index, releases_counted_reference_at_0x20 and registers_new_entry_at_0x30:
the copies of six virtual methods (virtual_method_families.cpp). Their cases are
virtual_method_family_cases.py's, at each member's own address.
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


def _virtual(name: str):
    return getattr(_load('virtual_method_family_cases'), name)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'forwards_to_referenced_node_slot_0x28': _virtual('forward_cases'),
         'posts_event_9_when_accepted': _virtual('event_cases'),
         'syncs_selected_id_and_index': _virtual('sync_cases'),
         'selects_entry_id_by_index': _virtual('select_cases'),
         'releases_counted_reference_at_0x20': _virtual('release_cases'),
         'registers_new_entry_at_0x30': _virtual('register_cases')}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'forwards_to_referenced_node_slot_0x28': ['bb_mutant_does_nothing', 'bb_mutant_node_forward_unsigned_index', 'bb_mutant_node_forward_drops_flag'],
    'posts_event_9_when_accepted': ['bb_mutant_does_nothing', 'bb_mutant_event_flags_read_before_base', 'bb_mutant_event_accepts_whole_register'],
    'syncs_selected_id_and_index': ['bb_mutant_does_nothing', 'bb_mutant_selected_id_last_match', 'bb_mutant_selected_id_not_stored'],
    'selects_entry_id_by_index': ['bb_mutant_does_nothing', 'bb_mutant_entry_index_inclusive', 'bb_mutant_entry_index_ignores_lock'],
    'releases_counted_reference_at_0x20': ['bb_mutant_does_nothing', 'bb_mutant_counted_release_no_reload', 'bb_mutant_counted_release_weak_zero', 'bb_mutant_counted_release_keeps_pointer'],
    'registers_new_entry_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_entry_registration_no_reload', 'bb_mutant_entry_registration_skips_null_entry'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
