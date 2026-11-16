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

frees_kind_sized_dword_storage_through_0x05ac8288 (kind_sized_dword_free_family.cpp): every kind
rule (4, 6, the rest, the low bit ignored, a second answer that differs), empty, negative,
wrapping and extreme counts, and first kind calls that change the count, the vtable or the
allocator global (read before the kind calls).

deletes_work_holder (work_holder_delete_family.cpp): no work, user and reference counts on both
sides of 1 (and their extremes), and signal sets that clear the holder's work or swap in another
(the work is read again after the signal).

creates_0xe40_byte_object_with_0x020e9820 (object_create_family.cpp): the allocation succeeding
(the constructor's result ignored) or failing.

frees_block_at_0xa0_then_destroys_base_0x00569a90 (block_at_0xa0_family.cpp): no block, a block
with and without data, and first frees that clear or swap the block field or repoint the
allocator global (both read again for the second free).

destroys_owner_of_two_objects and deletes_owner_of_two_objects (owner_of_two_family.cpp): no
objects, either or both, first releases that clear or swap the second field, a second release
that stores a first object again (cleared at the end) and sees the first field already cleared,
and a release that repoints the allocator global (read after the releases).
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
def _fill(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}


def _word(value: int, size: int = 4) -> str:
    return (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little').hex()


# frees_kind_sized_dword_storage_through_0x05ac8288 (kind_sized_dword_free_family.cpp). The object
# lives in writable image memory just past the allocator pointer, so a kind call can change the
# count, the vtable or the allocator pointer through its object argument (stub writes).
DWORD_FREE = '0x02fa1c20'
DWORD_FREE_ALLOCATOR = 0x05ac8288
DWORD_FREE_OBJECT = 0x05ac82c0
DWORD_FREE_KIND = '0x00500400'        # what the kind slot (vtable +0x88) points at
DWORD_FREE_OTHER_KIND = '0x00500420'  # the slot of the vtable a kind call may install
DWORD_FREE_SLOT = 0x88
DWORD_FREE_COUNT = 0x18


def dword_free_case(address: str, case_id: str, kinds: list[int], count: int, changes: dict | None) -> dict:
    """An object of COUNT entries whose kind calls answer KINDS in turn (the second only when the
    first is not kind 4); CHANGES: what the first kind call writes ('count', 'vtable',
    'allocator')."""
    changes = changes or {}
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'vtable': _fill(7, DWORD_FREE_SLOT + 8), 'other_vtable': _fill(9, DWORD_FREE_SLOT + 8),
                        'allocator': _fill(11, 0x20), 'other_allocator': _fill(13, 0x20),
                        'storage': _fill(15, 0x10)},
            'memory': [{'addr': hex(DWORD_FREE_OBJECT), 'bytes': _fill(17, 0x20)['bytes']},
                       {'addr': hex(DWORD_FREE_OBJECT), 'pointer': 'vtable'},
                       {'addr': hex(DWORD_FREE_OBJECT + 8), 'pointer': 'storage'},
                       {'addr': hex(DWORD_FREE_OBJECT + DWORD_FREE_COUNT), 'bytes': _word(count)},
                       {'addr': f'buf:vtable+0x{DWORD_FREE_SLOT:x}', 'guest': DWORD_FREE_KIND},
                       {'addr': f'buf:other_vtable+0x{DWORD_FREE_SLOT:x}', 'guest': DWORD_FREE_OTHER_KIND},
                       {'addr': hex(DWORD_FREE_ALLOCATOR), 'pointer': 'allocator'}],
            'imports': [], 'stubs': [],
            'args': {'rdi': f'guest:{DWORD_FREE_OBJECT:#x}', 'rsi': '0x1111111111111111'}}
    writes = []
    if 'count' in changes:
        writes.append({'arg': 0, 'offset': DWORD_FREE_COUNT, 'bytes': _word(changes['count'])})
    if changes.get('vtable'):
        writes.append({'arg': 0, 'offset': 0, 'pointer': 'other_vtable'})
    if changes.get('allocator'):
        writes.append({'arg': 0, 'offset': DWORD_FREE_ALLOCATOR - DWORD_FREE_OBJECT, 'pointer': 'other_allocator'})
    asked = kinds[:1] if (kinds[0] & 0xfffffffe) == 4 or len(kinds) == 1 else kinds[:2]
    for n, kind in enumerate(asked):
        stub = {'address': DWORD_FREE_OTHER_KIND if n and changes.get('vtable') else DWORD_FREE_KIND, 'argc': 2,
                'argmem': [{'arg': 0, 'offset': DWORD_FREE_COUNT, 'size': 4}],
                'ret': hex(0x1234567800000000 | (kind & 0xffffffff))}
        if n == 0 and writes:
            stub['writes'] = writes
        case['stubs'].append(stub)
    case['stubs'].append({'address': DWORD_FREE, 'argc': 4, 'ret': '0x0'})
    return case


# (id, kinds, count, changes): every kind and count rule, and kind calls that change the count,
# the vtable or the allocator pointer.
DWORD_FREE_CASES = [
    ('kind4', [4], 7, None), ('kind5', [5], 7, None), ('kind6', [6, 6], 7, None),
    ('kind7', [7, 7], 7, None), ('kind0', [0, 0], 7, None), ('kind2', [2, 3], 7, None),
    ('kind6_then_4', [6, 4], 7, None), ('kind0_then_6', [0, 6], 7, None),
    ('kind1_then_7', [1, 7], 5, None), ('kind_high', [0xfffffffe, 0xfffffffe], 7, None),
    ('kind4_empty', [4], 0, None), ('kind6_empty', [6, 6], 0, None), ('kind0_one', [0, 0], 1, None),
    ('kind4_negative', [4], -1, None), ('kind6_negative', [6, 6], -3, None), ('kind0_negative', [0, 0], -2, None),
    ('kind4_wraps', [4], 0x55555556, None), ('kind6_wraps', [6, 6], 0x55555556, None),
    ('kind0_largest', [0, 0], 0x7fffffff, None), ('kind4_smallest', [4], -0x80000000, None),
    ('kind4_count_changes', [4], 7, {'count': 11}), ('kind6_count_changes', [6, 6], 7, {'count': 11}),
    ('kind0_count_changes', [0, 0], 9, {'count': 3}),
    ('vtable_changes', [0, 6], 7, {'vtable': True}), ('vtable_changes_kind4', [6, 4], 7, {'vtable': True}),
    ('allocator_changes', [4], 7, {'allocator': True}), ('allocator_changes_kind6', [0, 6], 7, {'allocator': True}),
]


def dword_free_cases(address: str, value: int | None) -> list[dict]:
    return [dword_free_case(address, *row) for row in DWORD_FREE_CASES]


# deletes_work_holder (work_holder_delete_family.cpp). One block holds work A (+0x0), the holder
# (+0x80) and work B (+0xc0), so the signal's set, called with A's signal (+0x10), can rewrite the
# holder's work pointer through its argument.
WORK_SIGNAL_SET = '0x0247e780'
WORK_FATAL_ERROR = '0x024b55b0'
WORK_DELETE = '0x02fbe588'
WORK_DESTROY = '0x00500440'   # what the works' vtable slot 0 points at
WORK_INVALID_UNREF = '0x04d40331'
WORK_HOLDER, WORK_OTHER = 0x80, 0xc0
WORK_SIGNAL, WORK_USERS, WORK_REFERENCES = 0x10, 0x30, 0x8


def work_holder_case(address: str, case_id: str, users: int | None, references: int,
                     change: str | None = None, other_references: int = 1) -> dict:
    """USERS None: the holder has no work. CHANGE: what the signal's set stores in the holder's
    work pointer ('null' or 'other')."""
    memory = [{'addr': f'buf:block+{WORK_HOLDER:#x}', 'guest': '0x00500460'}]
    stubs = []
    for base, refs in ((0, references), (WORK_OTHER, other_references)):
        memory.append({'addr': f'buf:block+{base:#x}', 'pointer': 'workvt'})
        memory.append({'addr': f'buf:block+{base + WORK_REFERENCES:#x}', 'bytes': _word(refs)})
        memory.append({'addr': f'buf:block+{base + WORK_USERS:#x}', 'bytes': _word(users or 0)})
    if users is None:
        memory.append({'addr': f'buf:block+{WORK_HOLDER + 8:#x}', 'bytes': _word(0, 8)})
    else:
        memory.append({'addr': f'buf:block+{WORK_HOLDER + 8:#x}', 'pointer': 'block'})
        work = 0
        if users <= 1:
            signal = {'address': WORK_SIGNAL_SET, 'argc': 2, 'argi': [0], 'argu8': [1], 'ret': '0x5a5a',
                      'argmem': [{'arg': 0, 'offset': WORK_USERS - WORK_SIGNAL, 'size': 4}]}
            offset = WORK_HOLDER + 8 - WORK_SIGNAL
            if change == 'null':
                signal['writes'] = [{'arg': 0, 'offset': offset, 'bytes': _word(0, 8)}]
                work = None
            elif change == 'other':
                signal['writes'] = [{'arg': 0, 'offset': offset, 'pointer': f'block+{WORK_OTHER:#x}'}]
                work, references = WORK_OTHER, other_references
            stubs.append(signal)
        if work is not None:
            if references == 1:
                stubs.append({'address': WORK_DESTROY, 'argc': 1})
            elif references <= 0:
                stubs.append({'address': WORK_FATAL_ERROR, 'argc': 3})
    stubs.append({'address': WORK_DELETE, 'argc': 1})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'args': {'rdi': f'buf:block+{WORK_HOLDER:#x}'},
            'buffers': {'block': _fill(21, 0x100), 'workvt': _fill(23, 0x10)},
            'memory': memory + [{'addr': 'buf:workvt+0x0', 'guest': WORK_DESTROY}],
            'imports': [], 'stubs': stubs}


# (id, users, references, change, other work's references)
WORK_HOLDER_CASES = [
    ('no_work', None, 1, None, 1), ('users2_refs2', 2, 2, None, 1), ('users2_refs1', 2, 1, None, 1),
    ('users1_refs2', 1, 2, None, 1), ('users1_refs1', 1, 1, None, 1), ('users0_refs3', 0, 3, None, 1),
    ('users_negative', -5, 1, None, 1), ('users_smallest', -0x80000000, 2, None, 1),
    ('users_largest', 0x7fffffff, 1, None, 1), ('refs0', 3, 0, None, 1), ('refs_negative', 1, -2, None, 1),
    ('refs_smallest', 2, -0x80000000, None, 1), ('refs_largest', 2, 0x7fffffff, None, 1),
    ('signal_clears_work', 1, 1, 'null', 1), ('signal_swaps_work', 1, 2, 'other', 1),
    ('signal_swaps_work_kept', 0, 1, 'other', 4), ('signal_swaps_work_unref', 1, 1, 'other', 0),
]


def work_holder_cases(address: str, value: int | None) -> list[dict]:
    return [work_holder_case(address, *row) for row in WORK_HOLDER_CASES]


# creates_0xe40_byte_object_with_0x020e9820 (object_create_family.cpp): an owner whose first
# field points at an allocator with a scripted allocate slot (0x58).
CREATE_CONSTRUCT = '0x020e9820'
CREATE_ALLOCATE = '0x00500480'


def create_case(address: str, case_id: str, fails: bool, construct_ret: str = '0x0') -> dict:
    stubs = [{'address': CREATE_ALLOCATE, 'argc': 3, 'ret': '0x0' if fails else 'buf:object'}]
    if not fails:
        stubs.append({'address': CREATE_CONSTRUCT, 'argc': 2, 'ret': construct_ret})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i64',
            'args': {'rdi': 'buf:owner', 'rsi': '0x2222222222222222'},
            'buffers': {'owner': _fill(31, 0x20), 'heap': _fill(33, 0x10), 'heapvt': _fill(35, 0x60),
                        'object': _fill(37, 0x40)},
            'memory': [{'addr': 'buf:owner+0x0', 'pointer': 'heap'}, {'addr': 'buf:heap+0x0', 'pointer': 'heapvt'},
                       {'addr': 'buf:heapvt+0x58', 'guest': CREATE_ALLOCATE}],
            'imports': [], 'stubs': stubs}


def create_cases(address: str, value: int | None) -> list[dict]:
    return [create_case(address, 'creates', False), create_case(address, 'allocation_fails', True),
            create_case(address, 'constructor_result_ignored', False, 'buf:owner'),
            create_case(address, 'constructor_returns_minus_one', False, '0xffffffffffffffff')]


# frees_block_at_0xa0_then_destroys_base_0x00569a90 (block_at_0xa0_family.cpp). The allocator
# lives in writable image memory just past the global that points at it, so a free can repoint
# the global through its allocator argument; the block's data is the owner itself when a free
# must rewrite the owner's block field through its data argument.
BLOCK_GLOBAL = 0x057e7320
BLOCK_ALLOCATOR = 0x057e7340
BLOCK_FREE = '0x005004a0'        # the allocator's free slot (+0x68)
BLOCK_OTHER_FREE = '0x005004c0'  # the free slot of the allocator a free may install
BLOCK_BASE = '0x00569a90'
BLOCK_FIELD = 0xa0


def block_case(address: str, case_id: str, block: bool, data: str | None, change: str | None = None) -> dict:
    """DATA: what the block's first field points at ('buf' a buffer, 'owner' the owner, None
    null). CHANGE: what the first free writes ('clear' or 'swap' the block field, 'allocator')."""
    memory = [{'addr': hex(BLOCK_ALLOCATOR), 'pointer': 'allocvt'}, {'addr': hex(BLOCK_GLOBAL), 'guest': hex(BLOCK_ALLOCATOR)},
              {'addr': 'buf:allocvt+0x68', 'guest': BLOCK_FREE}, {'addr': 'buf:otherallocvt+0x68', 'guest': BLOCK_OTHER_FREE},
              {'addr': 'buf:other_allocator+0x0', 'pointer': 'otherallocvt'},
              {'addr': 'buf:block2+0x0', 'pointer': 'data'}]
    stubs = []
    if block:
        memory.append({'addr': f'buf:owner+{BLOCK_FIELD:#x}', 'pointer': 'block'})
        if data == 'buf':
            memory.append({'addr': 'buf:block+0x0', 'pointer': 'data'})
        elif data == 'owner':
            memory.append({'addr': 'buf:block+0x0', 'pointer': 'owner'})
        else:
            memory.append({'addr': 'buf:block+0x0', 'bytes': _word(0, 8)})
        first = {'address': BLOCK_FREE, 'argc': 2, 'ret': '0x0'}
        if change == 'clear':
            first['writes'] = [{'arg': 1, 'offset': BLOCK_FIELD, 'bytes': _word(0, 8)}]
        elif change == 'swap':
            first['writes'] = [{'arg': 1, 'offset': BLOCK_FIELD, 'pointer': 'block2'}]
        elif change == 'allocator':
            first['writes'] = [{'arg': 0, 'offset': BLOCK_GLOBAL - BLOCK_ALLOCATOR, 'pointer': 'other_allocator'}]
        stubs += [first, {'address': BLOCK_OTHER_FREE if change == 'allocator' else BLOCK_FREE, 'argc': 2, 'ret': '0x0'}]
    else:
        memory.append({'addr': f'buf:owner+{BLOCK_FIELD:#x}', 'bytes': _word(0, 8)})
    stubs.append({'address': BLOCK_BASE, 'argc': 1})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void', 'args': {'rdi': 'buf:owner'},
            'buffers': {'owner': _fill(41, 0xb0), 'block': _fill(43, 0x10), 'block2': _fill(45, 0x10),
                        'data': _fill(47, 0x20), 'allocvt': _fill(49, 0x70), 'otherallocvt': _fill(51, 0x70),
                        'other_allocator': _fill(53, 0x10)},
            'memory': memory, 'imports': [], 'stubs': stubs}


def block_cases(address: str, value: int | None) -> list[dict]:
    return [block_case(address, 'no_block', False, None), block_case(address, 'block_with_data', True, 'buf'),
            block_case(address, 'block_without_data', True, None),
            block_case(address, 'free_clears_block', True, 'owner', 'clear'),
            block_case(address, 'free_swaps_block', True, 'owner', 'swap'),
            block_case(address, 'free_swaps_allocator', True, 'buf', 'allocator')]


# destroys_owner_of_two_objects and deletes_owner_of_two_objects (owner_of_two_family.cpp). One
# block holds object 1 (+0x0), object 2 (+0x20), the owner (+0x40) and object 3 (+0x60), so a
# release can rewrite the owner's fields through its object argument; object 2's release compares
# the owner's first field (cleared before it). For the deleting copies an object in writable image
# memory past the allocator global can repoint the global.
OWNER_RELEASE = '0x005004e0'   # what the objects' vtable slot 0x18 points at
OWNER_FREE = '0x02fa1c20'
OWNER_GLOBAL = 0x05ac8288
OWNER_IMAGE_OBJECT = 0x05ac82c0
OWNER_AT, OWNER_SECOND_AT, OWNER_THIRD_AT = 0x40, 0x20, 0x60


def owner_case(address: str, case_id: str, first: str | None, second: str | None, change: str | None,
               deleting: bool) -> dict:
    """FIRST/SECOND: 'one', 'two', 'three' (objects in the block), 'image' (the image object) or
    None. CHANGE: 'clear_second' / 'swap_second' (object 1's release), 'set_first' (object 2's),
    'allocator' (the image object's)."""
    where = {'one': 0, 'two': OWNER_SECOND_AT, 'three': OWNER_THIRD_AT}
    memory = [{'addr': f'buf:block+{at:#x}', 'pointer': 'objvt'} for at in where.values()]
    memory += [{'addr': 'buf:objvt+0x18', 'guest': OWNER_RELEASE},
               {'addr': hex(OWNER_IMAGE_OBJECT), 'pointer': 'objvt'},
               {'addr': hex(OWNER_GLOBAL), 'pointer': 'allocator'}]
    for field, value in ((8, first), (0x10, second)):
        addr = f'buf:block+{OWNER_AT + field:#x}'
        if value is None:
            memory.append({'addr': addr, 'bytes': _word(0, 8)})
        elif value == 'image':
            memory.append({'addr': addr, 'guest': hex(OWNER_IMAGE_OBJECT)})
        else:
            memory.append({'addr': addr, 'pointer': f'block+{where[value]:#x}'})
    stubs = []
    if first:
        release = {'address': OWNER_RELEASE, 'argc': 1, 'ret': '0x0'}
        if change == 'clear_second':
            release['writes'] = [{'arg': 0, 'offset': OWNER_AT + 0x10, 'bytes': _word(0, 8)}]
            second = None
        elif change == 'swap_second':
            release['writes'] = [{'arg': 0, 'offset': OWNER_AT + 0x10, 'pointer': f'block+{OWNER_THIRD_AT:#x}'}]
            second = 'three'
        elif change == 'allocator':
            release['writes'] = [{'arg': 0, 'offset': OWNER_GLOBAL - OWNER_IMAGE_OBJECT, 'pointer': 'other_allocator'}]
        stubs.append(release)
        if second:
            release = {'address': OWNER_RELEASE, 'argc': 1, 'ret': '0x0'}
            if second == 'two':
                release['argmem'] = [{'arg': 0, 'offset': OWNER_AT + 8 - OWNER_SECOND_AT, 'size': 8}]
                if change == 'set_first':
                    release['writes'] = [{'arg': 0, 'offset': OWNER_AT + 8 - OWNER_SECOND_AT,
                                          'pointer': f'block+{OWNER_THIRD_AT:#x}'}]
            stubs.append(release)
    if deleting:
        stubs.append({'address': OWNER_FREE, 'argc': 4, 'ret': '0x0'})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'args': {'rdi': f'buf:block+{OWNER_AT:#x}'},
            'buffers': {'block': _fill(61, 0x80), 'objvt': _fill(63, 0x20), 'allocator': _fill(65, 0x10),
                        'other_allocator': _fill(67, 0x10)},
            'memory': memory, 'imports': [], 'stubs': stubs}


# (id, first, second, change)
OWNER_CASES = [
    ('no_objects', None, None, None), ('first_only', 'one', None, None), ('second_only', None, 'two', None),
    ('both', 'one', 'two', None), ('first_release_clears_second', 'one', 'two', 'clear_second'),
    ('first_release_swaps_second', 'one', 'two', 'swap_second'),
    ('first_release_sets_second', 'one', None, 'swap_second'),
    ('second_release_sets_first', 'one', 'two', 'set_first'),
    ('release_swaps_allocator', 'image', 'two', 'allocator'),
]


def owner_cases(deleting: bool):
    def cases(address: str, value: int | None) -> list[dict]:
        return [owner_case(address, case_id, first, second, change, deleting)
                for case_id, first, second, change in OWNER_CASES]
    return cases


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'frees_kind_sized_dword_storage_through_0x05ac8288': dword_free_cases,
         'deletes_work_holder': work_holder_cases,
         'creates_0xe40_byte_object_with_0x020e9820': create_cases,
         'frees_block_at_0xa0_then_destroys_base_0x00569a90': block_cases,
         'destroys_owner_of_two_objects': owner_cases(False),
         'deletes_owner_of_two_objects': owner_cases(True)}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'frees_kind_sized_dword_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_dword_storage_free_reads_allocator_late', 'bb_mutant_dword_storage_free_reads_count_early', 'bb_mutant_dword_storage_free_asks_once', 'bb_mutant_dword_storage_free_caches_vtable', 'bb_mutant_dword_storage_free_without_entries'],
    'deletes_work_holder': ['bb_mutant_does_nothing', 'bb_mutant_work_holder_delete_work_once', 'bb_mutant_work_holder_delete_signal_on_one', 'bb_mutant_work_holder_delete_kept'],
    'creates_0xe40_byte_object_with_0x020e9820': ['bb_mutant_does_nothing', 'bb_mutant_create_object_aligned_8', 'bb_mutant_create_object_constructs_on_failure', 'bb_mutant_create_object_returns_constructor_result'],
    'frees_block_at_0xa0_then_destroys_base_0x00569a90': ['bb_mutant_does_nothing', 'bb_mutant_block_at_0xa0_once', 'bb_mutant_block_at_0xa0_allocator_once', 'bb_mutant_block_at_0xa0_skips_data'],
    'destroys_owner_of_two_objects': ['bb_mutant_does_nothing', 'bb_mutant_owner_of_two_second_early', 'bb_mutant_owner_of_two_first_cleared_late', 'bb_mutant_owner_of_two_no_final_clear', 'bb_mutant_owner_of_two_second_alone'],
    'deletes_owner_of_two_objects': ['bb_mutant_does_nothing', 'bb_mutant_owner_of_two_delete_second_early', 'bb_mutant_owner_of_two_delete_first_cleared_late', 'bb_mutant_owner_of_two_delete_allocator_early'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
