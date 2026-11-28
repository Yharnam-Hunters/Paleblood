#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/allocator_forward_and_equality_families.cpp's families, which
family_cases.py runs on every member at its own address. Every callee is a scripted stub (or, for
memcmp, a scripted import) comparing its arguments; vtables are buffers whose slots point at fake
addresses that the cases stub. Generated fixtures, not game data.

frees_8_byte_block_through_0x05ac8288, frees_sized_block_aligned_8_through_0x05ac8288,
allocates_aligned_8_through_0x05ac8288 and allocates_with_alignment_through_0x05ac8288: the
allocator global 0x05ac8288 set to a buffer, to null, to all bits, or left as the image loads it;
pointers that are buffers, null or all bits; sizes 0, 1, 8, odd, large and all bits; alignments
1, 8, 16, 0x1000, 0 and odd; allocations that answer a buffer, null or all bits (compared).

resets_with_0x02c375e0_and_returns_object: an object, null and a large value, with 0x02c375e0
answering something else than the object (the object itself is returned).

releases_value_at_0x8_with_0x005c14d0_then_clears_kind: a holder whose +0x8 points at a value
with kinds 0, 5, 9, 12 and 0xff (the release sees the kind before it is cleared), and releases
that move or clear the holder's +0x8 or rewrite the kind (the value is read once, before the
release, and its kind cleared after).

equals_by_kind_and_16_bytes_at_0x10: two objects whose slot 0 answers kinds that are equal,
differ, or differ only above the low byte; memcmp answers 0, 1, -1 and values whose low 32 bits
are 0; one object passed twice; and kind calls that replace the second object's table or either
object's key pointer (the second table and both keys are read after the calls).
"""
from __future__ import annotations

ALLOCATOR_GLOBAL = 0x05ac8288
ALLOCATE = '0x02fa1b80'
SIZED_FREE = '0x02fa1c20'
RESET = '0x02c375e0'
RELEASE = '0x005c14d0'
KIND_FIRST = '0x00500600'   # what the first object's slot 0 points at
KIND_SECOND = '0x00500620'  # the second object's slot 0
KIND_OTHER = '0x00500640'   # the slot 0 of a table a kind call installs
MASK = 0xffffffffffffffff
ALIGNMENT = 8
FIXED_SIZE = 8
KEY = 0x10
KEY_SIZE = 16
SECOND = 0x40  # where the second object lives in the block
VALUE_FIELD = 0x8
HOLDER = 0x20  # where the holder lives in the block that also holds the value


def _pattern(size: int, seed: int) -> str:
    return bytes((seed * 31 + i * 11) & 0xff for i in range(size)).hex()


def _qword(value: int) -> str:
    return (value & MASK).to_bytes(8, 'little').hex()


def _allocator_memory(allocator: str) -> list[dict]:
    """allocator: 'buf' (the global points at a buffer), 'image' (left as loaded), or a value."""
    if allocator == 'buf':
        return [{'addr': hex(ALLOCATOR_GLOBAL), 'pointer': 'allocator'}]
    if allocator == 'image':
        return []
    return [{'addr': hex(ALLOCATOR_GLOBAL), 'bytes': _qword(int(allocator, 0))}]


def _forward_case(address: str, cid: str, returns: str, args: dict, allocator: str, stub: dict) -> dict:
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': {'allocator': {'size': 0x40, 'bytes': _pattern(0x40, 3)},
                        'block': {'size': 0x40, 'bytes': _pattern(0x40, 5)}},
            'memory': _allocator_memory(allocator), 'imports': [], 'stubs': [stub]}


# frees_8_byte_block_through_0x05ac8288: (block) -> sized free (allocator, block, 8, 8)
def free_8_cases(address: str, value: int | None) -> list[dict]:
    rows = [('block', 'buf:block', 'buf', '0x0'), ('null_block', '0x0', 'buf', '0x0'),
            ('all_bits_block', hex(MASK), 'buf', '0x1'), ('image_allocator', 'buf:block', 'image', '0x0'),
            ('null_allocator', 'buf:block', '0x0', '0x0'), ('all_bits_allocator', 'buf:block', hex(MASK), hex(MASK)),
            ('free_answers', 'buf:block+0x8', 'buf', '0x123456789abcdef0')]
    return [_forward_case(address, cid, 'void', {'rdi': block, 'rsi': '0x1111111111111111'}, allocator,
                          {'address': SIZED_FREE, 'argc': 4, 'ret': ret})
            for cid, block, allocator, ret in rows]


# frees_sized_block_aligned_8_through_0x05ac8288: (block, size) -> sized free (allocator, block, size, 8)
def free_sized_cases(address: str, value: int | None) -> list[dict]:
    rows = [('size_8', 'buf:block', 8, 'buf'), ('size_0', 'buf:block', 0, 'buf'), ('size_1', 'buf:block', 1, 'buf'),
            ('size_0x18', 'buf:block', 0x18, 'buf'), ('size_odd', 'buf:block+0x8', 0x1235, 'buf'),
            ('size_all_bits', 'buf:block', MASK, 'buf'), ('null_block', '0x0', 0x40, 'buf'),
            ('all_bits_block', hex(MASK), 0x40, 'buf'), ('image_allocator', 'buf:block', 0x20, 'image'),
            ('null_allocator', 'buf:block', 0x20, '0x0')]
    return [_forward_case(address, cid, 'void', {'rdi': block, 'rsi': hex(size), 'rdx': '0x2222222222222222'},
                          allocator, {'address': SIZED_FREE, 'argc': 4, 'ret': '0x0'})
            for cid, block, size, allocator in rows]


# allocates_aligned_8_through_0x05ac8288: (size) -> allocate (allocator, size, 8)
def allocate_8_cases(address: str, value: int | None) -> list[dict]:
    rows = [('size_0x40', 0x40, 'buf', 'buf:block'), ('size_0', 0, 'buf', 'buf:block'),
            ('size_1', 1, 'buf', 'buf:block+0x8'), ('size_odd', 0x1235, 'buf', 'buf:block'),
            ('size_all_bits', MASK, 'buf', '0x0'), ('fails', 0x40, 'buf', '0x0'),
            ('answers_all_bits', 0x40, 'buf', hex(MASK)), ('image_allocator', 0x20, 'image', 'buf:block'),
            ('null_allocator', 0x20, '0x0', 'buf:block'), ('all_bits_allocator', 0x20, hex(MASK), 'buf:block')]
    return [_forward_case(address, cid, 'i64', {'rdi': hex(size), 'rsi': '0x1111111111111111'}, allocator,
                          {'address': ALLOCATE, 'argc': 3, 'ret': ret})
            for cid, size, allocator, ret in rows]


# allocates_with_alignment_through_0x05ac8288: (size, alignment) -> allocate (allocator, size, alignment)
def allocate_cases(address: str, value: int | None) -> list[dict]:
    rows = [('align_8', 0x40, 8, 'buf', 'buf:block'), ('align_1', 0x40, 1, 'buf', 'buf:block'),
            ('align_16', 0x40, 16, 'buf', 'buf:block'), ('align_0x1000', 0x2000, 0x1000, 'buf', 'buf:block'),
            ('align_0', 0x40, 0, 'buf', 'buf:block'), ('align_odd', 0x40, 3, 'buf', 'buf:block+0x8'),
            ('size_equals_align', 0x10, 0x10, 'buf', 'buf:block'), ('size_0', 0, 8, 'buf', '0x0'),
            ('size_all_bits', MASK, 8, 'buf', '0x0'), ('align_all_bits', 0x40, MASK, 'buf', hex(MASK)),
            ('fails', 0x40, 16, 'buf', '0x0'), ('image_allocator', 0x20, 8, 'image', 'buf:block'),
            ('null_allocator', 0x20, 4, '0x0', 'buf:block')]
    return [_forward_case(address, cid, 'i64', {'rdi': hex(size), 'rsi': hex(align), 'rdx': '0x2222222222222222'},
                          allocator, {'address': ALLOCATE, 'argc': 3, 'ret': ret})
            for cid, size, align, allocator, ret in rows]


# resets_with_0x02c375e0_and_returns_object
def reset_cases(address: str, value: int | None) -> list[dict]:
    rows = [('object', 'buf:object', '0x5a5a5a5a00000001'), ('object_reset_answers_zero', 'buf:object', '0x0'),
            ('object_reset_answers_itself', 'buf:object', 'buf:object'), ('null', '0x0', '0x77'),
            ('large_value', '0xfedcba9876543210', hex(MASK))]
    return [{'schema': 1, 'address': address, 'id': cid, 'returns': 'i64', 'args': {'rdi': obj},
             'buffers': {'object': {'size': 0x20, 'bytes': _pattern(0x20, 7)}}, 'memory': [], 'imports': [],
             'stubs': [{'address': RESET, 'argc': 1, 'ret': ret}]}
            for cid, obj, ret in rows]


# releases_value_at_0x8_with_0x005c14d0_then_clears_kind: the value at block+0 and the holder at
# block+0x20, so the release (given the value) can rewrite the holder's +0x8 through its argument.
def _release_case(address: str, cid: str, kind: int, change: str | None = None) -> dict:
    stub = {'address': RELEASE, 'argc': 1, 'ret': '0x5a5a5a5a5a5a5a5a',
            'argmem': [{'arg': 0, 'offset': 0, 'size': 0x10}]}
    field = HOLDER + VALUE_FIELD
    if change == 'move':
        stub['writes'] = [{'arg': 0, 'offset': field, 'pointer': 'other'}]
    elif change == 'clear':
        stub['writes'] = [{'arg': 0, 'offset': field, 'bytes': _qword(0)}]
    elif change == 'kind':
        stub['writes'] = [{'arg': 0, 'offset': 0, 'bytes': '07'}]
    elif change == 'move_and_kind':
        stub['writes'] = [{'arg': 0, 'offset': field, 'pointer': 'other'}, {'arg': 0, 'offset': 1, 'bytes': 'a5a5'}]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': {'rdi': f'buf:block+{HOLDER:#x}'},
            'buffers': {'block': {'size': 0x40, 'bytes': _pattern(0x40, 9)},
                        'other': {'size': 0x10, 'bytes': _pattern(0x10, 11)}},
            'memory': [{'addr': 'buf:block+0x0', 'bytes': f'{kind:02x}'},
                       {'addr': f'buf:block+{field:#x}', 'pointer': 'block'}],
            'imports': [], 'stubs': [stub]}


def release_cases(address: str, value: int | None) -> list[dict]:
    return [_release_case(address, 'kind_0', 0), _release_case(address, 'kind_5', 5),
            _release_case(address, 'kind_9', 9), _release_case(address, 'kind_12', 12),
            _release_case(address, 'kind_all_bits', 0xff), _release_case(address, 'release_moves_value', 9, 'move'),
            _release_case(address, 'release_clears_holder', 6, 'clear'),
            _release_case(address, 'release_rewrites_kind', 10, 'kind'),
            _release_case(address, 'release_moves_and_rewrites', 8, 'move_and_kind')]


# equals_by_kind_and_16_bytes_at_0x10: the two objects share one block (first at FIRST, second at
# FIRST ^ SECOND), so either kind call can rewrite the other object through its argument.
def _equal_case(address: str, cid: str, kinds: tuple[int, int], compare: int | None, *,
                writes: list | None = None, second_writes: list | None = None, swapped: bool = False,
                same: bool = False, second_kind: str = KIND_SECOND) -> dict:
    first, second = (SECOND, 0) if swapped else (0, SECOND)
    if same:
        second = first
    memory = [{'addr': f'buf:block+{first:#x}', 'pointer': 'table_first'},
              {'addr': f'buf:block+{first + KEY:#x}', 'pointer': 'key_first'},
              {'addr': 'buf:table_first+0x0', 'guest': KIND_FIRST},
              {'addr': 'buf:table_second+0x0', 'guest': KIND_SECOND},
              {'addr': 'buf:table_other+0x0', 'guest': KIND_OTHER}]
    if not same:
        memory += [{'addr': f'buf:block+{second:#x}', 'pointer': 'table_second'},
                   {'addr': f'buf:block+{second + KEY:#x}', 'pointer': 'key_second'}]
    one = {'address': KIND_FIRST, 'argc': 1, 'ret': hex(kinds[0])}
    two = {'address': KIND_FIRST if same else second_kind, 'argc': 1, 'ret': hex(kinds[1])}
    if writes:
        one['writes'] = writes
    if second_writes:
        two['writes'] = second_writes
    imports = []
    if compare is not None:
        imports = [{'name': 'memcmp', 'argc': 3, 'ret': hex(compare),
                    'argmem': [{'arg': 0, 'size': KEY_SIZE}, {'arg': 1, 'size': KEY_SIZE}]}]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i8',
            'args': {'rdi': f'buf:block+{first:#x}', 'rsi': f'buf:block+{second:#x}'},
            'buffers': {'block': {'size': 0x80, 'bytes': _pattern(0x80, 13)},
                        'table_first': {'size': 0x10, 'bytes': _pattern(0x10, 15)},
                        'table_second': {'size': 0x10, 'bytes': _pattern(0x10, 17)},
                        'table_other': {'size': 0x10, 'bytes': _pattern(0x10, 19)},
                        'key_first': {'size': 0x20, 'bytes': _pattern(0x20, 21)},
                        'key_second': {'size': 0x20, 'bytes': _pattern(0x20, 23)},
                        'key_other': {'size': 0x20, 'bytes': _pattern(0x20, 25)}},
            'memory': memory, 'imports': imports, 'stubs': [one, two]}


def equal_cases(address: str, value: int | None) -> list[dict]:
    other_table = [{'arg': 0, 'offset': SECOND, 'pointer': 'table_other'}]
    first_key = [{'arg': 0, 'offset': KEY, 'pointer': 'key_other'}]
    second_key = [{'arg': 0, 'offset': SECOND + KEY, 'pointer': 'key_other'}]
    return [_equal_case(address, 'equal', (3, 3), 0),
            _equal_case(address, 'keys_differ', (3, 3), 1),
            _equal_case(address, 'keys_less', (3, 3), 0xffffffff),
            _equal_case(address, 'keys_differ_above_32_bits', (3, 3), 0x100000000),
            _equal_case(address, 'keys_differ_high_half', (3, 3), 0xffffffff00000000),
            _equal_case(address, 'kind_0', (0, 0), 0),
            _equal_case(address, 'kind_all_bits', (0xff, 0xff), 0),
            _equal_case(address, 'kinds_differ', (3, 4), None),
            _equal_case(address, 'kinds_differ_high_bit', (0x83, 0x03), None),
            _equal_case(address, 'kinds_equal_low_byte', (0x1234567800001203, 0x3403), 0),
            _equal_case(address, 'kinds_differ_low_byte', (0x1203, 0x1204), None),
            _equal_case(address, 'same_object', (5, 5), 0, same=True),
            _equal_case(address, 'first_call_replaces_second_table', (6, 6), 0, writes=other_table,
                        second_kind=KIND_OTHER),
            _equal_case(address, 'first_call_replaces_second_table_differs', (6, 7), None, writes=other_table,
                        second_kind=KIND_OTHER),
            _equal_case(address, 'first_call_moves_first_key', (2, 2), 0, writes=first_key),
            _equal_case(address, 'first_call_moves_second_key', (2, 2), 1, writes=second_key),
            _equal_case(address, 'second_call_moves_second_key', (2, 2), 0, second_writes=first_key),
            _equal_case(address, 'second_call_moves_first_key', (2, 2), 0, swapped=True, second_writes=second_key)]
