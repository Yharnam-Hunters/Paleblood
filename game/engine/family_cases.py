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

destroys_holder_of_shared_work (shared_work_holder_family.cpp): no work, user and reference
counts on both sides of 1 (and their extremes), and signal sets that clear the holder's work or
swap in another (the work is read again after the signal).

destroys_owner_of_wide_string_then_base_0x0139b630 and
deletes_owner_of_wide_string_after_base_0x0139b630 (wide_string_owner_family.cpp): the owned
flag clear or set (1, 0x80, 0xff), no string, capacities around the inline room and at the
extremes, and heap lookups that change the string's capacity or allocator (both read after it).

visits_children_of_list_at_0x40 and secondary_base_visits_children_of_list_at_0x40
(child_list_visit_family.cpp): no list, empty, negative and smallest counts, one to eight
children, and slot calls that swap the list or its array, change the count (read once) or a
later child.

selects_index_of_changed_id_with_0x01348fe0 (changed_id_index_family.cpp): no followed id, an
unchanged id, a match first, in the middle, last, none, an empty table, duplicates (the first
wins) and extreme ids.
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


# Generated fixtures for the families below, not game data. Every called function is a stub that
# compares its arguments; every written byte is compared.
def _pattern(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 31 + i * 11) & 0xff for i in range(size)).hex()}


def _le(value: int, size: int = 4) -> str:
    return (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little').hex()


# destroys_holder_of_shared_work (shared_work_holder_family.cpp). One block holds work A (+0x0),
# the holder (+0x80) and work B (+0xc0), so the signal's set, called with A's signal (+0x10), can
# rewrite the holder's work pointer through its argument.
HOLDER_SIGNAL_SET = '0x0247e780'
HOLDER_FATAL_ERROR = '0x024b55b0'
HOLDER_WORK_DESTROY = '0x00500440'   # what the works' vtable slot 0 points at
HOLDER_AT, HOLDER_OTHER_WORK = 0x80, 0xc0
HOLDER_SIGNAL, HOLDER_USERS, HOLDER_REFERENCES = 0x10, 0x30, 0x8


def holder_case(address: str, case_id: str, users: int | None, references: int,
                change: str | None = None, other_references: int = 1) -> dict:
    """USERS None: the holder has no work. CHANGE: what the signal's set stores in the holder's
    work pointer ('null' or 'other')."""
    memory = [{'addr': f'buf:block+{HOLDER_AT:#x}', 'guest': '0x00500460'}]
    stubs = []
    for base, refs in ((0, references), (HOLDER_OTHER_WORK, other_references)):
        memory.append({'addr': f'buf:block+{base:#x}', 'pointer': 'workvt'})
        memory.append({'addr': f'buf:block+{base + HOLDER_REFERENCES:#x}', 'bytes': _le(refs)})
        memory.append({'addr': f'buf:block+{base + HOLDER_USERS:#x}', 'bytes': _le(users or 0)})
    if users is None:
        memory.append({'addr': f'buf:block+{HOLDER_AT + 8:#x}', 'bytes': _le(0, 8)})
    else:
        memory.append({'addr': f'buf:block+{HOLDER_AT + 8:#x}', 'pointer': 'block'})
        work = 0
        if users <= 1:
            signal = {'address': HOLDER_SIGNAL_SET, 'argc': 2, 'argi': [0], 'argu8': [1], 'ret': '0x5a5a',
                      'argmem': [{'arg': 0, 'offset': HOLDER_USERS - HOLDER_SIGNAL, 'size': 4}]}
            offset = HOLDER_AT + 8 - HOLDER_SIGNAL
            if change == 'null':
                signal['writes'] = [{'arg': 0, 'offset': offset, 'bytes': _le(0, 8)}]
                work = None
            elif change == 'other':
                signal['writes'] = [{'arg': 0, 'offset': offset, 'pointer': f'block+{HOLDER_OTHER_WORK:#x}'}]
                work, references = HOLDER_OTHER_WORK, other_references
            stubs.append(signal)
        if work is not None:
            if references == 1:
                stubs.append({'address': HOLDER_WORK_DESTROY, 'argc': 1})
            elif references <= 0:
                stubs.append({'address': HOLDER_FATAL_ERROR, 'argc': 3})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'args': {'rdi': f'buf:block+{HOLDER_AT:#x}'},
            'buffers': {'block': _pattern(21, 0x100), 'workvt': _pattern(23, 0x10)},
            'memory': memory + [{'addr': 'buf:workvt+0x0', 'guest': HOLDER_WORK_DESTROY}],
            'imports': [], 'stubs': stubs}


# (id, users, references, change, other work's references)
HOLDER_CASES = [
    ('no_work', None, 1, None, 1), ('users2_refs2', 2, 2, None, 1), ('users2_refs1', 2, 1, None, 1),
    ('users1_refs2', 1, 2, None, 1), ('users1_refs1', 1, 1, None, 1), ('users0_refs3', 0, 3, None, 1),
    ('users_negative', -5, 1, None, 1), ('users_smallest', -0x80000000, 2, None, 1),
    ('users_largest', 0x7fffffff, 1, None, 1), ('refs0', 3, 0, None, 1), ('refs_negative', 1, -2, None, 1),
    ('refs_smallest', 2, -0x80000000, None, 1), ('refs_largest', 2, 0x7fffffff, None, 1),
    ('signal_clears_work', 1, 1, 'null', 1), ('signal_swaps_work', 1, 2, 'other', 1),
    ('signal_swaps_work_kept', 0, 1, 'other', 4), ('signal_swaps_work_unref', 1, 1, 'other', 0),
]


def holder_cases(address: str, value: int | None) -> list[dict]:
    return [holder_case(address, *row) for row in HOLDER_CASES]


# destroys_owner_of_wide_string_then_base_0x0139b630 and
# deletes_owner_of_wide_string_after_base_0x0139b630 (wide_string_owner_family.cpp). The string
# object's heap comes from the heap lookup stub; its long text goes to its own allocator. Both
# allocators are buffers whose vtables point slot 0x70 at a stub; the lookup may rewrite the
# string's capacity or allocator through its argument (both are read after it).
STRING_HEAP_OWNING = '0x0247b720'
STRING_BASE_DESTROY = '0x0139b630'
STRING_OPERATOR_DELETE = '0x02fbe588'
STRING_TEXT_FREE = '0x00500400'        # slot 0x70 of the string's allocator
STRING_HEAP_FREE = '0x00500420'        # slot 0x70 of the string object's heap
STRING_OTHER_TEXT_FREE = '0x00500480'  # slot 0x70 of an allocator the lookup may install
STRING_FREE_SLOT = 0x70
STRING_AT, STRING_FLAG = 0x30, 0x44
STRING_SIZE, STRING_CAPACITY, STRING_ALLOCATOR = 0x18, 0x20, 0x28


def string_owner_case(address: str, deleting: bool, case_id: str, flag: int, has_string: bool,
                      capacity: int = 7, change: dict | None = None) -> dict:
    """FLAG: the owned flag byte; CAPACITY: the string's; CHANGE: what the heap lookup writes into
    the string ('capacity' value, 'allocator' True for the other allocator)."""
    change = change or {}
    memory = [{'addr': 'buf:owner+0x0', 'guest': '0x00500460'},
              {'addr': f'buf:owner+{STRING_FLAG:#x}', 'bytes': _le(flag, 1)},
              {'addr': 'buf:string+0x8', 'pointer': 'text'},
              {'addr': f'buf:string+{STRING_SIZE:#x}', 'bytes': _le(5, 8)},
              {'addr': f'buf:string+{STRING_CAPACITY:#x}', 'bytes': _le(capacity, 8)},
              {'addr': f'buf:string+{STRING_ALLOCATOR:#x}', 'pointer': 'allocator'},
              {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_vt'},
              {'addr': f'buf:allocator_vt+{STRING_FREE_SLOT:#x}', 'guest': STRING_TEXT_FREE},
              {'addr': 'buf:other_allocator+0x0', 'pointer': 'other_allocator_vt'},
              {'addr': f'buf:other_allocator_vt+{STRING_FREE_SLOT:#x}', 'guest': STRING_OTHER_TEXT_FREE},
              {'addr': 'buf:heap+0x0', 'pointer': 'heap_vt'},
              {'addr': f'buf:heap_vt+{STRING_FREE_SLOT:#x}', 'guest': STRING_HEAP_FREE}]
    memory.append({'addr': f'buf:owner+{STRING_AT:#x}', 'pointer': 'string'} if has_string
                  else {'addr': f'buf:owner+{STRING_AT:#x}', 'bytes': _le(0, 8)})
    stubs = []
    if flag & 0xff and has_string:
        lookup = {'address': STRING_HEAP_OWNING, 'argc': 1, 'ret': 'buf:heap'}
        writes = []
        if 'capacity' in change:
            writes.append({'arg': 0, 'offset': STRING_CAPACITY, 'bytes': _le(change['capacity'], 8)})
            capacity = change['capacity']
        if change.get('allocator'):
            writes.append({'arg': 0, 'offset': STRING_ALLOCATOR, 'pointer': 'other_allocator'})
        if writes:
            lookup['writes'] = writes
        stubs.append(lookup)
        if capacity >= 8:
            stubs.append({'address': STRING_OTHER_TEXT_FREE if change.get('allocator') else STRING_TEXT_FREE,
                          'argc': 2})
        stubs.append({'address': STRING_HEAP_FREE, 'argc': 2,
                      'argmem': [{'arg': 1, 'offset': 0x8, 'size': 0x20}]})
    stubs.append({'address': STRING_BASE_DESTROY, 'argc': 1, 'argmem': [{'arg': 0, 'offset': 0, 'size': 0x48}]})
    if deleting:
        stubs.append({'address': STRING_OPERATOR_DELETE, 'argc': 1})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'args': {'rdi': 'buf:owner'},
            'buffers': {'owner': _pattern(31, 0x60), 'string': _pattern(33, 0x40), 'text': _pattern(35, 0x40),
                        'allocator': _pattern(37, 0x10), 'allocator_vt': _pattern(39, 0x80),
                        'other_allocator': _pattern(41, 0x10), 'other_allocator_vt': _pattern(43, 0x80),
                        'heap': _pattern(45, 0x10), 'heap_vt': _pattern(47, 0x80)},
            'memory': memory, 'imports': [], 'stubs': stubs}


# (id, flag, has string, capacity, change)
STRING_OWNER_CASES = [
    ('not_owned', 0, True, 0x20, None), ('not_owned_no_string', 0, False, 0x20, None),
    ('owned_no_string', 1, False, 0x20, None), ('owned_inline', 1, True, 7, None),
    ('owned_empty_capacity', 1, True, 0, None), ('owned_long_smallest', 1, True, 8, None),
    ('owned_long', 1, True, 0x40, None), ('owned_capacity_largest', 1, True, (1 << 64) - 1, None),
    ('owned_capacity_high_bit', 1, True, 1 << 63, None), ('owned_flag_ff', 0xff, True, 0x10, None),
    ('owned_flag_80', 0x80, True, 7, None),
    ('lookup_grows_capacity', 1, True, 7, {'capacity': 8}),
    ('lookup_shrinks_capacity', 1, True, 0x10, {'capacity': 7}),
    ('lookup_swaps_allocator', 1, True, 0x10, {'allocator': True}),
    ('lookup_swaps_allocator_inline', 1, True, 7, {'allocator': True}),
]


def string_owner_cases(deleting: bool):
    def build(address: str, value: int | None) -> list[dict]:
        return [string_owner_case(address, deleting, *row) for row in STRING_OWNER_CASES]
    return build


# visits_children_of_list_at_0x40 and secondary_base_visits_children_of_list_at_0x40
# (child_list_visit_family.cpp). One block holds the object (+0x0), two lists (+0x100, +0x200)
# and two child arrays (+0x300, +0x380), so the object's slot 0x28 call, which gets the object,
# can swap the list or its array, change the count or a later child through its argument.
VISIT_PASS = '0x00b69e20'
VISIT_SLOT = '0x00500400'   # what the object's vtable slot 0x28 points at
VISIT_LIST, VISIT_OTHER_LIST = 0x100, 0x200
VISIT_ARRAY, VISIT_OTHER_ARRAY = 0x300, 0x380
VISIT_LIST_FIELD, VISIT_CHILDREN, VISIT_COUNT = 0x40, 0xa0, 0xa8
VISIT_ARRAY_SLOTS = 8
VISIT_SECONDARY = 0x48


def child_pointer(array: int, index: int) -> str:
    return f'kids+{(array // 0x80 - 6) * 0x100 + index * 0x20:#x}'


def visit_case(address: str, secondary: bool, case_id: str, count: int | None,
               change: dict | None = None) -> dict:
    """COUNT None: no list. CHANGE: what the first slot call writes ('list' to the other list,
    'array' to the other array, 'count' a new count, 'child' a new third child)."""
    change = change or {}
    memory = [{'addr': 'buf:block+0x0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+0x28', 'guest': VISIT_SLOT}]
    if count is None:
        memory.append({'addr': f'buf:block+{VISIT_LIST_FIELD:#x}', 'bytes': _le(0, 8)})
    else:
        memory.append({'addr': f'buf:block+{VISIT_LIST_FIELD:#x}', 'pointer': f'block+{VISIT_LIST:#x}'})
    for at, array, n in ((VISIT_LIST, VISIT_ARRAY, count or 0), (VISIT_OTHER_LIST, VISIT_OTHER_ARRAY, 2)):
        memory.append({'addr': f'buf:block+{at + VISIT_CHILDREN:#x}', 'pointer': f'block+{array:#x}'})
        memory.append({'addr': f'buf:block+{at + VISIT_COUNT:#x}', 'bytes': _le(n)})
    for array in (VISIT_ARRAY, VISIT_OTHER_ARRAY):
        for i in range(VISIT_ARRAY_SLOTS):
            memory.append({'addr': f'buf:block+{array + 8 * i:#x}', 'pointer': child_pointer(array, i)})
    writes = []
    if change.get('list'):
        writes.append({'arg': 0, 'offset': VISIT_LIST_FIELD, 'pointer': f'block+{VISIT_OTHER_LIST:#x}'})
    if change.get('array'):
        writes.append({'arg': 0, 'offset': VISIT_LIST + VISIT_CHILDREN, 'pointer': f'block+{VISIT_OTHER_ARRAY:#x}'})
    if 'count' in change:
        writes.append({'arg': 0, 'offset': VISIT_LIST + VISIT_COUNT, 'bytes': _le(change['count'])})
    if change.get('child'):
        writes.append({'arg': 0, 'offset': VISIT_ARRAY + 16, 'pointer': 'kids+0xf0'})
    stubs = []
    for i in range(max(count or 0, 0)):
        stubs.append({'address': VISIT_PASS, 'argc': 2})
        slot = {'address': VISIT_SLOT, 'argc': 2}
        if i == 0 and writes:
            slot['writes'] = writes
        stubs.append(slot)
    rdi = f'buf:block+{VISIT_SECONDARY:#x}' if secondary else 'buf:block'
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void', 'args': {'rdi': rdi},
            'buffers': {'block': _pattern(51, 0x400), 'vtable': _pattern(53, 0x40), 'kids': _pattern(55, 0x200)},
            'memory': memory, 'imports': [], 'stubs': stubs}


# (id, count, change)
VISIT_CASES = [
    ('no_list', None, None), ('empty', 0, None), ('negative', -1, None), ('smallest', -0x80000000, None),
    ('one', 1, None), ('two', 2, None), ('five', 5, None), ('eight', 8, None),
    ('list_swapped', 4, {'list': True}), ('array_swapped', 3, {'array': True}),
    ('count_shrinks', 4, {'count': 1}), ('count_grows', 2, {'count': 6}), ('count_cleared', 3, {'count': 0}),
    ('child_changed', 4, {'child': True}), ('list_swapped_one', 1, {'list': True}),
]


def visit_cases(secondary: bool):
    def build(address: str, value: int | None) -> list[dict]:
        return [visit_case(address, secondary, *row) for row in VISIT_CASES]
    return build


# selects_index_of_changed_id_with_0x01348fe0 (changed_id_index_family.cpp): an object following an
# id, its owner (+0x18) with the target at +0x110, and a table of 24-byte entries (id at +0x10).
SELECT_APPLY = '0x01348fe0'
SELECT_ENTRY, SELECT_ENTRY_ID = 0x18, 0x10


def select_case(address: str, case_id: str, followed: int | None, seen: int, ids: list[int]) -> dict:
    """FOLLOWED None: no followed id. IDS: the entries' ids."""
    memory = [{'addr': 'buf:follower+0x18', 'pointer': 'owner'},
              {'addr': 'buf:follower+0x20', 'bytes': _le(seen)},
              {'addr': 'buf:follower+0x30', 'pointer': 'table'},
              {'addr': 'buf:owner+0x110', 'pointer': 'target'},
              {'addr': 'buf:table+0x18', 'pointer': 'entries'},
              {'addr': 'buf:table+0x20', 'pointer': f'entries+{SELECT_ENTRY * len(ids):#x}'}]
    memory += [{'addr': f'buf:entries+{SELECT_ENTRY * i + SELECT_ENTRY_ID:#x}', 'bytes': _le(v)}
               for i, v in enumerate(ids)]
    stubs = []
    if followed is None:
        memory.append({'addr': 'buf:follower+0x28', 'bytes': _le(0, 8)})
    else:
        memory.append({'addr': 'buf:follower+0x28', 'pointer': 'id'})
        memory.append({'addr': 'buf:id+0x0', 'bytes': _le(followed)})
        # Scripted on an unchanged id too, so a call made there is recorded.
        stubs.append({'address': SELECT_APPLY, 'argc': 3, 'argi': [0], 'argu32': [1, 2], 'ret': '0x77'})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void', 'args': {'rdi': 'buf:follower'},
            'buffers': {'follower': _pattern(61, 0x40), 'owner': _pattern(63, 0x120), 'target': _pattern(65, 0x10),
                        'table': _pattern(67, 0x30), 'entries': _pattern(69, SELECT_ENTRY * 8 + 8),
                        'id': _pattern(71, 0x8)},
            'memory': memory, 'imports': [], 'stubs': stubs}


# (id, followed id, seen id, entry ids)
SELECT_CASES = [
    ('no_id', None, 3, [1, 2, 3]), ('unchanged', 5, 5, [1, 5, 9]), ('unchanged_negative', -1, -1, [-1]),
    ('first', 7, 2, [7, 8, 9]), ('middle', 8, 2, [7, 8, 9]), ('last', 9, 2, [7, 8, 9]),
    ('absent', 4, 2, [7, 8, 9]), ('empty_table', 4, 2, []), ('one_entry', 4, 2, [4]),
    ('duplicates', 6, 1, [3, 6, 6, 6]), ('seen_in_table', 6, 3, [3, 6]), ('negative_id', -2, 0, [0, -2, 5]),
    ('smallest_id', -0x80000000, 0x7fffffff, [0x7fffffff, -0x80000000]),
    ('eight_entries_last', 0x101, 0, [1, 2, 3, 4, 5, 6, 7, 0x101]), ('zero_id', 0, 1, [1, 0]),
]


def select_cases(address: str, value: int | None) -> list[dict]:
    return [select_case(address, *row) for row in SELECT_CASES]


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'destroys_holder_of_shared_work': holder_cases,
         'destroys_owner_of_wide_string_then_base_0x0139b630': string_owner_cases(False),
         'deletes_owner_of_wide_string_after_base_0x0139b630': string_owner_cases(True),
         'visits_children_of_list_at_0x40': visit_cases(False),
         'secondary_base_visits_children_of_list_at_0x40': visit_cases(True),
         'selects_index_of_changed_id_with_0x01348fe0': select_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'destroys_holder_of_shared_work': ['bb_mutant_does_nothing', 'bb_mutant_holder_of_shared_work_work_once', 'bb_mutant_holder_of_shared_work_signal_on_one', 'bb_mutant_holder_of_shared_work_keeps_work'],
    'destroys_owner_of_wide_string_then_base_0x0139b630': ['bb_mutant_does_nothing', 'bb_mutant_string_owner_ignores_flag', 'bb_mutant_string_owner_capacity_early', 'bb_mutant_string_owner_allocator_early', 'bb_mutant_string_owner_frees_inline', 'bb_mutant_string_owner_keeps_pointer', 'bb_mutant_string_owner_no_reset'],
    'deletes_owner_of_wide_string_after_base_0x0139b630': ['bb_mutant_does_nothing', 'bb_mutant_string_owner_delete_capacity_early', 'bb_mutant_string_owner_delete_first', 'bb_mutant_string_owner_delete_kept'],
    'visits_children_of_list_at_0x40': ['bb_mutant_does_nothing', 'bb_mutant_visit_children_reloads_count', 'bb_mutant_visit_children_caches_list', 'bb_mutant_visit_children_caches_array', 'bb_mutant_visit_children_slot_first'],
    'secondary_base_visits_children_of_list_at_0x40': ['bb_mutant_does_nothing', 'bb_mutant_secondary_visit_children_reloads_count', 'bb_mutant_secondary_visit_children_caches_list', 'bb_mutant_secondary_visit_children_slot_first'],
    'selects_index_of_changed_id_with_0x01348fe0': ['bb_mutant_does_nothing', 'bb_mutant_select_changed_id_keeps_seen', 'bb_mutant_select_changed_id_finds_last', 'bb_mutant_select_changed_id_absent_as_zero', 'bb_mutant_select_changed_id_unchanged_too'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
