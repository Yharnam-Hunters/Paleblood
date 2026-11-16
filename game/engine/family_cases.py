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

destroys_with_0x010f6190_then_deletes and the seven other destroys_with_..._then_deletes families
(destroy_then_delete_families.cpp): the copies of eight deleting destructors, one destructor each.
Their cases are destroy_then_delete_family_cases.py's, at each member's own address.
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


def _deleting(destroy: str):
    return _load('destroy_then_delete_family_cases').deleting_cases(destroy)


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'destroys_with_0x010f6190_then_deletes': _deleting('0x010f6190'),
         'destroys_with_0x02af2b00_then_deletes': _deleting('0x02af2b00'),
         'destroys_with_0x02af4240_then_deletes': _deleting('0x02af4240'),
         'destroys_with_0x02af6d40_then_deletes': _deleting('0x02af6d40'),
         'destroys_with_0x02af8220_then_deletes': _deleting('0x02af8220'),
         'destroys_with_0x02af9940_then_deletes': _deleting('0x02af9940'),
         'destroys_with_0x02afc240_then_deletes': _deleting('0x02afc240'),
         'destroys_with_0x02afee40_then_deletes': _deleting('0x02afee40')}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes neither call; each other mutant says in tests/family_mutants.c how it
    # differs from the family it is listed for.
    'destroys_with_0x010f6190_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x010f6190_kept', 'bb_mutant_deleting_dtor_0x010f6190_deletes_first'],
    'destroys_with_0x02af2b00_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af2b00_kept', 'bb_mutant_deleting_dtor_0x02af2b00_deletes_first'],
    'destroys_with_0x02af4240_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af4240_kept', 'bb_mutant_deleting_dtor_0x02af4240_deletes_first'],
    'destroys_with_0x02af6d40_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af6d40_kept', 'bb_mutant_deleting_dtor_0x02af6d40_deletes_first'],
    'destroys_with_0x02af8220_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af8220_kept', 'bb_mutant_deleting_dtor_0x02af8220_deletes_first'],
    'destroys_with_0x02af9940_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af9940_kept', 'bb_mutant_deleting_dtor_0x02af9940_deletes_first'],
    'destroys_with_0x02afc240_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02afc240_kept', 'bb_mutant_deleting_dtor_0x02afc240_deletes_first'],
    'destroys_with_0x02afee40_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02afee40_kept', 'bb_mutant_deleting_dtor_0x02afee40_deletes_first'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
