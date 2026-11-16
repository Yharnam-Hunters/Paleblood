#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/array_resize_families.cpp's families, which family_cases.py runs on
every member at its own address. Generated fixtures, not game data.

reallocates_array_of_8_byte_4_aligned_elements and reallocates_array_of_16_byte_elements: an
engine array (`array`: begin, end, capacity end, allocator) over distinct element bytes (`old`)
and a fake allocator whose allocate and free slots are scripted stubs (with a spare call each);
the new buffer (`fresh`) holds other bytes. The cases are those of array64_reserve_cases.py:
growing, full, the same capacity, allocated but empty, a capacity below the count, capacity 0,
capacities whose byte size wraps, an old buffer that is only 4-aligned, one and eight elements, a
new buffer one element past the old one's start (the copy runs over its own source) and failed
allocations over an empty array and one with a single element.

reallocates_array_of_24_byte_handle_records: the same layout with 24-byte records, whose handle
copies (0x02fa7870) and destructions (0x02fa7940) are scripted stubs, one per record and a spare.
The array header lies in the old buffer after the records, so a stub can rewrite it through its
own argument: a first copy that moves the end back or on shows the copies run to the end read
before them while the destruction and the count use the end read after them; a first
destruction that moves the begin or swaps the allocator shows the free reads both after the
destruction (each with a control mutant).

resizes_heap_array_of_8_byte_elements: an array (`array`: data, count, capacity) and the heap the
global at 0x057e7320 points at, whose reallocate, free and allocate slots are scripted stubs (with
a spare call each). The owner passed through to allocate is a buffer of its own (the callers pass
the array itself; one case does too). The cases grow from no buffer and from one, within the
capacity and to it, shrink above, at and below half the capacity, to zero with and without a
buffer, round capacities up to four, take null buffers back from reallocate and allocate, and use
counts whose quarter-more growth wraps to zero (freeing the buffer) or to the capacity (keeping it).
"""
from __future__ import annotations

import struct

FATAL_ERROR = '0x024b55b0'
TRANSACTION_FLAG_OFFSET = 0x057e4318
THREAD_BLOCK = 0x40
FAKE_ALLOCATE, FAKE_FREE = '0x00500200', '0x00500220'
ALLOCATE_SLOT, FREE_SLOT = 0x58, 0x70
ARRAY_ALLOCATOR = 0x20
MASK = 0xffffffffffffffff

ARRAY_HEAP = '0x057e7320'
FAKE_HEAP_REALLOCATE, FAKE_HEAP_FREE, FAKE_HEAP_ALLOCATE = '0x00500240', '0x00500260', '0x00500280'
HEAP_REALLOCATE_SLOT, HEAP_FREE_SLOT, HEAP_ALLOCATE_SLOT = 0x60, 0x68, 0x70
HEAP_ARRAY_COUNT, HEAP_ARRAY_CAPACITY = 0x8, 0x10


def _u64(value: int) -> str:
    return struct.pack('<Q', value & MASK).hex()


def _element_bytes(seed: int, count: int, element: int) -> str:
    """count elements of distinct bytes, every byte different from its neighbours."""
    words = (count * element + 7) // 8
    data = b''.join(struct.pack('<Q', (0x0102030405060708 * (seed + i) ^ 0xa5a5a5a5a5a5a5a5) & MASK)
                    for i in range(words))
    return data[:count * element].hex()


def _failure_report(c: dict, buf, ptr, memory: list, stubs: list) -> None:
    """The thread block for a failed allocation: fs:0 points at itself, the transaction flag
    (offset -0x20) is zero, so the fatal error is reported."""
    buf('tls', THREAD_BLOCK + 8, '00')
    ptr('tls', THREAD_BLOCK, f'tls+{THREAD_BLOCK}')
    memory.append({'addr': f'0x{TRANSACTION_FLAG_OFFSET:08x}', 'bytes': struct.pack('<q', -0x20).hex()})
    c['gs'] = f'buf:tls+{THREAD_BLOCK}'
    stubs.append({'address': FATAL_ERROR, 'argc': 3})


def _builder():
    buffers, memory = {}, []

    def buf(name, n, fill):
        buffers[name] = {'size': n, 'bytes': fill * n if len(fill) == 2 else fill}

    def put(name, off, data):
        memory.append({'addr': f'buf:{name}+{off:#x}', 'bytes': data})

    def ptr(name, off, target):
        memory.append({'addr': f'buf:{name}+{off:#x}', 'pointer': target})

    return buffers, memory, buf, put, ptr


def array_case(address: str, cid: str, element: int, size: int, capacity: int, new_capacity: int,
               null: bool = False, fails: bool = False, old_off: int = 0, overlap: bool = False) -> dict:
    """size/capacity: the old array's, in elements (null: no buffer at all); new_capacity: the
    argument."""
    buffers, memory, buf, put, ptr = _builder()
    stubs = []
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
        n = max(size, new_capacity & 0xff, 1)
        buf('fresh', element * n + 0x10, _element_bytes(0x51, n + 0x10, element)[:2 * (element * n + 0x10)])
        fresh = 'buf:fresh'
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': fresh})
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': 0})
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
         'args': {'rdi': 'buf:array', 'rsi': hex(new_capacity & MASK)},
         'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}
    if fails:
        _failure_report(c, buf, ptr, memory, stubs)
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


def heap_array_case(address: str, cid: str, count: int, capacity: int, new_count: int, data: bool = True,
                    returns_null: bool = False, owner_is_array: bool = False) -> dict:
    """count/capacity: the array's before the call (data: whether it has a buffer); new_count: the
    argument; returns_null: reallocate and allocate give back no buffer."""
    buffers, memory, buf, put, ptr = _builder()
    buf('array', 0x28, 'cd')
    buf('owner', 0x10, '5a')
    buf('heap', 0x10, '3c')
    buf('heapvt', 0x80, 'e7')
    buf('old', 0x40, _element_bytes(0x21, 8, 8))
    buf('fresh', 0x40, _element_bytes(0x61, 8, 8))
    memory.append({'addr': ARRAY_HEAP, 'pointer': 'heap'})
    ptr('heap', 0, 'heapvt')
    memory.append({'addr': f'buf:heapvt+{HEAP_REALLOCATE_SLOT:#x}', 'guest': FAKE_HEAP_REALLOCATE})
    memory.append({'addr': f'buf:heapvt+{HEAP_FREE_SLOT:#x}', 'guest': FAKE_HEAP_FREE})
    memory.append({'addr': f'buf:heapvt+{HEAP_ALLOCATE_SLOT:#x}', 'guest': FAKE_HEAP_ALLOCATE})
    if data:
        ptr('array', 0, 'old')
    else:
        put('array', 0, _u64(0))
    put('array', HEAP_ARRAY_COUNT, _u64(count))
    put('array', HEAP_ARRAY_CAPACITY, _u64(capacity))
    fresh = 0 if returns_null else 'buf:fresh'
    # The allocate slot is given a pointer to a stack dword: compare its bytes, not its address.
    allocate = {'address': FAKE_HEAP_ALLOCATE, 'argc': 4, 'argi': [0, 1, 2],
                'argmem': [{'arg': 3, 'offset': 0, 'size': 4}]}
    stubs = [{'address': FAKE_HEAP_REALLOCATE, 'argc': 3, 'ret': fresh},
             {'address': FAKE_HEAP_REALLOCATE, 'argc': 3, 'ret': 0},
             {'address': FAKE_HEAP_FREE, 'argc': 2},
             {'address': FAKE_HEAP_FREE, 'argc': 2},
             dict(allocate, ret=fresh),
             dict(allocate, ret=0)]
    owner = 'buf:array' if owner_is_array else 'buf:owner'
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:array', 'rsi': owner, 'rdx': hex(new_count & MASK)},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


# Counts whose growth by a quarter wraps: to zero, and to one.
WRAPS_TO_ZERO = 0xcccccccccccccccd
WRAPS_TO_ONE = 0xccccccccccccccce


def heap_resize_cases(address: str, value: int | None) -> list[dict]:
    def case(*a, **k):
        return heap_array_case(address, *a, **k)
    return [case('grow_no_buffer_to_1', 0, 0, 1, data=False),
            case('grow_no_buffer_to_5', 0, 0, 5, data=False),
            case('grow_no_buffer_owner_is_array', 0, 0, 3, data=False, owner_is_array=True),
            case('grow_3_to_5', 3, 4, 5),
            case('grow_7_to_9', 7, 8, 9),
            case('grow_to_12_rounds_to_16', 8, 8, 12),
            case('grow_huge_byte_size_wraps', 2, 4, (1 << 61) | 5),
            case('grow_within_capacity', 2, 8, 6),
            case('grow_to_capacity', 3, 8, 8),
            case('same_count', 4, 8, 4),
            case('same_count_over_capacity', 6, 4, 6),
            case('grow_wraps_to_zero_frees', 2, 4, WRAPS_TO_ZERO),
            case('grow_wraps_to_zero_no_buffer', 0, 0, WRAPS_TO_ZERO, data=False),
            case('grow_wraps_to_capacity_kept', 0, 1, WRAPS_TO_ONE),
            case('shrink_above_half', 6, 8, 5),
            case('shrink_to_half', 6, 8, 4),
            case('shrink_below_half', 10, 16, 3),
            case('shrink_odd_capacity', 8, 9, 3),
            case('shrink_to_zero', 5, 8, 0),
            case('shrink_to_zero_no_buffer', 5, 8, 0, data=False),
            case('shrink_to_zero_capacity_1', 1, 1, 0),
            case('shrink_without_buffer', 10, 16, 2, data=False),
            case('reallocate_returns_null', 3, 4, 6, returns_null=True),
            case('allocate_returns_null', 0, 0, 2, data=False, returns_null=True),
            case('shrink_reallocate_returns_null', 9, 16, 1, returns_null=True)]


HANDLE_COPY, HANDLE_DESTROY = '0x02fa7870', '0x02fa7940'
RECORD = 0x18
ARRAY_BEGIN, ARRAY_END = 0x8, 0x10


def handle_record_case(address: str, cid: str, size: int, capacity: int, new_capacity: int, null: bool = False,
                       fails: bool = False, overlap: bool = False, copy_sets_end: int | None = None,
                       destroy_sets_begin: bool = False, destroy_sets_allocator: bool = False) -> dict:
    """reallocates_array_of_24_byte_handle_records: the array header lives in the `old` buffer
    after its records, so the scripted handle copies and destructions can rewrite it through
    their own arguments. copy_sets_end: the first copy moves the end to that many records;
    destroy_sets_begin / destroy_sets_allocator: the first destruction moves the begin one record
    on / swaps in a second allocator."""
    buffers, memory, buf, put, ptr = _builder()
    stubs = []
    slots = max(capacity, size, 1)
    header = RECORD * slots + 0x20
    buf('old', header + 0x30, '77')
    put('old', 0, _element_bytes(0x31, slots, RECORD))
    buf('heap', 0x10, '3c')
    buf('heapvt', 0x80, 'e7')
    buf('heap2', 0x10, '4b')
    ptr('old', header + ARRAY_ALLOCATOR, 'heap')
    ptr('heap', 0, 'heapvt')
    ptr('heap2', 0, 'heapvt')
    memory.append({'addr': f'buf:heapvt+{ALLOCATE_SLOT:#x}', 'guest': FAKE_ALLOCATE})
    memory.append({'addr': f'buf:heapvt+{FREE_SLOT:#x}', 'guest': FAKE_FREE})
    if null:
        for off in (8, 0x10, 0x18):
            put('old', header + off, _u64(0))
    else:
        ptr('old', header + ARRAY_BEGIN, 'old')
        ptr('old', header + ARRAY_END, f'old+{RECORD * size:#x}')
        ptr('old', header + 0x18, f'old+{RECORD * capacity:#x}')
    if fails:
        fresh = 0
    elif overlap:
        fresh = f'buf:old+{RECORD:#x}'
    else:
        n = max(size, new_capacity & 0xff, 1)
        buf('fresh', RECORD * n + 0x10, _element_bytes(0x71, n + 1, RECORD)[:2 * (RECORD * n + 0x10)])
        fresh = 'buf:fresh'
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': fresh})
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': 0})
    copies = size if not fails else size - 1
    for i in range(copies + 1):
        copy = {'address': HANDLE_COPY, 'argc': 2}
        if i == 0 and copy_sets_end is not None:
            # The source of the first copy is the first old record, at the buffer's start.
            copy['writes'] = [{'arg': 1, 'offset': header + ARRAY_END, 'pointer': f'old+{RECORD * copy_sets_end:#x}'}]
        stubs.append(copy)
    for i in range(max(size, copy_sets_end or 0) + 1):
        destroy = {'address': HANDLE_DESTROY, 'argc': 1}
        writes = []
        if i == 0 and destroy_sets_begin:
            writes.append({'arg': 0, 'offset': header + ARRAY_BEGIN, 'pointer': f'old+{RECORD:#x}'})
        if i == 0 and destroy_sets_allocator:
            writes.append({'arg': 0, 'offset': header + ARRAY_ALLOCATOR, 'pointer': 'heap2'})
        if writes:
            destroy['writes'] = writes
        stubs.append(destroy)
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
         'args': {'rdi': f'buf:old+{header:#x}', 'rsi': hex(new_capacity & MASK)},
         'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}
    if fails:
        _failure_report(c, buf, ptr, memory, stubs)
    if not null:
        stubs.append({'address': FAKE_FREE, 'argc': 2})
        stubs.append({'address': FAKE_FREE, 'argc': 2})
    return c


def handle_record_cases(address: str, value: int | None) -> list[dict]:
    def case(*a, **k):
        return handle_record_case(address, *a, **k)
    return [case('empty_initial_0x20', 0, 0, 0x20, null=True),
            case('grow_3_to_8', 3, 4, 8),
            case('grow_full_5_to_9', 5, 5, 9),
            case('same_capacity', 4, 4, 4),
            case('allocated_but_empty', 0, 6, 7),
            case('one_record', 1, 1, 2),
            case('eight_records', 8, 8, 12),
            case('shrink_below_size', 6, 6, 2),
            case('capacity_0', 2, 3, 0),
            case('capacity_byte_size_wraps', 2, 2, (1 << 62) | 3),
            case('new_buffer_over_the_old', 4, 6, 6, overlap=True),
            case('copy_moves_end_back', 3, 4, 6, copy_sets_end=1),
            case('copy_moves_end_on', 2, 4, 6, copy_sets_end=4),
            case('destroy_moves_begin', 3, 4, 6, destroy_sets_begin=True),
            case('destroy_swaps_allocator', 3, 4, 6, destroy_sets_allocator=True),
            case('fails_empty', 0, 0, 0x20, null=True, fails=True),
            case('fails_with_one_record', 1, 4, 8, fails=True)]


HANDLE_ASSIGN = '0x02fa7bc0'


def erase_case(address: str, cid: str, size: int, first: int, last: int, null: bool = False,
               assign_sets_end: int | None = None, destroy_sets_begin: bool = False) -> dict:
    """erases_range_of_24_byte_handle_records over `size` records, erasing [first, last) (record
    indices; null: no buffer, both iterators null). The array header lies in the `old` buffer after
    room for eight records. assign_sets_end: the first assignment moves the end to that many
    records; destroy_sets_begin: the first destruction moves the begin one record on."""
    buffers, memory, buf, put, ptr = _builder()
    slots = 8
    header = RECORD * slots + 0x20
    buf('old', header + 0x30, '77')
    put('old', 0, _element_bytes(0x41, slots, RECORD))
    buf('result', 0x18, 'ab')
    if null:
        for off in (8, 0x10, 0x18):
            put('old', header + off, _u64(0))
    else:
        ptr('old', header + ARRAY_BEGIN, 'old')
        ptr('old', header + ARRAY_END, f'old+{RECORD * size:#x}')
        ptr('old', header + 0x18, f'old+{RECORD * slots:#x}')
    put('old', header + ARRAY_ALLOCATOR, _u64(0x5d5d5d5d5d5d5d5d))

    def iterator(index: int, seed: int) -> list:
        record = 0 if null else f'buf:old+{RECORD * index:#x}'
        return [hex(0x1111111111111111 * seed), hex(0x0101010101010101 * (seed + 0x20)), record]
    stubs = []
    whole = first == 0 and last == size
    end = assign_sets_end if assign_sets_end is not None else size
    moved = 0 if whole or first == last or last == size else size - last
    for i in range(moved + 1):
        assign = {'address': HANDLE_ASSIGN, 'argc': 2}
        if i == 0 and assign_sets_end is not None:
            # The first assignment's target is record `first`.
            assign['writes'] = [{'arg': 0, 'offset': header + ARRAY_END - RECORD * first,
                                 'pointer': f'old+{RECORD * assign_sets_end:#x}'}]
        stubs.append(assign)
    destroyed = size if whole else (0 if first == last else (end - (first + moved) if moved else last - first))
    for i in range(max(destroyed, 0) + 1):
        destroy = {'address': HANDLE_DESTROY, 'argc': 1}
        if i == 0 and destroy_sets_begin:
            # The first destruction's record is the first one (the whole array is erased).
            destroy['writes'] = [{'arg': 0, 'offset': header + ARRAY_BEGIN, 'pointer': f'old+{RECORD:#x}'}]
        stubs.append(destroy)
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:result', 'rsi': f'buf:old+{header:#x}'},
            'stack': iterator(first, 1) + iterator(last, 2),
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def erase_cases(address: str, value: int | None) -> list[dict]:
    def case(*a, **k):
        return erase_case(address, *a, **k)
    return [case('whole_array', 3, 0, 3),
            case('whole_array_of_one', 1, 0, 1),
            case('whole_empty_array', 0, 0, 0),
            case('whole_no_buffer', 0, 0, 0, null=True),
            case('empty_range_inside', 4, 2, 2),
            case('empty_range_at_end', 4, 4, 4),
            case('empty_range_at_begin', 4, 0, 0),
            case('front_one', 4, 0, 1),
            case('front_three_of_five', 5, 0, 3),
            case('middle_two_of_five', 5, 1, 3),
            case('middle_one_of_six', 6, 2, 3),
            case('tail_from_two_of_five', 5, 2, 5),
            case('last_one_of_five', 5, 4, 5),
            case('assign_moves_end_on', 4, 0, 1, assign_sets_end=6),
            case('assign_moves_end_on_middle', 5, 1, 2, assign_sets_end=7),
            case('whole_destroy_moves_begin', 3, 0, 3, destroy_sets_begin=True)]
