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


SMALL_HEAP_OWNING = '0x0247b720'
SMALL_PATH_TABLE_FIND = '0x013e68f0'
SMALL_PATH_TABLE_RELEASE = '0x013e6ae0'
SMALL_COMPARE_MEMORY = '0x02fbe638'
SMALL_INTERFACE_ID = '0x04b2f320'
SMALL_FATAL_ERROR = '0x024b55b0'
SMALL_FATAL_ERROR_MESSAGE = '0x04b38390'
SMALL_RESOLVE = '0x02c4f560'
SMALL_TAGGED_STRING_RELEASE = '0x00836840'
SMALL_TAGGED_STRING_OWNER_BASE = '0x00d5f020'
SMALL_FLAGGED_RELEASE = '0x0271db70'          # slot +0x18 of the table at 0x057b9530
SMALL_FLAGGED_BASE = '0x0271a600'
SMALL_OPERATOR_DELETE = '0x02fbe588'
SMALL_HAVOK_HEAP = '0x058018b0'
SMALL_RELEASE_ELEMENT = '0x02fa7940'
SMALL_STACK_GUARD_SLOT = '0x057e3be0'
# Stand-ins for the virtual functions fake tables point at.
SMALL_DESTROY, SMALL_HEAP_FREE, SMALL_SLOT = '0x00500100', '0x00500200', '0x00500300'


def seeded(size: int, seed: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}


def small_case(address: str, case_id: str, returns: str, args: dict, buffers: dict) -> dict:
    full = {'rdi': '0x1111111111111111', 'rsi': '0x2222222222222222', 'rdx': '0x3333333333333333',
            'rax': '0x5aa5c33c0ff0e11e'}
    full.update(args)
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': returns, 'buffers': buffers,
            'memory': [], 'imports': [], 'stubs': [], 'args': full}


def add_destroy_and_free(case: dict, name: str, seed: int) -> None:
    """NAME (a buffer) is destroyed through its table's second slot and freed through the heap the
    owning-heap stub returns; the destructor stub writes into the object, so its order shows."""
    case['buffers'].update({f'{name}_table': {'size': 0x10}, 'heap': seeded(0x10, seed + 7),
                            'heap_table': {'size': 0x78}})
    case['memory'] += [{'addr': f'buf:{name}+0x0', 'pointer': f'{name}_table'},
                       {'addr': f'buf:{name}_table+0x8', 'guest': SMALL_DESTROY},
                       {'addr': 'buf:heap+0x0', 'pointer': 'heap_table'},
                       {'addr': 'buf:heap_table+0x70', 'guest': SMALL_HEAP_FREE}]
    case['stubs'] += [{'address': SMALL_HEAP_OWNING, 'argc': 1, 'argi': [0], 'ret': 'buf:heap'},
                      {'address': SMALL_DESTROY, 'argc': 1, 'argi': [0],
                       'writes': [{'arg': 0, 'offset': 8, 'bytes': f'{0xd0 + seed:02x}' * 8}]},
                      {'address': SMALL_HEAP_FREE, 'argc': 2, 'argi': [0, 1], 'ret': 0}]


def owning_heap_delete_cases(address: str, value: int | None) -> list[dict]:
    """Two objects (destroyed, then freed through the heap that owns them) and null (nothing)."""
    out = []
    for seed in (3, 5):
        case = small_case(address, f'object_{seed}', 'void', {'rdi': 'buf:object'}, {'object': seeded(0x20, seed)})
        add_destroy_and_free(case, 'object', seed)
        out.append(case)
    out.append(small_case(address, 'null', 'void', {'rdi': '0x0'}, {}))
    return out


def path_entry_release_cases(address: str, value: int | None) -> list[dict]:
    """The release (a stub comparing the table at +0x68 and the entry) says last use or not, for an
    entry and for null; only a set entry at its last use is destroyed and freed."""
    out = []
    for case_id, entry, last_use, seed in (('last_use', True, 1, 3), ('still_used', True, 0, 5),
                                           ('null_last_use', False, 1, 7), ('null_still_used', False, 0, 9)):
        case = small_case(address, case_id, 'void', {'rdi': 'buf:holder', 'rsi': 'buf:entry' if entry else '0x0'},
                          {'holder': seeded(0x90, seed)})
        case['stubs'].append({'address': SMALL_PATH_TABLE_RELEASE, 'argc': 2, 'argi': [0, 1], 'ret': last_use})
        if entry:
            case['buffers']['entry'] = seeded(0x68, seed + 1)
            if last_use:
                add_destroy_and_free(case, 'entry', seed)
        out.append(case)
    return out


def path_entry_find_cases(address: str, value: int | None) -> list[dict]:
    """The find (a stub comparing the table at +0x68 and the key's text, hash and stale byte; the
    key is on the stack) returns an entry or null, which comes back."""
    out = []
    for case_id, found, seed in (('found', True, 3), ('not_found', False, 5)):
        case = small_case(address, case_id, 'i64', {'rdi': 'buf:holder', 'rsi': 'buf:text'},
                          {'holder': seeded(0x90, seed), 'text': seeded(0x20, seed + 1),
                           'guard': seeded(8, seed + 2)})
        case['memory'].append({'addr': SMALL_STACK_GUARD_SLOT, 'pointer': 'guard'})
        if found:
            case['buffers']['entry'] = seeded(0x60, seed + 3)
        case['stubs'].append({'address': SMALL_PATH_TABLE_FIND, 'argc': 2, 'argi': [0],
                              'argmem': [{'arg': 1, 'size': 0xd}], 'ret': 'buf:entry' if found else 0})
        out.append(case)
    return out


def interface_id_cases(address: str, value: int | None) -> list[dict]:
    """The id comparison (a stub comparing both pointers and the size) says equal (a reference is
    added and the object stored), greater or less (nothing); the result is the same."""
    out = []
    for case_id, compared, seed in (('equal', 0, 3), ('greater', 1, 5), ('less', 0xffffffff, 7)):
        case = small_case(address, case_id, 'i32', {'rdi': 'buf:object', 'rsi': 'buf:id', 'rdx': 'buf:out'},
                          {'object': seeded(0x18, seed), 'object_table': {'size': 0x10},
                           'id': seeded(0x10, seed + 1), 'out': seeded(8, seed + 2)})
        case['memory'] += [{'addr': 'buf:object+0x0', 'pointer': 'object_table'},
                           {'addr': 'buf:object_table+0x8', 'guest': SMALL_SLOT}]
        case['stubs'].append({'address': SMALL_COMPARE_MEMORY, 'argc': 3, 'argi': [0, 1, 2], 'ret': compared})
        if compared == 0:
            case['stubs'].append({'address': SMALL_SLOT, 'argc': 1, 'argi': [0], 'ret': 0})
        out.append(case)
    return out


def fatal_error_cases(address: str, value: int | None) -> list[dict]:
    """The report (a stub comparing the file, the line and the message)."""
    out = []
    for seed in (3, 5):
        case = small_case(address, f'report_{seed}', 'void', {'rdi': f'0x{seed:x}{seed:x}{seed:x}'}, {})
        case['stubs'].append({'address': SMALL_FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2], 'ret': 0})
        out.append(case)
    return out


def resolve_slot_cases(slot: int):
    """Kinds and indices (the index sign-extended) resolved by a stub comparing the context, the
    kind and the index; the resolved object's slot (a stub comparing it and the context) returns
    the result."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, kind, index, seed in (('kind_2', 2, 0xfffd, 3), ('kind_8001', 0x8001, 0x7fff, 5),
                                           ('index_8000', 0x7, 0x8000, 7)):
            reference = bytearray.fromhex(seeded(0x10, seed)['bytes'])
            reference[8:12] = kind.to_bytes(2, 'little') + index.to_bytes(2, 'little')
            case = small_case(address, case_id, 'i64', {'rdi': 'buf:reference', 'rsi': 'buf:context'},
                              {'reference': {'size': 0x10, 'bytes': reference.hex()},
                               'context': seeded(0x60, seed + 1), 'resolved': seeded(0x20, seed + 2),
                               'resolved_table': {'size': 0x60}})
            case['memory'] += [{'addr': 'buf:resolved+0x0', 'pointer': 'resolved_table'},
                               {'addr': f'buf:resolved_table+0x{slot:x}', 'guest': SMALL_SLOT}]
            case['stubs'] += [{'address': SMALL_RESOLVE, 'argc': 3, 'argi': [0, 1, 2], 'ret': 'buf:resolved'},
                              {'address': SMALL_SLOT, 'argc': 2, 'argi': [0, 1], 'ret': seed << 36 | 0x5107}]
            out.append(case)
        return out
    return cases


def tagged_string_destructor_cases(address: str, value: int | None) -> list[dict]:
    """The tagged string release and the base destructor are stubs comparing their argument."""
    out = []
    for seed in (3, 5):
        case = small_case(address, f'object_{seed}', 'void', {'rdi': 'buf:object'}, {'object': seeded(0x48, seed)})
        case['stubs'] += [{'address': SMALL_TAGGED_STRING_RELEASE, 'argc': 1, 'argi': [0], 'ret': 0},
                          {'address': SMALL_TAGGED_STRING_OWNER_BASE, 'argc': 1, 'argi': [0], 'ret': 0}]
        out.append(case)
    return out


def flagged_destructor_cases(deletes: bool):
    """The flag bit set or clear; the slot +0x18 call and the base destructor are stubs comparing
    the object and its first 0x10 bytes at the call (the table stored, then the flag cleared);
    DELETES: operator delete is a stub comparing the object."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, flags, seed in (('flag_set', 0xff, 3), ('flag_clear', 0x3a, 5)):
            case = small_case(address, case_id, 'void', {'rdi': 'buf:object'}, {'object': seeded(0x20, seed)})
            case['memory'].append({'addr': 'buf:object+0xb', 'bytes': f'{flags:02x}'})
            compared = [{'arg': 0, 'size': 0x10}]
            case['stubs'] += [{'address': SMALL_FLAGGED_RELEASE, 'argc': 1, 'argi': [0], 'argmem': compared, 'ret': 0},
                              {'address': SMALL_FLAGGED_BASE, 'argc': 1, 'argi': [0], 'argmem': compared, 'ret': 0}]
            if deletes:
                case['stubs'].append({'address': SMALL_OPERATOR_DELETE, 'argc': 1, 'argi': [0], 'ret': 0})
            out.append(case)
        return out
    return cases


def hk_array_cases(address: str, value: int | None) -> list[dict]:
    """Owned buffers (capacity 8, 8 with the 0x40000000 flag, 0) are freed through the Havok heap's
    slot +0x28 (a stub comparing the heap, the data and the low dword of the size); a buffer the
    array does not own (0x80000008) is not."""
    out = []
    for case_id, capacity, seed in (('owned_8', 8, 3), ('owned_flagged', 0x40000008, 5),
                                    ('owned_empty', 0, 7), ('not_owned', 0x80000008, 9)):
        case = small_case(address, case_id, 'void', {'rdi': 'buf:array'},
                          {'array': seeded(0x18, seed), 'data': seeded(0x20, seed + 1),
                           'heap_table': {'size': 0x30}})
        case['memory'] += [{'addr': 'buf:array+0x0', 'pointer': 'data'},
                           {'addr': 'buf:array+0x8', 'bytes': (5).to_bytes(4, 'little').hex() + capacity.to_bytes(4, 'little').hex()},
                           {'addr': SMALL_HAVOK_HEAP, 'pointer': 'heap_table'},
                           {'addr': 'buf:heap_table+0x28', 'guest': SMALL_HEAP_FREE}]
        if capacity < 0x80000000:
            case['stubs'].append({'address': SMALL_HEAP_FREE, 'argc': 2, 'argi': [0, 1], 'argu32': [2], 'ret': 0})
        out.append(case)
    return out


def element_array_cases(address: str, value: int | None) -> list[dict]:
    """Three, one and no elements, each released by a stub comparing it; the end becomes the begin."""
    out = []
    for count, seed in ((3, 3), (1, 5), (0, 7)):
        case = small_case(address, f'elements_{count}', 'void', {'rdi': 'buf:array'},
                          {'array': seeded(0x30, seed), 'items': seeded(0x48, seed + 1)})
        case['memory'] += [{'addr': 'buf:array+0x18', 'pointer': 'items'},
                           {'addr': 'buf:array+0x20', 'pointer': f'items+0x{count * 0x18:x}'}]
        case['stubs'] += [{'address': SMALL_RELEASE_ELEMENT, 'argc': 1, 'argi': [0], 'ret': 0} for _ in range(count)]
        out.append(case)
    return out


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'destroys_and_frees_object_through_owning_heap': owning_heap_delete_cases,
         'releases_path_entry_then_deletes_it': path_entry_release_cases,
         'finds_path_entry_by_text': path_entry_find_cases,
         'adds_reference_on_interface_id_0x04b2f320': interface_id_cases,
         'reports_fatal_error_line_0xad': fatal_error_cases,
         'resolves_kind_and_index_then_calls_slot_0x38': resolve_slot_cases(0x38),
         'resolves_kind_and_index_then_calls_slot_0x58': resolve_slot_cases(0x58),
         'stores_vtable_releases_tagged_string_0x38_then_destroys': tagged_string_destructor_cases,
         'stores_vtable_calls_slot_0x18_clears_flag_then_destroys': flagged_destructor_cases(False),
         'stores_vtable_calls_slot_0x18_clears_flag_destroys_then_deletes': flagged_destructor_cases(True),
         'clears_hk_array_of_4_byte_elements': hk_array_cases,
         'releases_0x18_byte_elements_at_0x18_then_empties': element_array_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    'destroys_and_frees_object_through_owning_heap': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_path_entry_then_deletes_it': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'finds_path_entry_by_text': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adds_reference_on_interface_id_0x04b2f320': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'reports_fatal_error_line_0xad': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'resolves_kind_and_index_then_calls_slot_0x38': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'resolves_kind_and_index_then_calls_slot_0x58': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_releases_tagged_string_0x38_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_calls_slot_0x18_clears_flag_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_calls_slot_0x18_clears_flag_destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'clears_hk_array_of_4_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_0x18_byte_elements_at_0x18_then_empties': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
