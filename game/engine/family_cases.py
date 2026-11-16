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

stores_vtable_0x0579c8a0_releases_members_then_destroys and
stores_vtable_0x0579c8a0_releases_members_destroys_then_deletes: the copies of one class's
destructor and its deleting twin (vtable_0x0579c8a0_destructor_family.cpp). The cases run no
callable, one outside and one inside its slot's storage; the wide string inline and on the heap
(capacities 8, 0x7f and all bits); the held blocks with bit 0x40 set or clear among other flags;
and callables and member destructors that rewrite the string's capacity, a block's flags, its
releaser and its value, which show each is read after the calls before it (with control mutants).

sorts_16_byte_elements_with_comparator: the copies of 0x0292d520's quicksort
(sort_16_byte_elements_family.cpp). The cases replay the sort in Python to script every
comparator call: one to seven elements in order, reversed, mixed, with equal and negative keys,
an inner range, first past last, negative indexes, and comparators that rewrite the middle element
(the pivot is a copy) or an element before it is swapped.

finds_id_pair_in_nested_hash_maps: the copies of 0x01274eb0's lookup of an id pair in a hash map
of hash maps (nested_hash_lookup_family.cpp). The cases build real chained tables (runs in id
order per bucket, a bucket count below the mask's so some indexes fold) and script the C
library's ldiv with its quotient and remainder (rax and rdx): an empty bucket and an empty table,
ids found at the start, inside and at the end of a run, in a folded bucket and with the top bit
set; ids missing below, inside and above a run; ids 0, all bits and the seed; an empty inner
table; equal outer ids (the first in order is taken).
"""
from __future__ import annotations

import copy
import importlib.util
import os
import struct

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


# stores_vtable_0x0579c8a0_releases_members_then_destroys and its deleting twin
# (vtable_0x0579c8a0_destructor_family.cpp): an object (`object`, 0x2018 bytes of filler) whose
# callable slot (+0x1ff0, pointer +0x2010), wide string (+0x1fb0) and held blocks (+0xd30,
# +0x1f30) are set per case; the callable, the string's allocator and the releasers have fake
# tables whose slots are scripted stubs comparing every argument; the member and base destructors
# are stubs too. Some stubs write into the object to show which fields are read after them.
# Generated fixtures, not game data.
DESTROY_FIRST_MEMBER, DESTROY_BASE_MEMBER, DESTROY_BASE = '0x01f4cbb0', '0x01ed24f0', '0x01ed5390'
OPERATOR_DELETE = '0x02fbe588'
FAKE_CALLABLE_DESTROY, FAKE_STRING_FREE, FAKE_RELEASE = '0x00500300', '0x00500320', '0x00500340'
OBJECT_SIZE = 0x2018
CALLABLE_STORAGE, CALLABLE_POINTER = 0x1ff0, 0x2010
TEXT_HEAP, TEXT_SIZE, TEXT_CAPACITY, TEXT_ALLOCATOR = 0x1fb8, 0x1fc8, 0x1fd0, 0x1fd8
SECOND_HELD, BASE_HELD = 0x1f30, 0xd30
HELD_AREA, HELD_RELEASER, HELD_FLAGS, HELD_VALUE = 0x30, 0x40, 0x48, 0x50
FIRST_MEMBER, BASE_MEMBER, SECOND_MEMBER = 0xd90, 0xa70, 0x1f20
HELD_SET = 0x40
ALL_BITS = 0xffffffffffffffff


def _qword(value: int) -> str:
    return struct.pack('<Q', value & ALL_BITS).hex()


def _destructor_case(address: str, cid: str, deleting: bool, callable_at: str | None = None,
                     capacity: int = 7, second_flags: int = 0, base_flags: int = 0,
                     callable_writes: list | None = None, first_member_writes: list | None = None) -> dict:
    """callable_at: None (no callable), 'outside' (its own buffer) or 'inside' (the slot's
    storage). callable_writes / first_member_writes: what those stubs store (through their first
    argument); a write's 'flags'/'capacity' key says which later decision it changes."""
    buffers = {'object': {'size': OBJECT_SIZE, 'bytes': ''.join(f'{(i * 7 + 0x31) & 0xff:02x}' for i in range(OBJECT_SIZE))},
               'callable': {'size': 0x20, 'bytes': '6b' * 0x20},
               'callvt': {'size': 0x30, 'bytes': 'e1' * 0x30},
               'text': {'size': 0x40, 'bytes': '47' * 0x40},
               'heap': {'size': 0x10, 'bytes': '3c' * 0x10},
               'heapvt': {'size': 0x80, 'bytes': 'e7' * 0x80},
               'releaser': {'size': 0x10, 'bytes': '2d' * 0x10},
               'other_releaser': {'size': 0x10, 'bytes': '2e' * 0x10},
               'relvt': {'size': 0x20, 'bytes': 'e9' * 0x20}}
    memory = [{'addr': 'buf:callable+0x0', 'pointer': 'callvt'},
              {'addr': 'buf:callvt+0x20', 'guest': FAKE_CALLABLE_DESTROY},
              {'addr': 'buf:heap+0x0', 'pointer': 'heapvt'},
              {'addr': 'buf:heapvt+0x70', 'guest': FAKE_STRING_FREE},
              {'addr': 'buf:releaser+0x0', 'pointer': 'relvt'},
              {'addr': 'buf:other_releaser+0x0', 'pointer': 'relvt'},
              {'addr': 'buf:relvt+0x18', 'guest': FAKE_RELEASE},
              {'addr': f'buf:object+{TEXT_HEAP:#x}', 'pointer': 'text'},
              {'addr': f'buf:object+{TEXT_SIZE:#x}', 'bytes': _qword(5)},
              {'addr': f'buf:object+{TEXT_CAPACITY:#x}', 'bytes': _qword(capacity)},
              {'addr': f'buf:object+{TEXT_ALLOCATOR:#x}', 'pointer': 'heap'}]
    for held, flags in ((SECOND_HELD, second_flags), (BASE_HELD, base_flags)):
        memory.append({'addr': f'buf:object+{held + HELD_RELEASER:#x}', 'pointer': 'releaser'})
        memory.append({'addr': f'buf:object+{held + HELD_FLAGS:#x}', 'bytes': struct.pack('<I', flags).hex()})
        memory.append({'addr': f'buf:object+{held + HELD_VALUE:#x}', 'bytes': _qword(0x5150000000000000 + held)})
    if callable_at == 'outside':
        memory.append({'addr': f'buf:object+{CALLABLE_POINTER:#x}', 'pointer': 'callable'})
    elif callable_at == 'inside':
        memory.append({'addr': f'buf:object+{CALLABLE_STORAGE:#x}', 'pointer': 'callvt'})
        memory.append({'addr': f'buf:object+{CALLABLE_POINTER:#x}', 'pointer': f'object+{CALLABLE_STORAGE:#x}'})
    else:
        memory.append({'addr': f'buf:object+{CALLABLE_POINTER:#x}', 'bytes': _qword(0)})

    stubs = []
    if callable_at:
        stub = {'address': FAKE_CALLABLE_DESTROY, 'argc': 2}
        if callable_writes:
            stub['writes'] = [{k: v for k, v in w.items() if k not in ('flags', 'capacity')} for w in callable_writes]
            for w in callable_writes:
                capacity = w.get('capacity', capacity)
                second_flags = w.get('flags', second_flags)
        stubs.append(stub)
    if capacity >= 8:
        stubs.append({'address': FAKE_STRING_FREE, 'argc': 2})
    if second_flags & HELD_SET:
        # The member's and the held block's tables are in place when the releaser runs.
        stubs.append({'address': FAKE_RELEASE, 'argc': 3,
                      'argmem': [{'arg': 1, 'offset': -(HELD_AREA + SECOND_HELD - SECOND_MEMBER), 'size': 0x18}]})
    stub = {'address': DESTROY_FIRST_MEMBER, 'argc': 1,
            # The object's own tables are in place while the member is destroyed.
            'argmem': [{'arg': 0, 'offset': -FIRST_MEMBER, 'size': 0x50}]}
    if first_member_writes:
        stub['writes'] = [{k: v for k, v in w.items() if k not in ('flags', 'capacity')} for w in first_member_writes]
        for w in first_member_writes:
            base_flags = w.get('flags', base_flags)
    stubs.append(stub)
    if base_flags & HELD_SET:
        # The base's tables and the held block's table are in place when the releaser runs.
        stubs.append({'address': FAKE_RELEASE, 'argc': 3,
                      'argmem': [{'arg': 1, 'offset': -(HELD_AREA + BASE_HELD), 'size': 0x50},
                                 {'arg': 1, 'offset': -HELD_AREA, 'size': 0x8}]})
    stubs.append({'address': DESTROY_BASE_MEMBER, 'argc': 1})
    stubs.append({'address': DESTROY_BASE, 'argc': 1, 'ret': '0x5a5a5a5a00000001'})
    if deleting:
        stubs.append({'address': OPERATOR_DELETE, 'argc': 1})
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:object'}, 'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def _destructor_cases(address: str, deleting: bool) -> list[dict]:
    c = lambda cid, **kw: _destructor_case(address, cid, deleting, **kw)
    # Writes through the callable (in the slot's storage, at +0x1ff0) or the member at +0xd90.
    raise_capacity = {'arg': 0, 'offset': TEXT_CAPACITY - CALLABLE_STORAGE, 'bytes': _qword(0x20), 'capacity': 0x20}
    set_second_flag = {'arg': 0, 'offset': SECOND_HELD + HELD_FLAGS - CALLABLE_STORAGE,
                       'bytes': struct.pack('<I', HELD_SET).hex(), 'flags': HELD_SET}
    clear_second_flag = {'arg': 0, 'offset': SECOND_HELD + HELD_FLAGS - CALLABLE_STORAGE,
                         'bytes': struct.pack('<I', 0).hex(), 'flags': 0}
    move_releaser = {'arg': 0, 'offset': SECOND_HELD + HELD_RELEASER - CALLABLE_STORAGE, 'pointer': 'other_releaser'}
    set_base_flag = {'arg': 0, 'offset': BASE_HELD + HELD_FLAGS - FIRST_MEMBER,
                     'bytes': struct.pack('<I', HELD_SET | 0x100).hex(), 'flags': HELD_SET | 0x100}
    move_base_releaser = {'arg': 0, 'offset': BASE_HELD + HELD_RELEASER - FIRST_MEMBER, 'pointer': 'other_releaser'}
    new_base_value = {'arg': 0, 'offset': BASE_HELD + HELD_VALUE - FIRST_MEMBER, 'bytes': _qword(0x0123456789abcdef)}
    return [c('nothing_held'),
            c('callable_outside_storage', callable_at='outside'),
            c('callable_in_storage', callable_at='inside'),
            c('string_on_heap', capacity=8),
            c('string_capacity_large', capacity=0x7f),
            c('string_capacity_all_bits', capacity=ALL_BITS),
            c('member_block_held', second_flags=HELD_SET),
            c('base_block_held', base_flags=HELD_SET),
            c('both_blocks_held_all_flags', second_flags=0xffffffff, base_flags=0xffffffff),
            c('flags_without_bit_0x40', second_flags=0xffffffbf, base_flags=0xbf),
            c('bit_0x40_in_second_byte', second_flags=0x4000, base_flags=0x40000000),
            c('everything', callable_at='outside', capacity=0x10, second_flags=0x41, base_flags=0xc0),
            # the callable's destruction raises the string's capacity: it is read after the call
            c('callable_raises_capacity', callable_at='inside', callable_writes=[raise_capacity]),
            # it sets or clears the member block's flag and moves its releaser: read after it
            c('callable_sets_held_flag', callable_at='inside', callable_writes=[set_second_flag, move_releaser]),
            c('callable_clears_held_flag', callable_at='inside', second_flags=HELD_SET,
              callable_writes=[clear_second_flag]),
            # the member at +0xd90's destructor sets the base block's flag, its releaser and value
            c('first_member_sets_base_flag', first_member_writes=[set_base_flag, move_base_releaser, new_base_value])]


def destructor_cases(address: str, value: int | None) -> list[dict]:
    return _destructor_cases(address, False)


def deleting_destructor_cases(address: str, value: int | None) -> list[dict]:
    return _destructor_cases(address, True)


# sorts_16_byte_elements_with_comparator (sort_16_byte_elements_family.cpp): an array of
# 16-byte elements (a key qword the fake comparator orders by, and a distinct tag qword) sorted
# over first..last. The comparator is a scripted stub at a fake address: the cases replay the
# quicksort in Python to script every call in order, comparing the array element's address and
# both elements' bytes (the pivot is a stack copy, so only its bytes). Some calls write into the
# element they are given, to show the pivot is copied before and the swaps read after them.
# Generated fixtures, not game data.
FAKE_BEFORE = '0x00500360'
ELEMENT = 0x10
BEFORE_TRUE, BEFORE_FALSE = 0x5a5a5a5a5a5a0001, 0x5a5a5a5a5a5a0100
MAX_STUBS = 64


def _element(key: int, tag: int) -> bytes:
    return struct.pack('<qQ', key, 0xa000000000000000 | tag)


def _replay_sort(elements: list, origin: int, first: int, last: int, rewrites: dict) -> list[dict]:
    """The quicksort over elements (index i at elements[origin + i]); rewrites: call number ->
    the element bytes that call stores through the array element it is given."""
    stubs = []

    def before(index: int, pivot: bytes, array_first: bool) -> bool:
        n = len(stubs)
        element = elements[origin + index]
        a, b = (element, pivot) if array_first else (pivot, element)
        result = struct.unpack_from('<q', a)[0] < struct.unpack_from('<q', b)[0]
        stub = {'address': FAKE_BEFORE, 'argc': 2, 'argi': [0 if array_first else 1],
                'argmem': [{'arg': 0, 'size': ELEMENT}, {'arg': 1, 'size': ELEMENT}],
                'ret': hex(BEFORE_TRUE if result else BEFORE_FALSE)}
        if n in rewrites:
            stub['writes'] = [{'arg': 0 if array_first else 1, 'offset': 0, 'bytes': rewrites[n].hex()}]
            elements[origin + index] = rewrites[n]
        stubs.append(stub)
        return result

    def sort(lo: int, hi: int) -> None:
        while True:
            middle = ((lo + hi) & 0xffffffff)
            middle = (middle - (1 << 32) if middle >= 1 << 31 else middle) >> 1
            pivot = elements[origin + middle]
            i, j = lo, hi
            while True:
                while before(i, pivot, True):
                    i += 1
                while before(j, pivot, False):
                    j -= 1
                if i <= j:
                    if i != j:
                        elements[origin + i], elements[origin + j] = elements[origin + j], elements[origin + i]
                    i += 1
                    j -= 1
                if i > j:
                    break
            if lo < j:
                sort(lo, j)
            if i >= hi:
                return
            lo = i

    sort(first, last)
    return stubs


def _sort_case(address: str, cid: str, keys: list, first: int, last: int, origin: int = 0,
               rewrites: dict | None = None) -> dict:
    """keys: the array's element keys; origin: the index the elements pointer is at."""
    elements = [_element(k, i) for i, k in enumerate(keys)]
    data = b''.join(elements).hex()
    stubs = _replay_sort(list(elements), origin, first, last, rewrites or {})
    assert len(stubs) <= MAX_STUBS, (cid, len(stubs))
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            # the indexes are 32-bit: the upper halves of their registers are filler
            'args': {'rdi': f'buf:elements+{origin * ELEMENT:#x}', 'rsi': hex(0xdead00000000 | (first & 0xffffffff)),
                     'rdx': hex(0xbeef00000000 | (last & 0xffffffff)), 'rcx': f'guest:{FAKE_BEFORE}'},
            'buffers': {'elements': {'size': len(keys) * ELEMENT, 'bytes': data}},
            'memory': [], 'imports': [], 'stubs': stubs}


def sort_cases(address: str, value: int | None) -> list[dict]:
    c = lambda cid, keys, first=0, last=None, **kw: _sort_case(address, cid, keys, first,
                                                                 len(keys) - 1 if last is None else last, **kw)
    return [c('one', [5]),
            c('two_in_order', [1, 2]),
            c('two_reversed', [2, 1]),
            c('two_equal', [3, 3]),
            c('three_in_order', [1, 2, 3]),
            c('three_reversed', [3, 2, 1]),
            c('three_mixed', [3, 1, 2]),
            c('five_mixed', [4, 1, 5, 2, 3]),
            c('five_with_equal_keys', [2, 2, 1, 2, 1]),
            c('five_equal', [7, 7, 7, 7, 7]),
            c('six_reversed', [6, 5, 4, 3, 2, 1]),
            c('seven_mixed', [3, 7, 1, 6, 2, 5, 4]),
            c('negative_keys', [0, -3, 9, -8, 4]),
            # only first..last move; the rest are not touched
            c('inner_range', [9, 8, 7, 6, 5, 4, 3, 2], first=2, last=5),
            # first past last: one pass of the partition still runs
            c('first_past_last', [4, 1, 3, 2], first=2, last=1),
            # negative indexes before the elements pointer (the middle rounds down)
            c('negative_first', [5, 3, 8, 1, 6], first=-2, last=2, origin=2),
            # a call rewrites the middle element: the pivot is the copy taken before
            c('comparator_rewrites_middle', [4, 1, 5, 2, 3], rewrites={2: _element(9, 0x77)}),
            # a call rewrites an element before it is swapped: the swap moves the new bytes
            c('comparator_rewrites_swapped', [6, 5, 4, 3, 2, 1], rewrites={1: _element(-1, 0x78)})]


# finds_id_pair_in_nested_hash_maps (nested_hash_lookup_family.cpp): real chained hash tables
# built here: an outer one at +0x8 of `owner` whose values point at inner tables. Each table's
# nodes (a list from its sentinel, the end) run bucket by bucket in id order; each bucket holds
# its first and last node as iterators (filler before the node), or the end twice when empty. The
# C library's ldiv is a scripted import (the quotient in rax, the remainder in rdx) comparing both
# arguments. Generated fixtures, not game data.
HASH_SEED, HASH_DIVISOR = 0xdeadbeef, 127773
NODE, BUCKET, TABLE = 0x20, 0x30, 0x58
TABLE_END, TABLE_BUCKETS, TABLE_MASK, TABLE_COUNT = 0x8, 0x28, 0x48, 0x50
NODE_NEXT, NODE_PREVIOUS, NODE_ID, NODE_VALUE = 0x0, 0x8, 0x10, 0x18
ITERATOR_NODE, BUCKET_LAST = 0x10, 0x18
OWNER_TABLE = 0x8


def _id_hash(key: int) -> tuple[int, int, int]:
    """(hash, quotient, remainder) as the code computes them."""
    quotient, remainder = divmod(key ^ HASH_SEED, HASH_DIVISOR)
    value = (16807 * remainder - 2836 * quotient) & ALL_BITS
    if value >= 1 << 63:
        value = (value + (1 << 63) - 1) & ALL_BITS
    return value, quotient, remainder


def _bucket(key: int, mask: int, count: int) -> int:
    index = _id_hash(key)[0] & mask
    if count <= index:
        index = (index - ((mask >> 1) + 1)) & ALL_BITS
    return index


def _hash_table(name: str, at: str, entries: list, mask: int, count: int, buffers: dict, memory: list) -> None:
    """A table at `at` (buf:NAME+off) over entries [(id, value buffer name or None)]."""
    runs = {}
    for key, value in entries:
        runs.setdefault(_bucket(key, mask, count), []).append((key, value))
    order = [entry for b in sorted(runs) for entry in sorted(runs[b], key=lambda e: e[0])]
    nodes, buckets = f'{name}_nodes', f'{name}_buckets'
    buffers[nodes] = {'size': NODE * (len(order) + 1), 'bytes': '93' * (NODE * (len(order) + 1))}
    buffers[buckets] = {'size': BUCKET * count, 'bytes': '71' * (BUCKET * count)}
    slot = lambda i: f'{nodes}+{NODE * i:#x}'          # node 0 is the end
    links = [0] + [i + 1 for i in range(len(order))] + [0]
    for a, b in zip(links, links[1:]):
        memory.append({'addr': f'buf:{nodes}+{NODE * a + NODE_NEXT:#x}', 'pointer': slot(b)})
        memory.append({'addr': f'buf:{nodes}+{NODE * b + NODE_PREVIOUS:#x}', 'pointer': slot(a)})
    for i, (key, value) in enumerate(order, 1):
        base = NODE * i
        memory.append({'addr': f'buf:{nodes}+{base + NODE_ID:#x}', 'bytes': struct.pack('<I', key).hex()})
        if value:
            memory.append({'addr': f'buf:{nodes}+{base + NODE_VALUE:#x}', 'pointer': value})
    for b in range(count):
        run = [i for i, entry in enumerate(order, 1) if _bucket(entry[0], mask, count) == b]
        first, last = (run[0], run[-1]) if run else (0, 0)
        memory.append({'addr': f'buf:{buckets}+{BUCKET * b + ITERATOR_NODE:#x}', 'pointer': slot(first)})
        memory.append({'addr': f'buf:{buckets}+{BUCKET * b + BUCKET_LAST + ITERATOR_NODE:#x}', 'pointer': slot(last)})
    buffer, off = at.split('+')
    off = int(off, 16)
    memory.append({'addr': f'buf:{buffer}+{off + TABLE_END:#x}', 'pointer': slot(0)})
    memory.append({'addr': f'buf:{buffer}+{off + TABLE_BUCKETS:#x}', 'pointer': buckets})
    memory.append({'addr': f'buf:{buffer}+{off + TABLE_MASK:#x}', 'bytes': _qword(mask)})
    memory.append({'addr': f'buf:{buffer}+{off + TABLE_COUNT:#x}', 'bytes': _qword(count)})


def _lookup(entries: list, mask: int, count: int, key: int):
    """What the code finds: the first entry of key in its bucket's run, or None."""
    run = sorted((e for e in entries if _bucket(e[0], mask, count) == _bucket(key, mask, count)), key=lambda e: e[0])
    for entry in run:
        if entry[0] >= key:
            return entry if entry[0] == key else None
    return None


OUTER_MASK, OUTER_COUNT = 7, 6
INNER_MASK, INNER_COUNT = 3, 3


def _hash_case(address: str, cid: str, outer: list, inner: dict, first: int, second: int) -> dict:
    """outer: the outer ids, each with an inner table name in `inner` (name -> its ids) or None."""
    buffers = {'owner': {'size': OWNER_TABLE + TABLE, 'bytes': 'c5' * (OWNER_TABLE + TABLE)},
               'ids': {'size': 8, 'bytes': struct.pack('<II', first, second).hex()}}
    memory = []
    _hash_table('outer', f'owner+{OWNER_TABLE:#x}', [(k, v) for k, v in outer], OUTER_MASK, OUTER_COUNT, buffers, memory)
    for name, ids in inner.items():
        buffers[name] = {'size': TABLE, 'bytes': 'd6' * TABLE}
        _hash_table(name, f'{name}+0x0', [(k, None) for k in ids], INNER_MASK, INNER_COUNT, buffers, memory)
    imports = []

    def divide(key):
        _, quotient, remainder = _id_hash(key)
        imports.append({'name': 'ldiv', 'argc': 2, 'ret': hex(quotient), 'rdx': hex(remainder)})

    divide(first)
    found = _lookup(outer, OUTER_MASK, OUTER_COUNT, first)
    if found:
        divide(second)
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i8',
            'args': {'rdi': 'buf:owner', 'rsi': 'buf:ids'}, 'buffers': buffers, 'memory': memory,
            'imports': imports, 'stubs': []}


def _ids_in_bucket(bucket: int, mask: int, count: int, n: int, start: int = 1) -> list:
    out, key = [], start
    while len(out) < n:
        if _bucket(key, mask, count) == bucket:
            out.append(key)
        key += 1
    return out


def _negative_hash(start: int) -> int:
    """The first id from start whose hash before the wrap is negative."""
    return next(k for k in range(start, start + 0x1000000) if 16807 * _id_hash(k)[2] < 2836 * _id_hash(k)[1])


def hash_lookup_cases(address: str, value: int | None) -> list[dict]:
    outer_bucket = lambda k: _bucket(k, OUTER_MASK, OUTER_COUNT)
    inner_bucket = lambda k: _bucket(k, INNER_MASK, INNER_COUNT)
    # Outer ids: three in one bucket (with ids in bucket 1 below, between and above them unused),
    # two in another, one in a folded bucket, one with the top bit and one whose hash wraps.
    one = _ids_in_bucket(1, OUTER_MASK, OUTER_COUNT, 6)
    a, below, gap, above = [one[1], one[3], one[5]], one[0], one[2], one[5] + 1
    while outer_bucket(above) != 1:
        above += 1
    b = _ids_in_bucket(4, OUTER_MASK, OUTER_COUNT, 2, 0x1000)
    folded = next(k for k in range(0x2000, 0x10000) if _id_hash(k)[0] & OUTER_MASK >= OUTER_COUNT)
    high = next(k for k in range(0x80000000, 0x80010000) if outer_bucket(k) == 3)
    wraps = _negative_hash(0x10000)
    used = {outer_bucket(k) for k in a + b + [folded, high, wraps]}
    empty = next(k for k in range(1, 0x10000) if outer_bucket(k) not in used)
    # Inner ids: three in bucket 0 (one missing between them and one above), two in bucket 2, and
    # one whose hash wraps.
    zero = _ids_in_bucket(0, INNER_MASK, INNER_COUNT, 5, 0x40)
    x, inner_missing = [zero[0], zero[2], zero[3]], zero[1]
    inner_above = zero[4]
    y = _ids_in_bucket(2, INNER_MASK, INNER_COUNT, 2, 0x400)
    inner_wraps = _negative_hash(0x20000)
    inner = {'inner_a': x + y + [inner_wraps], 'inner_b': [y[0]], 'inner_c': []}
    outer = [(a[0], 'inner_a'), (a[1], 'inner_b'), (a[2], 'inner_c'), (b[0], 'inner_a'), (b[1], 'inner_b'),
             (folded, 'inner_a'), (high, 'inner_a'), (wraps, 'inner_a')]
    c = lambda cid, first, second, entries=outer: _hash_case(address, cid, entries, inner, first, second)
    return [c('outer_bucket_empty', empty, x[0]),
            c('outer_table_empty', a[0], x[0], []),
            c('found_first_of_run', a[0], x[0]),
            c('found_last_of_inner_run', a[0], x[2]),
            c('found_inner_other_bucket', a[0], y[1]),
            c('found_inner_hash_wraps', a[0], inner_wraps),
            c('inner_id_missing_inside_run', a[0], inner_missing),
            c('inner_id_above_run', a[0], inner_above),
            c('inner_one_entry', a[1], y[0]),
            c('inner_one_entry_other_id', a[1], x[0]),
            c('inner_table_empty', a[2], x[0]),
            c('found_middle_of_run', a[1], y[0]),
            c('found_last_of_run', a[2], y[0]),
            c('found_second_bucket', b[1], y[0]),
            c('found_folded_bucket', folded, x[1]),
            c('found_top_bit_id', high, x[0]),
            c('found_outer_hash_wraps', wraps, y[1]),
            c('outer_id_below_run', below, x[0]),
            c('outer_id_inside_run', gap, x[0]),
            c('outer_id_above_run', above, x[0]),
            c('outer_id_zero', 0, x[0]),
            c('outer_id_all_bits', 0xffffffff, x[0]),
            c('outer_id_seed', HASH_SEED, x[0]),
            # equal ids: the first in id order (its inner table) is taken
            c('equal_outer_ids', a[0], y[0], [(a[0], 'inner_c'), (a[0], 'inner_b')] + outer[1:])]


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'stores_vtable_0x0579c8a0_releases_members_then_destroys': destructor_cases,
         'stores_vtable_0x0579c8a0_releases_members_destroys_then_deletes': deleting_destructor_cases,
         'sorts_16_byte_elements_with_comparator': sort_cases,
         'finds_id_pair_in_nested_hash_maps': hash_lookup_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'stores_vtable_0x0579c8a0_releases_members_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_vtable_0x0579c8a0_destroy_reads_first', 'bb_mutant_vtable_0x0579c8a0_destroy_then_delete'],
    'stores_vtable_0x0579c8a0_releases_members_destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_vtable_0x0579c8a0_delete_reads_first', 'bb_mutant_vtable_0x0579c8a0_delete_without_delete'],
    'sorts_16_byte_elements_with_comparator': ['bb_mutant_does_nothing', 'bb_mutant_sort_pivot_by_reference', 'bb_mutant_sort_pivot_at_first', 'bb_mutant_sort_recurses_right'],
    'finds_id_pair_in_nested_hash_maps': ['bb_mutant_does_nothing', 'bb_mutant_hash_pair_signed_ids', 'bb_mutant_hash_pair_without_fold', 'bb_mutant_hash_pair_first_only'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
