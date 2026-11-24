#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for allocates_page_into_table_from_0x02795780 (game/engine/arena_page_table.h),
which family_cases.py runs on every member at its own address. Generated fixtures, not game data.

A table (arena, count at +0x10, capacity at +0x18, pages at +0x20) and an index. The arena
allocate is a stub comparing the arena and the size, and memcpy a stub comparing its three
arguments: the cases are an index below the capacity (only the page is allocated), at it with
pages (the table doubles and the counted pages are copied), past it, at it with a capacity of 1
and no counted pages, with no pages yet (16 slots), and the 64-bit and unsigned extremes below.

Every case scripts every path, not only the right one: three allocations (the right ones first,
then a spare) and a memcpy, each returning a 32-slot buffer the case owns. A replacement that
takes the wrong branch (grows when it should not, or the other way) then makes calls the
original does not and stores into case buffers, a difference the comparison records, instead of
stopping at an unscripted call.
"""
from __future__ import annotations

ARENA_ALLOCATE = '0x02795780'
RAX_SEED = '0x5aa5c33c0ff0e11e'


def pattern(seed: int, size: int) -> str:
    return bytes((seed * 7 + i) & 0xff for i in range(size)).hex()


ARENA = 0x7700_1234_5000           # the table's arena: only passed on, never read
SLOT_BUFFER = 0x100                # 32 slots: room for every index below, on every path


def page_table_case(address: str, case_id: str, count: int, capacity: int, index: int, pages: bool,
                    seed: int) -> dict:
    """The table (arena, count at +0x10, capacity at +0x18, pages at +0x20). The arena allocate
    is a stub comparing the arena and the size: the grown or first page array (when the index is
    not below the capacity), then the page, then a spare for a wrong extra allocation; memcpy, a
    stub that copies nothing, observes the copy of the old pages when they grow."""
    table = bytearray.fromhex(pattern(seed, 0x28))
    table[0:8] = ARENA.to_bytes(8, 'little')
    table[0x10:0x18] = count.to_bytes(8, 'little')
    table[0x18:0x20] = capacity.to_bytes(8, 'little')
    if not pages:
        table[0x20:0x28] = bytes(8)
    allocations = ['grown', 'page', 'spare'] if capacity <= index else ['page', 'spare', 'spare']
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'table': {'size': 0x28, 'bytes': table.hex()},
                        'pages': {'size': SLOT_BUFFER, 'bytes': pattern(seed + 1, SLOT_BUFFER)},
                        'grown': {'size': SLOT_BUFFER, 'bytes': pattern(seed + 2, SLOT_BUFFER)},
                        'page': {'size': SLOT_BUFFER, 'bytes': pattern(seed + 3, SLOT_BUFFER)},
                        'spare': {'size': SLOT_BUFFER, 'bytes': pattern(seed + 4, SLOT_BUFFER)}},
            'memory': [{'addr': 'buf:table+0x20', 'pointer': 'pages'}] if pages else [],
            'imports': [{'name': 'memcpy', 'argc': 3, 'ret': 'buf:grown'}],
            'stubs': [{'address': ARENA_ALLOCATE, 'argc': 2, 'ret': f'buf:{name}'} for name in allocations],
            'args': {'rdi': 'buf:table', 'rsi': hex(index), 'rdx': '0x2222222222222222',
                     'rax': RAX_SEED}}


def cases(address: str, value: int | None) -> list[dict]:
    return [page_table_case(address, 'below_capacity', 5, 16, 3, True, 3),
            page_table_case(address, 'at_capacity', 4, 4, 4, True, 5),
            page_table_case(address, 'past_capacity', 2, 2, 3, True, 7),
            page_table_case(address, 'at_capacity_empty', 0, 1, 1, True, 11),
            page_table_case(address, 'no_pages', 0, 0, 2, False, 13),
            # A count past 32 bits (the count is a 64-bit field), a capacity with the top bit set
            # (the index compare is unsigned, so nothing grows), and a capacity without pages
            # (the empty table is told by the missing pages, not by the capacity).
            page_table_case(address, 'count_high', 0xffffffff, 16, 3, True, 17),
            page_table_case(address, 'capacity_high_bit', 5, 1 << 63, 3, True, 19),
            page_table_case(address, 'no_pages_with_capacity', 0, 2, 3, False, 23)]
