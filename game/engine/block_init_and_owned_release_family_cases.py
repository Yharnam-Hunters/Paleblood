#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/block_init_and_owned_release_families.cpp's families, which
family_cases.py runs on every member at its own address. Every callee is a scripted stub comparing
its arguments; vtables are buffers whose slots point at fake addresses that the cases stub.
Generated fixtures, not game data.

initializes_owner_block_six_pointers_to_0x05ac5cac,
initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc and
initializes_owner_block_first_pointer_to_0x05ac74a3: a block of 0x90 bytes (the last 0x10 must
stay as they were) filled with zeros, all bits or a pattern, also placed 8 bytes into a larger
buffer; owners null, all bits, a value and buffers (the block itself too). The result (32 bits)
is compared.

destroys_pair_owner_with_vtable_0x0570fb90: one block holds object 1 (+0x0), object 2 (+0x20),
the owner (+0x40) and object 3 (+0x60), so a release can rewrite the owner's fields through its
object argument. No objects, either or both, first releases that clear or swap the second field
(read after the first release), a second release that stores a first object again (cleared at
the end) and sees the first field already cleared.

destroys_entry_storage_with_vtable_0x057045e0: counts 0, 1, 3, the largest, ones whose byte count
wraps in 32 bits, -1 and 0x80000000 (no free), a null storage, and storage that is the owner
itself, so the free sees the vtable and the cleared +0x20 already stored and its writes to the
storage field and the count are overwritten afterwards. The allocator object's vtable is a buffer
stored at 0x058018b0 for the case.

releases_counted_object_at_0x10: one block holds counted object 1 (+0x0), counted object 2
(+0x20) and the owner (+0x40). No object; strong and weak counts on both sides of 1, both wraps
from 0, the alive bit set or clear, other top bits, all bits; destroys that rewrite the counts
(read again after the destroy); and calls of 0x027286b0 that clear or swap the field (read again
after the call).
"""
from __future__ import annotations

import struct

MASK = 0xffffffffffffffff


def _qword(value: int) -> str:
    return struct.pack('<Q', value & MASK).hex()


def _u32(value: int) -> str:
    return struct.pack('<I', value & 0xffffffff).hex()


def _pattern(size: int, seed: int) -> str:
    return bytes((i * 13 + seed) & 0xff for i in range(size)).hex()


def _fill(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}


def _case(address: str, cid: str, returns: str, args: dict, buffers: dict, memory: list, stubs: list) -> dict:
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


# The three owner block families
BLOCK_SIZE = 0x90


def _block_case(address: str, cid: str, owner: str, fill: str, block_at: int = 0) -> dict:
    size = BLOCK_SIZE + block_at
    content = _pattern(size, 0x31) if fill == 'pattern' else fill * size
    buffers = {'block': {'size': size, 'bytes': content},
               'owner': {'size': 0x10, 'bytes': '5b' * 0x10}}
    args = {'rdi': owner, 'rsi': f'buf:block+{block_at:#x}' if block_at else 'buf:block'}
    return _case(address, cid, 'i32', args, buffers, [], [])


def block_cases(address: str, value: int | None) -> list[dict]:
    return [_block_case(address, 'zeros_owner_buffer', 'buf:owner', '00'),
            _block_case(address, 'all_bits_owner_null', '0x0', 'ff'),
            _block_case(address, 'pattern_owner_all_bits', hex(MASK), 'pattern'),
            _block_case(address, 'pattern_owner_value', '0x0fedcba987654321', 'pattern'),
            _block_case(address, 'owner_is_block', 'buf:block', 'c3'),
            _block_case(address, 'block_inside_buffer', 'buf:owner', 'pattern', block_at=8)]


# destroys_pair_owner_with_vtable_0x0570fb90
PAIR_RELEASE = '0x00500500'   # what the objects' vtable slot 0x18 points at
PAIR_AT, PAIR_SECOND_AT, PAIR_THIRD_AT = 0x40, 0x20, 0x60


def _pair_case(address: str, cid: str, first: str | None, second: str | None, change: str | None) -> dict:
    """FIRST/SECOND: 'one', 'two', 'three' (objects in the block) or None. CHANGE:
    'clear_second' / 'swap_second' (object 1's release), 'set_first' (object 2's)."""
    where = {'one': 0, 'two': PAIR_SECOND_AT, 'three': PAIR_THIRD_AT}
    memory = [{'addr': f'buf:block+{at:#x}', 'pointer': 'objvt'} for at in where.values()]
    memory.append({'addr': 'buf:objvt+0x18', 'guest': PAIR_RELEASE})
    for field, value in ((8, first), (0x10, second)):
        addr = f'buf:block+{PAIR_AT + field:#x}'
        if value is None:
            memory.append({'addr': addr, 'bytes': _qword(0)})
        else:
            memory.append({'addr': addr, 'pointer': f'block+{where[value]:#x}'})
    stubs = []
    if first:
        release = {'address': PAIR_RELEASE, 'argc': 1, 'ret': '0x0'}
        if change == 'clear_second':
            release['writes'] = [{'arg': 0, 'offset': PAIR_AT + 0x10, 'bytes': _qword(0)}]
            second = None
        elif change == 'swap_second':
            release['writes'] = [{'arg': 0, 'offset': PAIR_AT + 0x10, 'pointer': f'block+{PAIR_THIRD_AT:#x}'}]
            second = 'three'
        stubs.append(release)
        if second:
            release = {'address': PAIR_RELEASE, 'argc': 1, 'ret': '0x0'}
            if second == 'two':
                # The first field is already cleared when the second object is released.
                release['argmem'] = [{'arg': 0, 'offset': PAIR_AT + 8 - PAIR_SECOND_AT, 'size': 8}]
                if change == 'set_first':
                    release['writes'] = [{'arg': 0, 'offset': PAIR_AT + 8 - PAIR_SECOND_AT,
                                          'pointer': f'block+{PAIR_THIRD_AT:#x}'}]
            stubs.append(release)
    return _case(address, cid, 'void', {'rdi': f'buf:block+{PAIR_AT:#x}'},
                 {'block': _fill(71, 0x80), 'objvt': _fill(73, 0x20)}, memory, stubs)


# (id, first, second, change)
PAIR_CASES = [
    ('no_objects', None, None, None), ('first_only', 'one', None, None), ('second_only', None, 'two', None),
    ('both', 'one', 'two', None), ('first_release_clears_second', 'one', 'two', 'clear_second'),
    ('first_release_swaps_second', 'one', 'two', 'swap_second'),
    ('first_release_sets_second', 'one', None, 'swap_second'),
    ('second_release_sets_first', 'one', 'two', 'set_first'),
]


def pair_cases(address: str, value: int | None) -> list[dict]:
    return [_pair_case(address, cid, first, second, change) for cid, first, second, change in PAIR_CASES]


# destroys_entry_storage_with_vtable_0x057045e0
STORAGE_ALLOCATOR = 0x058018b0
STORAGE_FREE = '0x00500520'   # the allocator's slot 0x28
STORAGE_FIELD, STORAGE_VALUE, STORAGE_COUNT = 0x18, 0x20, 0x24


def _storage_case(address: str, cid: str, count: int, storage: str | None) -> dict:
    """STORAGE: 'buf' a buffer, 'owner' the owner itself, 'value' a plain value, None null."""
    memory = [{'addr': hex(STORAGE_ALLOCATOR), 'pointer': 'allocvt'},
              {'addr': 'buf:allocvt+0x28', 'guest': STORAGE_FREE},
              {'addr': f'buf:owner+{STORAGE_COUNT:#x}', 'bytes': _u32(count)}]
    if storage == 'buf':
        memory.append({'addr': f'buf:owner+{STORAGE_FIELD:#x}', 'pointer': 'storage'})
    elif storage == 'owner':
        memory.append({'addr': f'buf:owner+{STORAGE_FIELD:#x}', 'pointer': 'owner'})
    elif storage == 'value':
        memory.append({'addr': f'buf:owner+{STORAGE_FIELD:#x}', 'bytes': _qword(0x1234567890abcdef)})
    else:
        memory.append({'addr': f'buf:owner+{STORAGE_FIELD:#x}', 'bytes': _qword(0)})
    stubs = []
    if not count & 0x80000000:
        free = {'address': STORAGE_FREE, 'argc': 3, 'ret': '0x0'}
        if storage == 'owner':
            # The vtable and the cleared +0x20 are stored before the free; what the free writes
            # to the storage field and the count is overwritten after it.
            free['argmem'] = [{'arg': 1, 'offset': 0, 'size': 8},
                              {'arg': 1, 'offset': STORAGE_VALUE, 'size': 4}]
            free['writes'] = [{'arg': 1, 'offset': STORAGE_FIELD, 'pointer': 'storage'},
                              {'arg': 1, 'offset': STORAGE_COUNT, 'bytes': _u32(7)}]
        stubs.append(free)
    return _case(address, cid, 'void', {'rdi': 'buf:owner'},
                 {'owner': _fill(81, 0x30), 'storage': _fill(83, 0x20), 'allocvt': _fill(85, 0x30)},
                 memory, stubs)


def storage_cases(address: str, value: int | None) -> list[dict]:
    return [_storage_case(address, 'count_0', 0, 'buf'),
            _storage_case(address, 'count_1', 1, 'buf'),
            _storage_case(address, 'count_3_value', 3, 'value'),
            _storage_case(address, 'count_0x0fffffff', 0x0fffffff, 'buf'),
            _storage_case(address, 'count_0x10000000_wraps', 0x10000000, 'buf'),
            _storage_case(address, 'count_largest_wraps', 0x7fffffff, 'buf'),
            _storage_case(address, 'count_minus_1', 0xffffffff, 'buf'),
            _storage_case(address, 'count_0x80000000', 0x80000000, 'buf'),
            _storage_case(address, 'count_2_null_storage', 2, None),
            _storage_case(address, 'storage_is_owner', 1, 'owner')]


# releases_counted_object_at_0x10
COUNTED_BEFORE = '0x027286b0'
COUNTED_DESTROY, COUNTED_FREE = '0x00500540', '0x00500560'
COUNTED_OWNER_AT, COUNTED_OTHER_AT, COUNTED_FIELD = 0x40, 0x20, 0x10
COUNTS = 0x8
STRONG_MASK, WEAK_SHIFT, ALIVE = 0x7fff, 15, 0x40000000


def _counts(strong: int, weak: int, alive: bool, high: int = 0) -> int:
    return (strong & STRONG_MASK) | ((weak & STRONG_MASK) << WEAK_SHIFT) | (ALIVE if alive else 0) | high


def _drop_stubs(counts: int, destroy_sets: int | None) -> list[dict]:
    # Each call compares the counts as they are when it is made.
    seen = [{'arg': 0, 'offset': COUNTS, 'size': 4}]
    stubs = []
    bits = (counts & 0xffff8000) | ((counts + STRONG_MASK) & STRONG_MASK)
    if not (counts + STRONG_MASK) & STRONG_MASK and counts & ALIVE:
        destroy = {'address': COUNTED_DESTROY, 'argc': 1, 'argi': [0], 'argmem': seen}
        if destroy_sets is not None:
            destroy['writes'] = [{'arg': 0, 'offset': COUNTS, 'bytes': _u32(destroy_sets)}]
            bits = destroy_sets
        stubs.append(destroy)
        bits &= ~ALIVE & 0xffffffff
    if not ((bits >> WEAK_SHIFT) + STRONG_MASK) & STRONG_MASK:
        stubs.append({'address': COUNTED_FREE, 'argc': 1, 'argi': [0], 'argmem': seen})
    return stubs


def _counted_case(address: str, cid: str, counts: int | None, destroy_sets: int | None = None,
                  change: str | None = None, other_counts: int = 0) -> dict:
    """COUNTS: object 1's counts, None for no object. CHANGE: what the call of 0x027286b0 does to
    the field: 'clear' it or 'swap' in object 2 (whose counts are OTHER_COUNTS)."""
    block = bytearray.fromhex(_fill(91, 0x60)['bytes'])
    block[COUNTS:COUNTS + 4] = struct.pack('<I', counts or 0)
    block[COUNTED_OTHER_AT + COUNTS:COUNTED_OTHER_AT + COUNTS + 4] = struct.pack('<I', other_counts)
    field = f'buf:block+{COUNTED_OWNER_AT + COUNTED_FIELD:#x}'
    memory = [{'addr': 'buf:block+0x0', 'pointer': 'cvt'}, {'addr': f'buf:block+{COUNTED_OTHER_AT:#x}', 'pointer': 'cvt'},
              {'addr': 'buf:cvt+0x18', 'guest': COUNTED_DESTROY}, {'addr': 'buf:cvt+0x20', 'guest': COUNTED_FREE}]
    stubs = []
    if counts is None:
        memory.append({'addr': field, 'bytes': _qword(0)})
    else:
        memory.append({'addr': field, 'pointer': 'block'})
        before = {'address': COUNTED_BEFORE, 'argc': 1, 'argi': [0]}
        if change == 'clear':
            before['writes'] = [{'arg': 0, 'offset': COUNTED_OWNER_AT + COUNTED_FIELD, 'bytes': _qword(0)}]
        elif change == 'swap':
            before['writes'] = [{'arg': 0, 'offset': COUNTED_OWNER_AT + COUNTED_FIELD,
                                 'pointer': f'block+{COUNTED_OTHER_AT:#x}'}]
        stubs.append(before)
        if change is None:
            stubs += _drop_stubs(counts, destroy_sets)
        elif change == 'swap':
            stubs += _drop_stubs(other_counts, None)
    return _case(address, cid, 'void', {'rdi': f'buf:block+{COUNTED_OWNER_AT:#x}'},
                 {'block': {'size': 0x60, 'bytes': block.hex()}, 'cvt': _fill(93, 0x28)}, memory, stubs)


def counted_cases(address: str, value: int | None) -> list[dict]:
    return [_counted_case(address, 'no_object', None),
            _counted_case(address, 'both_drop', _counts(3, 2, True)),
            _counted_case(address, 'strong_to_0_destroys', _counts(1, 2, True)),
            _counted_case(address, 'both_to_0_destroy_and_free', _counts(1, 1, True)),
            _counted_case(address, 'strong_to_0_not_alive', _counts(1, 2, False)),
            _counted_case(address, 'weak_to_0_frees', _counts(4, 1, True)),
            _counted_case(address, 'not_alive_both_to_0', _counts(1, 1, False)),
            _counted_case(address, 'strong_wraps_from_0', _counts(0, 3, True)),
            _counted_case(address, 'weak_wraps_from_0', _counts(2, 0, True)),
            _counted_case(address, 'destroy_then_weak_wraps', _counts(1, 0, True)),
            _counted_case(address, 'top_bit_kept', _counts(1, 1, True, 0x80000000)),
            _counted_case(address, 'all_bits', 0xffffffff),
            _counted_case(address, 'large_counts', _counts(0x7fff, 0x7fff, False, 0x80000000)),
            # The destroy rewrites the counts: the weak count dropped is the new one.
            _counted_case(address, 'destroy_raises_weak', _counts(1, 1, True), destroy_sets=_counts(0, 5, True)),
            _counted_case(address, 'destroy_leaves_weak_1', _counts(1, 4, True), destroy_sets=_counts(9, 1, False)),
            _counted_case(address, 'destroy_sets_top_bits', _counts(1, 2, True), destroy_sets=0xc0000000),
            # The call before the drop rewrites the field: the object dropped is the new one.
            _counted_case(address, 'call_clears_field', _counts(1, 1, True), change='clear'),
            _counted_case(address, 'call_swaps_field', _counts(3, 3, True), change='swap',
                          other_counts=_counts(1, 1, True)),
            _counted_case(address, 'call_swaps_field_weak_only', _counts(1, 1, True), change='swap',
                          other_counts=_counts(5, 2, False))]
