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

reallocates_array_of_4_byte_elements, reallocates_array_of_12_byte_elements and
grows_full_array_of_16_byte_elements: the copies of an engine array's reallocation (to a given
capacity, or the growth of a full array), for one element size each
(array_reallocation_family.cpp). Every member runs array64_reserve_cases.py's cases for its
element size: growing, shrinking below the count, capacities whose byte size wraps, an old buffer
that is only 4-aligned, a new buffer over the old one, and failed allocations; the growth runs
full arrays of several counts and arrays with room.

takes_and_erases_map_entry_at_0x3b0: the copies of 0x022a54d0, which takes the counted object
under an id out of the map at +0x3b0 (map_take_family.cpp). The cases build real red-black maps:
empty, one entry, seven entries (found at the root, the ends and inside; absent between, above
and below), an entry without an object, ids with the top bit set, id 0, equal ids (the first in
order is taken) and reference counts that wrap or are negative.

releases_slot_blocks_at_0x38: the copies of 0x02af24e0, which frees the blocks of a table of
parallel slot arrays (slot_blocks_family.cpp), keeping the slots or moving the later ones down.
The cases run both ways over empty, negative and small counts, with and without empty slots, and
with frees that rewrite an id, the count or the blocks array, which show the id is read before the
free and the count and the array after it (each with a control mutant).
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


# reallocates_array_of_4_byte_elements, reallocates_array_of_12_byte_elements and
# grows_full_array_of_16_byte_elements (array_reallocation_family.cpp): the cases of
# array64_reserve_cases.py's reallocation, for each element size. An engine array (`array`:
# begin, end, capacity end, allocator) over distinct element bytes (`old`) and a fake allocator
# whose allocate and free slots are scripted stubs (with a spare call each); the new buffer
# (`fresh`) holds other bytes. Generated fixtures, not game data.
FATAL_ERROR = '0x024b55b0'
TRANSACTION_FLAG_OFFSET = 0x057e4318
THREAD_BLOCK = 0x40
FAKE_ALLOCATE, FAKE_FREE = '0x00500200', '0x00500220'
ALLOCATE_SLOT, FREE_SLOT = 0x58, 0x70
ARRAY_ALLOCATOR = 0x20
MASK = 0xffffffffffffffff


def _u64(value: int) -> str:
    return struct.pack('<Q', value & MASK).hex()


def _element_bytes(seed: int, count: int, element: int) -> str:
    """count elements of distinct bytes, every byte different from its neighbours."""
    words = (count * element + 7) // 8
    data = b''.join(struct.pack('<Q', (0x0102030405060708 * (seed + i) ^ 0xa5a5a5a5a5a5a5a5) & MASK)
                    for i in range(words))
    return data[:count * element].hex()


def array_case(address: str, cid: str, element: int, size: int, capacity: int, new_capacity: int | None,
               null: bool = False, fails: bool = False, old_off: int = 0, overlap: bool = False) -> dict:
    """size/capacity: the old array's, in elements (null: no buffer at all); new_capacity: the
    argument (None: the growth, which takes none and allocates when the array is full)."""
    buffers, memory, stubs = {}, [], []

    def buf(name, n, fill):
        buffers[name] = {'size': n, 'bytes': fill * n if len(fill) == 2 else fill}

    def put(name, off, data):
        memory.append({'addr': f'buf:{name}+{off:#x}', 'bytes': data})

    def ptr(name, off, target):
        memory.append({'addr': f'buf:{name}+{off:#x}', 'pointer': target})

    buf('array', 0x30, 'cd')
    buf('heap', 0x10, '3c')
    buf('heapvt', 0x80, 'e7')
    ptr('array', ARRAY_ALLOCATOR, 'heap')
    ptr('heap', 0, 'heapvt')
    memory.append({'addr': f'buf:heapvt+{ALLOCATE_SLOT:#x}', 'guest': FAKE_ALLOCATE})
    memory.append({'addr': f'buf:heapvt+{FREE_SLOT:#x}', 'guest': FAKE_FREE})
    slots = max(capacity, 1)
    buf('old', old_off + element * slots + 0x18, '77')
    put('old', old_off, _element_bytes(0x11, slots, element))
    if null:
        for off in (8, 0x10, 0x18):
            put('array', off, _u64(0))
    else:
        ptr('array', 8, f'old+{old_off:#x}')
        ptr('array', 0x10, f'old+{old_off + element * size:#x}')
        ptr('array', 0x18, f'old+{old_off + element * capacity:#x}')
    if fails:
        fresh = 0
    elif overlap:
        fresh = f'buf:old+{old_off + element:#x}'
    else:
        grown = size + size // 2 + 1 if new_capacity is None else new_capacity & 0xff
        n = max(size, grown, 1)
        buf('fresh', element * n + 0x10, _element_bytes(0x51, n + 0x10, element)[:2 * (element * n + 0x10)])
        fresh = 'buf:fresh'
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': fresh})
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': 0})
    args = {'rdi': 'buf:array'}
    if new_capacity is not None:
        args['rsi'] = hex(new_capacity & MASK)
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': args,
         'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}
    if fails:
        # The thread block: fs:0 points at itself; the transaction flag (offset -0x20) is zero.
        buf('tls', THREAD_BLOCK + 8, '00')
        ptr('tls', THREAD_BLOCK, f'tls+{THREAD_BLOCK}')
        memory.append({'addr': f'0x{TRANSACTION_FLAG_OFFSET:08x}', 'bytes': struct.pack('<q', -0x20).hex()})
        c['gs'] = f'buf:tls+{THREAD_BLOCK}'
        stubs.append({'address': FATAL_ERROR, 'argc': 3})
    if not null:
        stubs.append({'address': FAKE_FREE, 'argc': 2})
        stubs.append({'address': FAKE_FREE, 'argc': 2})
    return c


def reallocation_cases(element: int):
    def cases_for(address: str, value: int | None) -> list[dict]:
        def case(*a, **k):
            return array_case(address, a[0], element, *a[1:], **k)
        return [case('empty_initial_0x20', 0, 0, 0x20, null=True),
                case('grow_3_to_8', 3, 4, 8),
                case('grow_full_5_to_9', 5, 5, 9),
                case('same_capacity', 4, 4, 4),
                case('allocated_but_empty', 0, 6, 7),
                case('one_element', 1, 1, 2),
                case('eight_elements', 8, 8, 12),
                case('shrink_below_size', 6, 6, 2),
                case('capacity_0', 2, 3, 0),
                case('capacity_upper_bits', 2, 2, (1 << 32) | 3),
                case('capacity_byte_size_wraps', 2, 2, (1 << 62) | 3),
                case('old_buffer_4_aligned', 3, 3, 6, old_off=4),
                case('new_buffer_over_the_old', 4, 6, 6, overlap=True),
                case('fails_empty', 0, 0, 0x20, null=True, fails=True),
                case('fails_with_one_element', 1, 4, 8, fails=True)]
    return cases_for


def growth_cases(element: int):
    def cases_for(address: str, value: int | None) -> list[dict]:
        def case(cid, size, capacity, **k):
            return array_case(address, cid, element, size, capacity, None, **k)
        # Full arrays grow to one and a half times the count, at least one more (0 -> 1, 1 -> 2,
        # 2 -> 3, 3 -> 4, 4 -> 6, 9 -> 13); arrays with room, even one slot, are left alone.
        return [case('empty_no_buffer', 0, 0, null=True),
                case('full_1', 1, 1),
                case('full_2', 2, 2),
                case('full_3', 3, 3),
                case('full_4', 4, 4),
                case('full_9', 9, 9),
                case('allocated_but_empty_room', 0, 3),
                case('one_slot_left', 4, 5),
                case('half_full', 3, 6),
                case('full_old_buffer_4_aligned', 3, 3, old_off=4),
                case('full_new_buffer_over_the_old', 4, 4, overlap=True),
                case('fails_empty', 0, 0, null=True, fails=True),
                case('fails_full_1', 1, 1, fails=True)]
    return cases_for


# takes_and_erases_map_entry_at_0x3b0 (map_take_family.cpp): an object whose map at +0x3b0 (its
# head at +0x3b8) is a real red-black layout over `nodes` (0x30 bytes each: left, parent, right,
# color, nil flag, the 32-bit key at +0x20, the object pointer at +0x28); the head is the only nil
# node and every leaf link points back to it. The objects are counted (vtable, count at +0x8). The
# erase (0x022a8a80) is a stub comparing the map and the node of the iterator it gets on the stack
# (GP slot 8; the iterator's first 16 bytes are whatever the caller's stack holds). The result
# slot starts as other bytes. Generated fixtures, not game data.
MAP_ERASE = '0x022a8a80'
MAP_OFFSET = 0x3b0
NODE_SIZE = 0x30
ITERATOR_NODE_SLOT = 8


def _map_case(address: str, cid: str, keys: list[int], key: int, objects: dict[int, int] | None = None,
              references: int = 1) -> dict:
    """keys: the node keys in order (ties allowed); objects: node index -> object index (default:
    node i holds object i; -1: no object). The tree is the balanced one over the keys."""
    count = len(keys)
    objects = {i: i for i in range(count)} if objects is None else objects
    buffers = {'owner': {'size': MAP_OFFSET + 0x20, 'bytes': 'cd' * (MAP_OFFSET + 0x20)},
               'head': {'size': NODE_SIZE, 'bytes': '5a' * NODE_SIZE},
               'out': {'size': 8, 'bytes': 'ee' * 8},
               'key': {'size': 4, 'bytes': struct.pack('<I', key).hex()},
               'vtable': {'size': 8, 'bytes': '00' * 8}}
    memory = []
    if count:
        buffers['nodes'] = {'size': NODE_SIZE * count, 'bytes': '3c' * (NODE_SIZE * count)}
        buffers['objects'] = {'size': 0x10 * count, 'bytes': '00' * (0x10 * count)}

    def node(i):
        return 'head' if i is None else f'nodes+{i * NODE_SIZE:#x}'

    def link(owner, off, target):
        memory.append({'addr': f'buf:{owner}+{off:#x}' if '+' not in owner else
                       f'buf:{owner.split("+")[0]}+{int(owner.split("+")[1], 16) + off:#x}', 'pointer': target})

    def build(lo, hi, parent):
        if lo >= hi:
            return None
        mid = (lo + hi) // 2
        link(node(mid), 0x08, node(parent))
        left, right = build(lo, mid, mid), build(mid + 1, hi, mid)
        link(node(mid), 0x00, node(left))
        link(node(mid), 0x10, node(right))
        return mid

    root = build(0, count, None)
    for i in range(count):
        at = i * NODE_SIZE
        memory.append({'addr': f'buf:nodes+{at + 0x18:#x}', 'bytes': '0100'})   # color, not nil
        memory.append({'addr': f'buf:nodes+{at + 0x20:#x}', 'bytes': struct.pack('<I', keys[i]).hex()})
        held = objects.get(i, -1)
        if held < 0:
            memory.append({'addr': f'buf:nodes+{at + 0x28:#x}', 'bytes': _u64(0)})
        else:
            memory.append({'addr': f'buf:nodes+{at + 0x28:#x}', 'pointer': f'objects+{held * 0x10:#x}'})
            memory.append({'addr': f'buf:objects+{held * 0x10:#x}', 'pointer': 'vtable'})
            memory.append({'addr': f'buf:objects+{held * 0x10 + 8:#x}',
                           'bytes': struct.pack('<i', references + held).hex()})
    link('head', 0x00, node(0 if count else None))
    link('head', 0x08, node(root))
    link('head', 0x10, node(count - 1 if count else None))
    memory.append({'addr': 'buf:head+0x18', 'bytes': '0101'})   # black, nil
    link('owner', MAP_OFFSET + 8, 'head')
    memory.append({'addr': f'buf:owner+{MAP_OFFSET + 0x10:#x}', 'bytes': _u64(count)})
    stubs = [{'address': MAP_ERASE, 'argc': 2, 'argi': [1, ITERATOR_NODE_SLOT]},
             {'address': MAP_ERASE, 'argc': 2, 'argi': [1, ITERATOR_NODE_SLOT]}]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:out', 'rsi': 'buf:owner', 'rdx': 'buf:key'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def map_take_cases(address: str, value: int | None) -> list[dict]:
    seven = [3, 8, 0x10, 0x20, 0x31, 0x40, 0x77]
    return [_map_case(address, 'empty_map', [], 5),
            _map_case(address, 'one_entry_found', [5], 5),
            _map_case(address, 'one_entry_key_above', [5], 6),
            _map_case(address, 'one_entry_key_below', [5], 4),
            _map_case(address, 'found_at_root', seven, 0x20),
            _map_case(address, 'found_leftmost', seven, 3),
            _map_case(address, 'found_rightmost', seven, 0x77),
            _map_case(address, 'found_inner', seven, 0x31),
            _map_case(address, 'absent_between', seven, 0x21),
            _map_case(address, 'absent_above_all', seven, 0x78),
            _map_case(address, 'absent_below_all', seven, 2),
            _map_case(address, 'found_without_object', seven, 0x10, objects={0: 0, 1: 1, 3: 3, 4: 4, 5: 5, 6: 6}),
            _map_case(address, 'unsigned_keys_found', [1, 0x7fffffff, 0x80000000, 0xffffffff], 0x80000000),
            _map_case(address, 'unsigned_keys_absent', [1, 0x80000000, 0xffffffff], 0x7fffffff),
            _map_case(address, 'key_zero', [0, 1, 2], 0),
            # equal keys: the first in order (a left subtree) is taken, not the root
            _map_case(address, 'equal_keys_first_taken', [5, 5, 5], 5),
            _map_case(address, 'references_wrap', [5, 9], 9, references=0x7fffffff - 1),
            _map_case(address, 'references_negative', [5, 9], 5, references=-3)]


# releases_slot_blocks_at_0x38 (slot_blocks_family.cpp): a table (`table`: the byte at +0x8, the
# count at +0x1c, the id arrays at +0x28 and +0x30, the blocks at +0x38) over `slots` slots of
# distinct ids; a slot's block points 8 bytes into its own small buffer, and the global 0x056c45d0
# points at a scripted free comparing the block start. Some frees write into the table (a block
# whose start is the table, or the id array) to show which reads come after them: the id kept
# across the free, the count and the blocks array read again. Generated fixtures, not game data.
FREE_BLOCK_GLOBAL = 0x056c45d0
FAKE_FREE_BLOCK = '0x00500240'
BLOCK_HEADER = 0x8
TABLE_SIZE = 0x40
TABLE_COUNT, TABLE_IDS, TABLE_SECOND_IDS, TABLE_BLOCKS = 0x1c, 0x28, 0x30, 0x38


def _slot_case(address: str, cid: str, keep: int, count: int, slots: int, empty: tuple = (),
               frees: dict | None = None) -> dict:
    """empty: slots with no block; frees: slot -> (where the block starts: 'table' or 'ids', the
    writes its free makes through it)."""
    frees = frees or {}
    buffers = {'table': {'size': TABLE_SIZE, 'bytes': 'cd' * TABLE_SIZE},
               'ids': {'size': 4 * slots, 'bytes': ''.join(struct.pack('<I', 0x1000 + 0x111 * i).hex()
                                                           for i in range(slots))},
               'second': {'size': 4 * slots, 'bytes': '5a' * (4 * slots)},
               'blocks': {'size': 8 * slots, 'bytes': '00' * (8 * slots)},
               'other_blocks': {'size': 8 * slots, 'bytes': '00' * (8 * slots)},
               'held': {'size': 0x20 * slots, 'bytes': '3c' * (0x20 * slots)}}
    memory = [{'addr': f'buf:table+{TABLE_COUNT:#x}', 'bytes': struct.pack('<i', count).hex()},
              {'addr': f'buf:table+{TABLE_IDS:#x}', 'pointer': 'ids'},
              {'addr': f'buf:table+{TABLE_SECOND_IDS:#x}', 'pointer': 'second'},
              {'addr': f'buf:table+{TABLE_BLOCKS:#x}', 'pointer': 'blocks'},
              {'addr': f'0x{FREE_BLOCK_GLOBAL:08x}', 'guest': FAKE_FREE_BLOCK}]
    order = range(count - 1, -1, -1) if keep else range(max(count, 0))
    stubs = []
    for slot in range(slots):
        if slot in empty:
            continue
        start, writes = frees.get(slot, (f'held+{0x20 * slot:#x}', []))
        memory.append({'addr': f'buf:blocks+{8 * slot:#x}', 'pointer': f'{start.split("+")[0]}+'
                       f'{(int(start.split("+")[1], 16) if "+" in start else 0) + BLOCK_HEADER:#x}'})
        # The other array (after a swap) holds the same blocks.
        memory.append({'addr': f'buf:other_blocks+{8 * slot:#x}', 'pointer': f'held+{0x20 * slot + BLOCK_HEADER:#x}'})
    for slot in order:
        if slot in empty or slot >= slots:
            continue
        _, writes = frees.get(slot, (None, []))
        stub = {'address': FAKE_FREE_BLOCK, 'argc': 1, 'argi': [0]}
        if writes:
            stub['writes'] = writes
        stubs.append(stub)
    stubs.append({'address': FAKE_FREE_BLOCK, 'argc': 1, 'argi': [0]})
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:table', 'rsi': hex(keep)},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def slot_blocks_cases(address: str, value: int | None) -> list[dict]:
    def count_to(n):
        return [{'arg': 0, 'offset': TABLE_COUNT, 'bytes': struct.pack('<i', n).hex()}]

    swap = [{'arg': 0, 'offset': TABLE_BLOCKS, 'pointer': 'other_blocks'}]
    new_id = [{'arg': 0, 'offset': 4 * 2, 'bytes': struct.pack('<I', 0xfeedf00d).hex()}]
    return [_slot_case(address, 'keep_count_0', 1, 0, 4),
            _slot_case(address, 'keep_count_negative', 1, -1, 4),
            _slot_case(address, 'keep_one', 1, 1, 3),
            _slot_case(address, 'keep_four', 1, 4, 6),
            _slot_case(address, 'keep_four_some_empty', 1, 4, 6, empty=(0, 2)),
            # the free of slot 2 rewrites slot 2's id: the id read before it is kept
            _slot_case(address, 'keep_id_read_before_free', 1, 4, 6, frees={2: ('ids', new_id)}),
            # the free of slot 3 (the first) swaps the blocks array: the rest is cleared in the new one
            _slot_case(address, 'keep_blocks_read_after_free', 1, 4, 6, frees={3: ('table', swap)}),
            _slot_case(address, 'keep_free_changes_count', 1, 3, 6, frees={2: ('table', count_to(6))}),
            _slot_case(address, 'clear_count_0', 0, 0, 4),
            _slot_case(address, 'clear_count_negative', 0, -2, 4),
            _slot_case(address, 'clear_one', 0, 1, 3),
            _slot_case(address, 'clear_three', 0, 3, 8),
            _slot_case(address, 'clear_three_some_empty', 0, 3, 8, empty=(1, 4)),
            # the first free raises the count: the later slots take the blocks and ids 3 further on
            _slot_case(address, 'clear_count_read_after_free', 0, 3, 8, frees={0: ('table', count_to(5))}),
            _slot_case(address, 'clear_count_lowered', 0, 3, 8, frees={0: ('table', count_to(1))}),
            _slot_case(address, 'clear_blocks_read_after_free', 0, 3, 8, frees={1: ('table', swap)})]


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'reallocates_array_of_4_byte_elements': reallocation_cases(4),
         'reallocates_array_of_12_byte_elements': reallocation_cases(12),
         'grows_full_array_of_16_byte_elements': growth_cases(16),
         'takes_and_erases_map_entry_at_0x3b0': map_take_cases,
         'releases_slot_blocks_at_0x38': slot_blocks_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'reallocates_array_of_4_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array_reserve_8_byte_elements', 'bb_mutant_array32_reserve_keeps_old_buffer'],
    'reallocates_array_of_12_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array_reserve_8_byte_elements', 'bb_mutant_array96_reserve_keeps_old_buffer'],
    'grows_full_array_of_16_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array128_grows_by_one', 'bb_mutant_array128_grows_with_room'],
    'takes_and_erases_map_entry_at_0x3b0': ['bb_mutant_does_nothing', 'bb_mutant_map_take_keeps_entry', 'bb_mutant_map_take_without_reference', 'bb_mutant_map_take_signed_ids'],
    'releases_slot_blocks_at_0x38': ['bb_mutant_does_nothing', 'bb_mutant_slot_blocks_id_after_free', 'bb_mutant_slot_blocks_count_once', 'bb_mutant_slot_blocks_array_once'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
