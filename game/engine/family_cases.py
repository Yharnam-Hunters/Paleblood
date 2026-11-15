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


SHARED_DESTRUCTOR = '0x024b5a60'
OPERATOR_DELETE = '0x02fbe588'
VIRTUAL_DESTRUCTOR = '0x00400240'   # a stand-in for the object's slot 0x28, stubbed
DESTRUCTOR_SLOT = 0x28


def object_buffer(seed: int, size: int = 0x40) -> dict:
    return {'size': size, 'bytes': bytes((seed * 11 + i) & 0xff for i in range(size)).hex()}


OBJECT_ALLOCATOR = '0x057e7320'    # the global that points at the allocator
ALLOCATOR_FREE = '0x00400280'      # a stand-in for the allocator's slot 0x68, stubbed


def destroy_then_delete_cases(destructor: str | None, freed: bool = False):
    """The destructor, then operator delete (or, FREED, slot 0x68 of the allocator the global
    points at, comparing the allocator and the object), all stubs comparing the object; two
    seeds (the object's contents must come through untouched). DESTRUCTOR None: an empty
    function, run unstubbed."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for seed in (3, 5):
            case = {'schema': 1, 'address': address, 'id': f'object_{seed}', 'returns': 'i64' if freed else 'void',
                    'buffers': {'object': object_buffer(seed)}, 'memory': [], 'imports': [],
                    'stubs': [{'address': destructor, 'argc': 1, 'ret': seed}] if destructor else [],
                    'args': {'rdi': 'buf:object', 'rsi': hex(0x1111111111111111 * (seed & 0xf)),
                             'rax': '0x5aa5c33c0ff0e11e'}}
            if freed:
                case['buffers'].update({'allocator': object_buffer(seed + 1, 0x20), 'allocator_table': {'size': 0x80}})
                case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                                   {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                                   {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
                case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
            else:
                case['stubs'].append({'address': OPERATOR_DELETE, 'argc': 1, 'ret': 0})
            out.append(case)
        return out
    return cases


destroys_then_deletes_cases = destroy_then_delete_cases(SHARED_DESTRUCTOR)


def virtual_delete_case(case_id: str, address: str, flag: int, seed: int) -> dict:
    """An object whose table's slot 0x28 is a stub comparing the object; FLAG is the whole second
    argument (only its low byte decides operator delete, a stub comparing the object)."""
    stubs = [{'address': VIRTUAL_DESTRUCTOR, 'argc': 1, 'ret': seed << 8}]
    if flag & 0xff:
        stubs.append({'address': OPERATOR_DELETE, 'argc': 1, 'ret': 0})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'object': object_buffer(seed), 'table': {'size': 0x40}},
            'memory': [{'addr': 'buf:object+0x0', 'pointer': 'table'},
                       {'addr': f'buf:table+0x{DESTRUCTOR_SLOT:x}', 'guest': VIRTUAL_DESTRUCTOR}],
            'imports': [], 'stubs': stubs,
            'args': {'rdi': 'buf:object', 'rsi': hex(flag), 'rdx': '0x2222222222222222',
                     'rax': '0x5aa5c33c0ff0e11e'}}


def virtual_delete_cases(address: str, value: int | None) -> list[dict]:
    """The flag 1, 0, 0x100 (a set bit above the low byte: no delete) and 0xffffff80."""
    return [virtual_delete_case('delete', address, 1, 3),
            virtual_delete_case('keep', address, 0, 5),
            virtual_delete_case('keep_high_bits', address, 0x100, 7),
            virtual_delete_case('delete_high_bits', address, 0xffffff80, 11)]


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'destroys_then_deletes': destroys_then_deletes_cases,
         'calls_virtual_0x28_then_deletes_if_asked': virtual_delete_cases,
         'destroys_with_0x01402180_then_deletes': destroy_then_delete_cases('0x01402180'),
         'destroys_with_0x010e7ca0_then_deletes': destroy_then_delete_cases('0x010e7ca0'),
         'destroys_with_0x01401dc0_then_deletes': destroy_then_delete_cases('0x01401dc0'),
         'destroys_with_0x01640e70_then_deletes': destroy_then_delete_cases('0x01640e70'),
         'destroys_with_0x0139c4f0_then_deletes': destroy_then_delete_cases('0x0139c4f0'),
         'destroys_with_0x007164e0_then_frees': destroy_then_delete_cases('0x007164e0', freed=True),
         'destroys_with_0x005696e0_then_frees': destroy_then_delete_cases('0x005696e0', freed=True),
         'destroys_with_0x006fa840_then_frees': destroy_then_delete_cases('0x006fa840', freed=True),
         'destroys_with_0x0045a480_then_frees': destroy_then_delete_cases('0x0045a480', freed=True),
         'calls_empty_destructor_then_deletes': destroy_then_delete_cases(None),
}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # No call; and (virtual 0x28) the destructor called but the object never deleted.
    'destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_virtual_0x28_then_deletes_if_asked': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                                 'bb_mutant_virtual_0x28_never_deletes'],
    'destroys_with_0x01402180_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x010e7ca0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x01401dc0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x01640e70_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0139c4f0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x007164e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x005696e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x006fa840_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0045a480_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_empty_destructor_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
