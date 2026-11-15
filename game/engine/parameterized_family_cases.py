#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's relative families whose copies differ in one immediate
(symbols/families.csv); game/engine/family_cases.py merges CASES and MUTANTS into its own.

- calls_virtual_on_object_found_by_0x02c4f560: the handle's kind and index (positive, negative,
  the extremes) given to the finder, a stub returning an object whose table's slot K is a stub.
- clears_hk_array_freeing_to_0x058018b0: owned buffers of several capacities (one whose shift
  wraps at 32 bits, and capacity 0), and a buffer the array does not own (no free).
- calls_0x005c9770_on_member_unless_self: the third argument the object itself (true without a
  call), and another one with the dispatcher's result false and true.
- allocates_page_into_table_from_0x02795780: an index below the capacity, at it with pages (the
  table doubles and the pages are copied), past it, and with no pages yet (16 slots).
"""
from __future__ import annotations

FIND_OBJECT = '0x02c4f560'
VIRTUAL = '0x004000a0'           # a stand-in for the found object's slot K, stubbed
BUF_FREE = '0x004000a0'          # a stand-in for the container allocator's slot 0x28, stubbed
CONTAINER_ALLOCATOR = '0x058018b0'
DISPATCH = '0x005c9770'
ARENA_ALLOCATE = '0x02795780'
RAX_SEED = '0x5aa5c33c0ff0e11e'


def pattern(seed: int, size: int) -> str:
    return bytes((seed * 7 + i) & 0xff for i in range(size)).hex()


def found_object_case(address: str, case_id: str, kind: int, index: int, slot: int, seed: int) -> dict:
    """The handle's +8 kind and +0xa index; the finder (stubbed, its three arguments compared)
    returns the object, whose table's slot K is a stub comparing the object and the context."""
    handle = bytearray.fromhex(pattern(seed, 0x10))
    handle[8:10] = kind.to_bytes(2, 'little')
    handle[10:12] = (index & 0xffff).to_bytes(2, 'little')
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i64',
            'buffers': {'handle': {'size': 0x10, 'bytes': handle.hex()},
                        'context': {'size': 0x20, 'bytes': pattern(seed + 1, 0x20)},
                        'object': {'size': 0x10, 'bytes': pattern(seed + 2, 0x10)},
                        'table': {'size': 0x80}},
            'memory': [{'addr': 'buf:object', 'pointer': 'table'},
                       {'addr': f'buf:table+{slot:#x}', 'guest': VIRTUAL}],
            'imports': [],
            'stubs': [{'address': FIND_OBJECT, 'argc': 3, 'ret': 'buf:object'},
                      {'address': VIRTUAL, 'argc': 2, 'ret': seed << 40 | 0x5eed}],
            'args': {'rdi': 'buf:handle', 'rsi': 'buf:context', 'rdx': '0x2222222222222222',
                     'rcx': '0x3333333333333333', 'rax': RAX_SEED}}


def found_object_cases(address: str, value: int | None) -> list[dict]:
    return [found_object_case(address, 'kind_1', 1, 5, value, 3),
            found_object_case(address, 'kind_2_negative_index', 2, -3, value, 5),
            found_object_case(address, 'extremes', 0xffff, -0x8000, value, 7),
            found_object_case(address, 'largest_index', 0x8001, 0x7fff, value, 11)]


def hk_array_clear_case(address: str, case_id: str, size: int, capacity_and_flags: int, seed: int) -> dict:
    """The array (data, size, capacity and flags); the container allocator's table (written over
    its first qword) has a stub in slot 0x28 comparing the allocator, the buffer and the low 32
    bits of the size in bytes (an int)."""
    array = bytearray.fromhex(pattern(seed, 0x18))
    array[8:12] = size.to_bytes(4, 'little')
    array[12:16] = capacity_and_flags.to_bytes(4, 'little')
    owned = capacity_and_flags < 0x80000000   # not owned: no stub, the call would be unscripted
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'array': {'size': 0x18, 'bytes': array.hex()},
                        'data': {'size': 0x20, 'bytes': pattern(seed + 1, 0x20)},
                        'allocator_table': {'size': 0x30}},
            'memory': [{'addr': 'buf:array', 'pointer': 'data'},
                       {'addr': CONTAINER_ALLOCATOR, 'pointer': 'allocator_table'},
                       {'addr': 'buf:allocator_table+0x28', 'guest': BUF_FREE}],
            'imports': [],
            'stubs': [{'address': BUF_FREE, 'argc': 2, 'argi': [0, 1], 'argu32': [2], 'ret': seed}] if owned else [],
            'args': {'rdi': 'buf:array', 'rsi': '0x1111111111111111', 'rdx': '0xffffffffffffffff',
                     'rax': RAX_SEED}}
    return case


def hk_array_clear_cases(address: str, value: int | None) -> list[dict]:
    return [hk_array_clear_case(address, 'owned', 3, 5, 3),
            hk_array_clear_case(address, 'owned_large', 0x10, 0x00012345, 5),
            hk_array_clear_case(address, 'owned_wrapping', 1, 0x40000003, 7),
            hk_array_clear_case(address, 'owned_empty', 0, 0, 11),
            hk_array_clear_case(address, 'not_owned', 7, 0x80000009, 13),
            hk_array_clear_case(address, 'not_owned_all_flags', 2, 0xffffffff, 17)]


def dispatch_case(address: str, case_id: str, third: str, result: int | None, seed: int) -> dict:
    """The object, a first argument and THIRD; the dispatcher (stubbed, its three arguments
    compared) returns RESULT, or is not called."""
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i8',
            'buffers': {'object': {'size': 0x100, 'bytes': pattern(seed, 0x100)},
                        'other': {'size': 0x20, 'bytes': pattern(seed + 1, 0x20)}},
            'memory': [], 'imports': [],
            'stubs': [] if result is None else [{'address': DISPATCH, 'argc': 3, 'ret': result}],
            'args': {'rdi': 'buf:object', 'rsi': hex(0x0101010101010101 * seed), 'rdx': third,
                     'rcx': '0x3333333333333333', 'rax': RAX_SEED}}


def dispatch_cases(address: str, value: int | None) -> list[dict]:
    return [dispatch_case(address, 'self', 'buf:object', None, 3),
            dispatch_case(address, 'other_false', 'buf:other', 0x5eed_0000_0000_0100, 5),
            dispatch_case(address, 'other_true', 'buf:other', 0x5eed_0000_0000_0001, 7),
            dispatch_case(address, 'inside_object', f'buf:object+{value:#x}', 0x7f00, 11)]


ARENA = 0x7700_1234_5000           # the table's arena: only passed on, never read


def page_table_case(address: str, case_id: str, count: int, capacity: int, index: int, pages: bool,
                    seed: int) -> dict:
    """The table (arena, count at +0x10, capacity at +0x18, pages at +0x20). The arena allocate
    is a stub comparing the arena and the size: the grown or first page array (when the index is
    not below the capacity), then the page; memcpy copies the old pages when they grow."""
    table = bytearray.fromhex(pattern(seed, 0x28))
    table[0:8] = ARENA.to_bytes(8, 'little')
    table[0x10:0x18] = count.to_bytes(8, 'little')
    table[0x18:0x20] = capacity.to_bytes(8, 'little')
    if not pages:
        table[0x20:0x28] = bytes(8)
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'table': {'size': 0x28, 'bytes': table.hex()},
                        'pages': {'size': 0x100, 'bytes': pattern(seed + 1, 0x100)},
                        'grown': {'size': 0x100, 'bytes': pattern(seed + 2, 0x100)},
                        'page': {'size': 0x10}},
            'memory': [{'addr': 'buf:table+0x20', 'pointer': 'pages'}] if pages else [],
            'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:table', 'rsi': hex(index), 'rdx': '0x2222222222222222',
                     'rax': RAX_SEED}}
    if capacity <= index:
        case['stubs'].append({'address': ARENA_ALLOCATE, 'argc': 2, 'ret': 'buf:grown'})
        if pages:
            case['imports'].append({'name': 'memcpy', 'argc': 3, 'ret': 'buf:grown'})
    case['stubs'].append({'address': ARENA_ALLOCATE, 'argc': 2, 'ret': 'buf:page'})
    return case


def page_table_cases(address: str, value: int | None) -> list[dict]:
    return [page_table_case(address, 'below_capacity', 5, 16, 3, True, 3),
            page_table_case(address, 'at_capacity', 4, 4, 4, True, 5),
            page_table_case(address, 'past_capacity', 2, 2, 3, True, 7),
            page_table_case(address, 'at_capacity_empty', 0, 1, 1, True, 11),
            page_table_case(address, 'no_pages', 0, 0, 2, False, 13)]


CASES = {'calls_virtual_on_object_found_by_0x02c4f560': found_object_cases,
         'clears_hk_array_freeing_to_0x058018b0': hk_array_clear_cases,
         'calls_0x005c9770_on_member_unless_self': dispatch_cases,
         'allocates_page_into_table_from_0x02795780': page_table_cases}

# No call (and no write), and the neighbouring value's replacement (verify adds it).
MUTANTS = {name: ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'] for name in CASES}
