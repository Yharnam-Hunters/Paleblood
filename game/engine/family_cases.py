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

The kind-sized storage families (game/engine/kind_sized_storage.cpp) run every kind and count rule
(kinds 4 and 6 with and without the ignored low bit, other kinds, a second answer that differs from
the first, empty, negative and wrapping counts), kind calls that change the count, the vtable or
the allocator pointer (the order controls) and failing allocations.
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



# The kind-sized storage families (game/engine/kind_sized_storage.cpp). The object lives in
# writable image memory just past the allocator pointer, so a kind call can change the count, the
# vtable or the allocator pointer through its object argument (stub writes at fixed offsets).
KIND_ALLOCATE = '0x02fa1b80'
KIND_FREE = '0x02fa1c20'
KIND_ALLOCATOR = 0x05ac8288
KIND_OBJECT = 0x05ac82c0
KIND_CALL = '0x00500100'        # what the kind slot (vtable +0x88) points at: a stub
KIND_OTHER_CALL = '0x00500180'  # the slot of the vtable a kind call may install
KIND_SLOT = 0x88
KIND_COUNT = 0x18


def kind_holder(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 19 + i) & 0xff for i in range(size)).hex()}


def kind_word(value: int, size: int = 4) -> str:
    return (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little').hex()


def kind_case(address: str, case_id: str, returns: str, kinds: list[int], count: int,
              changes: dict | None = None) -> dict:
    """An object of COUNT entries whose kind calls answer KINDS in turn (the second only when the
    first is not kind 4); CHANGES: what the first kind call writes ('count', 'vtable',
    'allocator')."""
    changes = changes or {}
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': returns,
            'buffers': {'vtable': kind_holder(7, KIND_SLOT + 8), 'other_vtable': kind_holder(9, KIND_SLOT + 8),
                        'allocator': kind_holder(11, 0x20), 'other_allocator': kind_holder(13, 0x20)},
            'memory': [{'addr': hex(KIND_OBJECT), 'bytes': kind_holder(17, 0x20)['bytes']},
                       {'addr': hex(KIND_OBJECT), 'pointer': 'vtable'},
                       {'addr': hex(KIND_OBJECT + KIND_COUNT), 'bytes': kind_word(count)},
                       {'addr': f'buf:vtable+0x{KIND_SLOT:x}', 'guest': KIND_CALL},
                       {'addr': f'buf:other_vtable+0x{KIND_SLOT:x}', 'guest': KIND_OTHER_CALL},
                       {'addr': hex(KIND_ALLOCATOR), 'pointer': 'allocator'}],
            'imports': [], 'stubs': [],
            'args': {'rdi': f'guest:{KIND_OBJECT:#x}', 'rsi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}

    writes = []
    if 'count' in changes:
        writes.append({'arg': 0, 'offset': KIND_COUNT, 'bytes': kind_word(changes['count'])})
    if changes.get('vtable'):
        writes.append({'arg': 0, 'offset': 0, 'pointer': 'other_vtable'})
    if changes.get('allocator'):
        writes.append({'arg': 0, 'offset': KIND_ALLOCATOR - KIND_OBJECT, 'pointer': 'other_allocator'})
    asked = kinds[:1] if (kinds[0] & 0xfffffffe) == 4 or len(kinds) == 1 else kinds[:2]
    for n, kind in enumerate(asked):
        stub = {'address': KIND_OTHER_CALL if n and changes.get('vtable') else KIND_CALL, 'argc': 2,
                'argmem': [{'arg': 0, 'offset': KIND_COUNT, 'size': 4}],
                'ret': hex(0x1234567800000000 | (kind & 0xffffffff))}
        if n == 0 and writes:
            stub['writes'] = writes
        case['stubs'].append(stub)
    return case


# (id, kinds, count, changes): every kind and count rule, and kind calls that change the count,
# the vtable or the allocator pointer.
KIND_SIZE_CASES = [
    ('kind4', [4], 7, None), ('kind5', [5], 7, None), ('kind6', [6, 6], 7, None),
    ('kind7', [7, 7], 7, None), ('kind0', [0, 0], 7, None), ('kind2', [2, 3], 7, None),
    ('kind6_then_4', [6, 4], 7, None), ('kind0_then_6', [0, 6], 7, None),
    ('kind1_then_7', [1, 7], 5, None), ('kind_high', [0xfffffffe, 0xfffffffe], 7, None),
    ('kind4_empty', [4], 0, None), ('kind6_empty', [6, 6], 0, None), ('kind0_one', [0, 0], 1, None),
    ('kind4_negative', [4], -1, None), ('kind6_negative', [6, 6], -3, None), ('kind0_negative', [0, 0], -2, None),
    ('kind4_wraps', [4], 0x55555556, None), ('kind6_wraps', [6, 6], 0x55555556, None),
    ('kind0_largest', [0, 0], 0x7fffffff, None),
    ('kind4_count_changes', [4], 7, {'count': 11}), ('kind6_count_changes', [6, 6], 7, {'count': 11}),
    ('kind0_count_changes', [0, 0], 9, {'count': 3}),
    ('vtable_changes', [0, 6], 7, {'vtable': True}), ('vtable_changes_kind4', [6, 4], 7, {'vtable': True}),
    ('allocator_changes', [4], 7, {'allocator': True}), ('allocator_changes_kind6', [0, 6], 7, {'allocator': True}),
]


def kind_free_cases(address: str, value: int | None) -> list[dict]:
    """The sized free (a stub comparing the allocator, the buffer, the size and the alignment)."""
    out = []
    for case_id, kinds, count, changes in KIND_SIZE_CASES:
        case = kind_case(address, case_id, 'void', kinds, count, changes)
        case['stubs'].append({'address': KIND_FREE, 'argc': 4, 'ret': '0x0'})
        out.append(case)
    return out


def kind_allocate_cases(set_count: bool):
    """The allocation (a stub comparing the allocator, the size and the alignment) succeeding, or
    failing (the object left alone, false). SET_COUNT: the count comes in rsi (high bits ignored)
    and the object's count is something else."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, kinds, count, changes in KIND_SIZE_CASES + [('fails', [6, 6], 7, None), ('fails_kind4', [4], 5, None)]:
            case = kind_case(address, case_id, 'i8', kinds, count + 0x101 if set_count else count, changes)
            if set_count:
                case['args']['rsi'] = hex(0xdeadbeef00000000 | (count & 0xffffffff))
            case['buffers']['storage'] = kind_holder(19, 0x10)
            ret = '0x0' if case_id.startswith('fails') else 'buf:storage'
            case['stubs'].append({'address': KIND_ALLOCATE, 'argc': 3, 'ret': ret})
            out.append(case)
        return out
    return cases



CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'frees_kind_sized_vec4_storage_through_0x05ac8288': kind_free_cases,
         'allocates_kind_sized_dword_storage_through_0x05ac8288': kind_allocate_cases(False),
         'sets_count_and_allocates_kind_sized_dword_storage_through_0x05ac8288': kind_allocate_cases(True)}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    'frees_kind_sized_vec4_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_kind_storage_free_reads_allocator_late', 'bb_mutant_kind_storage_free_reads_count_early', 'bb_mutant_kind_storage_free_asks_once', 'bb_mutant_kind_storage_free_caches_vtable', 'bb_mutant_kind_storage_free_unrounded'],
    'allocates_kind_sized_dword_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_kind_storage_allocate_reads_allocator_late', 'bb_mutant_kind_storage_allocate_reads_count_early', 'bb_mutant_kind_storage_allocate_asks_once', 'bb_mutant_kind_storage_allocate_caches_vtable', 'bb_mutant_kind_storage_allocate_no_entry_offset', 'bb_mutant_kind_storage_allocate_true_on_failure'],
    'sets_count_and_allocates_kind_sized_dword_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_kind_storage_set_count_skipped', 'bb_mutant_kind_storage_set_count_after_kind', 'bb_mutant_kind_storage_allocate_true_on_failure'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
