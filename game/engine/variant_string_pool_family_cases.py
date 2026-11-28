#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for four engine families (variant_string_pool_families.cpp): the variant property
setter, the wide string copy on the thread's heap, the pool appends and the scaleform string
thunk. game/engine/family_cases.py runs them for every member at its own address.

Every case is synthetic: authored buffers, and stubs for the calls the members make (their
results and what they write). Cases whose stubs change a value the member reads before or after
that call show which read the original makes; the control mutants in game/engine/tests/
family_mutants.c read it at the other place and must fail.
"""
from __future__ import annotations

import struct

STAND_IN = '0x00400240'          # a stand-in for a virtual slot a case calls, stubbed
GUARD_GOT = '0x057e3be0'         # where the stack guard's address is read from
FATAL_ERROR = '0x024b55b0'
THREAD_HEAP_ALLOCATOR = '0x0247d7a0'
WIDE_STRING_ASSIGN = '0x02a2a310'
ENGINE_MEMCPY = '0x02a1a1b0'
POOL_MAP_ENTRY = '0x02edc8e0'
MAKE_STRING = '0x00671390'
RELEASE_NODE = '0x004f0410'
ASSIGN_STRING = '0x007183d0'


def filled(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7 + 1) & 0xff for i in range(size)).hex()}


def le32(value: int) -> str:
    return struct.pack('<I', value & 0xffffffff).hex()


def le64(value: int) -> str:
    return struct.pack('<Q', value & 0xffffffffffffffff).hex()


def f32_bits(value: float) -> int:
    return struct.unpack('<I', struct.pack('<f', value))[0]


def make_case(address: str, case_id: str, returns: str, args: dict, buffers: dict, memory: list,
              stubs: list) -> dict:
    regs = {'rdi': '0x7777777777777777', 'rsi': '0x1111111111111111', 'rdx': '0x2222222222222222',
            'rcx': '0x3333333333333333', 'r8': '0x4444444444444444', 'r9': '0x5555555555555555',
            'rax': '0x5aa5c33c0ff0e11e'}
    regs.update(args)
    buffers = dict(buffers, guard=filled(0x47, 8))
    memory = [{'addr': GUARD_GOT, 'pointer': 'guard'}] + memory
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': returns, 'buffers': buffers,
            'memory': memory, 'imports': [], 'stubs': stubs, 'args': regs}


def report_stub() -> dict:
    """The fatal error report: no file, the line and the message (rcx is not set, not compared)."""
    return {'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2], 'ret': '0x0'}


# ---- sets_property_0x3ea_or_0x3eb_from_variant ------------------------------------------------

# (case id, property (esi), variant type, value bits): numbers of each type for both properties
# and others; a float's low four bytes are its bits (high ones ignored), truncated toward zero.
NUMBERS = (
    ('float_0x3ea', 0x3ea, 1, f32_bits(2.75)),
    ('float_0x3eb', 0x3eb, 1, f32_bits(2.75)),
    ('float_negative_0x3eb', 0x3eb, 1, f32_bits(-7.5)),
    ('float_round_half_0x3ea', 0x3ea, 1, f32_bits(1.5)),
    ('float_below_one_0x3eb', 0x3eb, 1, f32_bits(-0.75)),
    ('float_nan_0x3ea', 0x3ea, 1, 0x7fc00001),
    ('float_too_large_0x3eb', 0x3eb, 1, f32_bits(3.0e9)),
    ('float_too_small_0x3ea', 0x3ea, 1, f32_bits(-3.0e9)),
    ('integer_0x3ea', 0x3ea, 2, 0xabcdef0112345678),
    ('integer_0x3eb', 0x3eb, 2, 0x00000001fffffff6),
    ('none_0x3eb', 0x3eb, 0, 0x4048f5c3),
    ('other_type_0x3ea', 0x3ea, 4, 0x1234),
    ('negative_type_0x3eb', 0x3eb, -1, 0x1234),
    ('float_other_property', 0x3ec, 1, f32_bits(9.25)),
    ('integer_other_property', 0x3e9, 2, 0x55),
    ('integer_property_zero', 0, 2, 0x66),
    ('integer_property_high_bits', 0xffffffff000003eb, 2, 0x77),
    ('integer_property_negative', 0xfffffc15, 2, 0x88),
)

# (case id, property, references before): an object variant takes a reference and drops it; the
# drop that finds 1 destroys the object (its first vtable slot), one that finds 0 or below
# reports an invalid unref; the stored number is 0.
OBJECTS = (
    ('object_last_reference_0x3ea', 0x3ea, 0),
    ('object_shared_0x3eb', 0x3eb, 4),
    ('object_negative_count_0x3eb', 0x3eb, -1),
    ('object_count_wraps', 0x3ea, 0x7fffffff),
    ('object_other_property_destroyed', 0x3ec, 0),
    ('object_other_property_kept', 0x3e9, 1),
    ('object_far_below_zero', 0x3eb, -0x10000),
)


def variant_property_cases(address: str, value: int | None) -> list[dict]:
    out = []
    for seed, (case_id, prop, kind, bits) in enumerate(NUMBERS):
        buffers = {'owner': filled(seed, 0x178),
                   'variant': {'size': 0x10, 'bytes': le64(bits) + le32(kind) + 'c5c5c5c5'}}
        args = {'rdi': 'buf:owner', 'rsi': f'0x{prop:x}', 'rdx': 'buf:variant'}
        out.append(make_case(address, case_id, 'void', args, buffers, [], []))
    for seed, (case_id, prop, count) in enumerate(OBJECTS, start=40):
        buffers = {'owner': filled(seed, 0x178), 'variant': {'size': 0x10, 'bytes': '00' * 8 + le32(3) + 'c5c5c5c5'},
                   'object': {'size': 0x18, 'bytes': '00' * 8 + le32(count) + 'a1' * 12}, 'table': {'size': 0x10}}
        memory = [{'addr': 'buf:variant+0x0', 'pointer': 'object'}, {'addr': 'buf:object+0x0', 'pointer': 'table'},
                  {'addr': 'buf:table+0x0', 'guest': STAND_IN}]
        before = (count + 1) & 0xffffffff
        before = before - (1 << 32) if before & 0x80000000 else before
        stubs = []
        if before == 1:
            stubs.append({'address': STAND_IN, 'argc': 1, 'ret': '0x0'})
        elif before <= 0:
            stubs.append(report_stub())
        args = {'rdi': 'buf:owner', 'rsi': f'0x{prop:x}', 'rdx': 'buf:variant'}
        out.append(make_case(address, case_id, 'void', args, buffers, memory, stubs))
    return out


# ---- copies_wide_string_at_0x28_on_thread_heap ------------------------------------------------

SOURCE_TEXT = 0x28   # the source's wide string
# One source buffer also holds the allocator, the heap text and a second text, so that the stubs
# (which write through their arguments) can reach the source's fields from the allocator or the
# characters they are given.
SOURCE_ALLOCATOR = 0x60
SOURCE_HEAP_TEXT = 0x80
SOURCE_OTHER_TEXT = 0xc0


def wide_string_copy_case(address: str, case_id: str, seed: int, capacity: int, size: int, owns: int,
                          flags: int = 0x20, info_changes_source: bool = False,
                          assign_changes_owns: bool = False) -> dict:
    text = SOURCE_TEXT
    buffers = {'copy': filled(seed, 0x38), 'source': filled(seed + 1, 0x100), 'table': {'size': 0x80}}
    memory = [{'addr': f'buf:source+{text + 0x18:#x}', 'bytes': le64(size)},
              {'addr': f'buf:source+{text + 0x20:#x}', 'bytes': le64(capacity)},
              {'addr': f'buf:source+{text + 0x30:#x}', 'bytes': f'{owns:02x}'},
              {'addr': f'buf:source+{SOURCE_ALLOCATOR:#x}', 'pointer': 'table'},
              {'addr': 'buf:table+0x20', 'guest': STAND_IN}]
    if capacity >= 8:
        memory.append({'addr': f'buf:source+{text + 8:#x}', 'pointer': f'source+{SOURCE_HEAP_TEXT:#x}'})
    heap = {'address': THREAD_HEAP_ALLOCATOR, 'argc': 0, 'ret': f'buf:source+{SOURCE_ALLOCATOR:#x}'}
    info = {'address': STAND_IN, 'argc': 3, 'argi': [1, 2], 'ret': '0x0',
            'writes': [{'arg': 0, 'bytes': f'{flags:02x}' + '7e' * 7}]}
    if info_changes_source:
        # The heap check changes the source's text pointer, size and capacity (through the
        # allocator it is given).
        base = -SOURCE_ALLOCATOR
        info['writes'] += [
            {'arg': 1, 'offset': base + text + 8, 'pointer': f'source+{SOURCE_OTHER_TEXT:#x}'},
            {'arg': 1, 'offset': base + text + 0x18, 'bytes': le64(size + 5)},
            {'arg': 1, 'offset': base + text + 0x20, 'bytes': le64(0x40 if capacity < 8 else 3)}]
    stubs = [heap, info]
    if not flags & 0x20:
        stubs.append(report_stub())
    # The assignment sees the copy's fields set (and its first word and owns byte untouched).
    assign = {'address': WIDE_STRING_ASSIGN, 'argc': 3, 'argmem': [{'arg': 0, 'size': 0x38}], 'ret': 'buf:copy',
              'writes': [{'arg': 0, 'offset': 0x18, 'bytes': le64(size)}, {'arg': 0, 'offset': 8, 'bytes': 'e1e2e3e4'}]}
    if assign_changes_owns:
        # Through the characters it is given (inline in the source, or its heap text).
        characters = SOURCE_HEAP_TEXT if capacity >= 8 else text + 8
        assign['writes'].append({'arg': 1, 'offset': text + 0x30 - characters, 'bytes': f'{owns ^ 0x5a:02x}'})
    stubs.append(assign)
    args = {'rdi': 'buf:copy', 'rsi': 'buf:source'}
    return make_case(address, case_id, 'i64', args, buffers, memory, stubs)


def wide_string_copy_cases(address: str, value: int | None) -> list[dict]:
    c = wide_string_copy_case
    return [
        c(address, 'inline_short', 3, 7, 3, 1),
        c(address, 'inline_empty', 5, 7, 0, 0),
        c(address, 'inline_capacity_zero', 7, 0, 2, 0xff),
        c(address, 'heap_capacity_eight', 9, 8, 8, 1),
        c(address, 'heap_long', 11, 0x100, 0x30, 0),
        c(address, 'heap_huge_capacity', 13, 0xffffffffffffffff, 4, 0x80),
        c(address, 'invalid_heap_reported', 15, 0x20, 6, 1, flags=0),
        c(address, 'invalid_heap_other_bits', 17, 7, 1, 1, flags=0xdf),
        c(address, 'valid_heap_all_bits', 19, 7, 1, 0, flags=0xff),
        c(address, 'heap_check_changes_inline_source', 21, 7, 2, 1, info_changes_source=True),
        c(address, 'heap_check_changes_heap_source', 23, 0x40, 9, 1, info_changes_source=True),
        c(address, 'heap_check_changes_capacity_eight', 25, 0x10, 3, 0, info_changes_source=True),
        c(address, 'report_after_heap_check_changes_source', 27, 7, 4, 1, flags=0, info_changes_source=True),
        c(address, 'assign_changes_owns', 29, 7, 3, 1, assign_changes_owns=True),
        c(address, 'assign_changes_owns_heap', 31, 0x20, 0x11, 0, assign_changes_owns=True),
    ]


# ---- appends_blocks_to_pool_slot_5_or_16 ------------------------------------------------------

POOL_BASES = 0x8
POOL_USED = 0xb8
# The pool buffer also holds both slots' data and the pair buffer the map, so that the copy (given
# the destination) and the map lookup (given the map) can reach the used sizes and the pair.
POOL_REGION = {5: 0x100, 16: 0x180}
PAIR_MAP = 0x20


def pool_case(address: str, case_id: str, seed: int, select: int, first: bool, second: bool, count: int,
              used: int = 0x10, entry_changes_count: int | None = None, copy_changes_used: int | None = None,
              entry_changes_second: bool = False) -> dict:
    buffers = {'pool': filled(seed, 0x200), 'pair': filled(seed + 1, 0x40), 'block_a': filled(seed + 4, 0x10),
               'block_b': filled(seed + 5, 0x10), 'block_c': filled(seed + 6, 0x10), 'entry_a': filled(seed + 8, 8),
               'entry_b': filled(seed + 9, 8)}
    slot = 16 if select & 0xff else 5
    used_of = {5: used, 16: (used + 8) & 0xffffffff}
    memory = [{'addr': 'buf:pool+0x0', 'pointer': f'pair+{PAIR_MAP:#x}'},
              {'addr': 'buf:pair+0x18', 'bytes': le32(count)}]
    for s in (5, 16):
        memory += [{'addr': f'buf:pool+{POOL_BASES + s * 8:#x}', 'pointer': f'pool+{POOL_REGION[s]:#x}'},
                   {'addr': f'buf:pool+{POOL_USED + s * 4:#x}', 'bytes': le32(used_of[s])}]
    memory.append({'addr': 'buf:pair+0x8', 'pointer': 'block_a'} if first else {'addr': 'buf:pair+0x8', 'bytes': '00' * 8})
    memory.append({'addr': 'buf:pair+0x10', 'pointer': 'block_b'} if second else {'addr': 'buf:pair+0x10', 'bytes': '00' * 8})
    stubs = []
    blocks = (['entry_a'] if first else []) + (['entry_b'] if second or entry_changes_second else [])
    for i, entry in enumerate(blocks):
        copy = {'address': ENGINE_MEMCPY, 'argc': 3, 'ret': f'0x{seed:x}'}
        if i == 0 and copy_changes_used is not None:
            # Through the destination (the slot's data at its used size).
            destination = POOL_REGION[slot] + used_of[slot]
            copy['writes'] = [{'arg': 0, 'offset': POOL_USED + slot * 4 - destination, 'bytes': le32(copy_changes_used)}]
        lookup = {'address': POOL_MAP_ENTRY, 'argc': 2, 'argmem': [{'arg': 1, 'size': 8}], 'ret': f'buf:{entry}'}
        if i == 0:
            # Through the map, which lies in the pair buffer.
            changes = []
            if entry_changes_count is not None:
                changes.append({'arg': 0, 'offset': 0x18 - PAIR_MAP, 'bytes': le32(entry_changes_count)})
            if entry_changes_second:
                changes.append({'arg': 0, 'offset': 0x10 - PAIR_MAP, 'pointer': 'block_c'})
            if changes:
                lookup['writes'] = changes
        stubs += [copy, lookup]
    args = {'rsi': 'buf:pool', 'rdx': 'buf:pair', 'rcx': f'0x{select:x}'}
    return make_case(address, case_id, 'i8', args, buffers, memory, stubs)


def pool_append_cases(address: str, value: int | None) -> list[dict]:
    c = pool_case
    return [
        c(address, 'both_slot_5', 3, 0, True, True, 3),
        c(address, 'both_slot_16', 5, 1, True, True, 3),
        c(address, 'selector_low_byte_only', 7, 0x100, True, True, 2),
        c(address, 'selector_high_low_byte', 9, 0xffffffffffffff80, True, True, 2),
        c(address, 'first_only', 11, 0, True, False, 5),
        c(address, 'second_only', 13, 1, False, True, 5),
        c(address, 'neither', 15, 0, False, False, 5),
        c(address, 'zero_count', 17, 1, True, True, 0),
        c(address, 'bytes_wrap_32_bits', 19, 0, True, True, 0x40000001),
        c(address, 'used_wraps', 21, 1, True, True, 4, used=0xfffffff8),
        c(address, 'entry_changes_count', 23, 0, True, True, 3, entry_changes_count=7),
        c(address, 'entry_changes_count_slot_16', 25, 1, True, True, 6, entry_changes_count=1),
        c(address, 'copy_changes_used', 27, 0, True, True, 2, copy_changes_used=0x30),
        c(address, 'copy_changes_used_slot_16', 29, 1, True, False, 2, copy_changes_used=4),
        c(address, 'entry_sets_second', 31, 0, True, False, 3, entry_changes_second=True),
        c(address, 'entry_replaces_second', 33, 1, True, True, 3, entry_changes_second=True),
    ]


# ---- assigns_created_string_unless_exception --------------------------------------------------

# The VM buffer also holds the maker's context and the string manager, so that the maker (given an
# input inside the context) and the first release (given the empty node) can reach the VM's
# exception byte and the empty node's count.
VM_EXCEPTION = 0xa0
VM_CONTEXT = 0xb0
VM_MANAGER = 0x140
CONTEXT_INPUT = VM_CONTEXT + 0x70
EMPTY_NODE = VM_MANAGER + 0x48
EMPTY_COUNT = EMPTY_NODE + 0x18


def string_thunk_case(address: str, case_id: str, seed: int, empty_count: int, created_count: int, exception: int,
                      release_raises: bool = False, make_raises: bool = False, assign_swaps: bool = False,
                      make_changes_empty: int | None = None) -> dict:
    buffers = {'vm': filled(seed, 0x1c0), 'owner': filled(seed + 1, 0x220), 'arguments': filled(seed + 3, 0x20),
               'object': filled(seed + 4, 0x50), 'link': filled(seed + 5, 0x88), 'info': filled(seed + 6, 0x18),
               'created': filled(seed + 8, 0x20), 'other': filled(seed + 9, 0x20), 'result': filled(seed + 10, 0x20)}
    memory = [{'addr': 'buf:vm+0x10', 'pointer': 'owner'}, {'addr': 'buf:owner+0x218', 'pointer': f'vm+{VM_MANAGER:#x}'},
              {'addr': f'buf:vm+{EMPTY_COUNT:#x}', 'bytes': le32(empty_count)},
              {'addr': f'buf:vm+{VM_EXCEPTION:#x}', 'bytes': f'{exception:02x}'},
              {'addr': 'buf:arguments+0x10', 'pointer': 'object'}, {'addr': 'buf:object+0x28', 'pointer': 'link'},
              {'addr': 'buf:link+0x80', 'pointer': 'info'}, {'addr': 'buf:info+0x10', 'pointer': f'vm+{VM_CONTEXT:#x}'},
              {'addr': 'buf:created+0x18', 'bytes': le32(created_count)},
              {'addr': 'buf:other+0x18', 'bytes': le32(1)}]
    flipped = f'{exception ^ 1:02x}'
    make = {'address': MAKE_STRING, 'argc': 3, 'ret': '0x0', 'writes': [{'arg': 0, 'pointer': 'created'}]}
    if make_raises:
        make['writes'].append({'arg': 2, 'offset': VM_EXCEPTION - CONTEXT_INPUT, 'bytes': flipped})
    if make_changes_empty is not None:
        make['writes'].append({'arg': 2, 'offset': EMPTY_COUNT - CONTEXT_INPUT, 'bytes': le32(make_changes_empty)})
    release = {'address': RELEASE_NODE, 'argc': 1, 'ret': '0x0'}
    first_release = dict(release)
    if release_raises:
        # The cases that use it release the empty node first.
        assert empty_count == 0
        first_release['writes'] = [{'arg': 0, 'offset': VM_EXCEPTION - EMPTY_NODE, 'bytes': flipped}]
    assign = {'address': ASSIGN_STRING, 'argc': 2, 'argmem': [{'arg': 1, 'size': 8}], 'ret': '0x0'}
    if assign_swaps:
        assign['writes'] = [{'arg': 1, 'pointer': 'other'}]
    # Room for every release a path can make (unused stubs do not count).
    stubs = [make, first_release, release, assign, release]
    args = {'rsi': 'buf:vm', 'rdx': 'buf:arguments', 'rcx': 'buf:result'}
    return make_case(address, case_id, 'void', args, buffers, memory, stubs)


def string_thunk_cases(address: str, value: int | None) -> list[dict]:
    c = string_thunk_case
    return [
        c(address, 'assigned_counts_kept', 3, 4, 1, 0),
        c(address, 'empty_released', 5, 0, 1, 0),
        c(address, 'created_released_early', 7, 3, 0, 0),
        c(address, 'both_released_early', 9, 0, 0, 0),
        c(address, 'exception_skips_assign', 11, 2, 1, 1),
        c(address, 'exception_any_bits', 13, 2, 2, 0x80),
        c(address, 'exception_with_releases', 15, 0, 0, 1),
        c(address, 'empty_count_negative', 17, -1, 5, 0),
        c(address, 'release_raises_exception', 19, 0, 1, 0, release_raises=True),
        c(address, 'release_clears_exception', 21, 0, 0, 1, release_raises=True),
        c(address, 'maker_raises_exception', 23, 4, 1, 0, make_raises=True),
        c(address, 'maker_changes_empty_count', 25, 4, 1, 0, make_changes_empty=0),
        c(address, 'assign_swaps_held_string', 27, 4, 1, 0, assign_swaps=True),
        c(address, 'assign_swaps_after_release', 29, 0, 3, 0, assign_swaps=True),
    ]
