#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the scaleform dispatch and visit families (game/engine/scaleform_visit_dispatch.cpp).

game/engine/family_cases.py hands every member these cases at its own address.

dispatches_on_slot_match_with_member_*: one buffer holds the handles, the value, its owner chain,
two slot tables and the handle pointer, so the resolver stub (0x004f0330, called with the
argument's handle) can change any of them through its argument: the argument's target, the handle
the third argument points at, the first or second slot, the owner's context and the value's kind.
Both dispatch targets (0x005c9830, 0x0056d2c0) are stubs comparing every argument.

visits_base_pairs_and_entries_at_*: the base visit (0x027fd2c0) is a stub comparing the object and
the visitor; the pair visit (0x027fe500) runs as original code except where a case stubs it. The
entries' states cover the visit count's carry into and out of the pending bits, listed and
unlisted entries, null entries, an entry listed twice and the visitor's current entry itself;
other cases change the array pointer from the base visit, from a pair visit, and from an entry's
own unlinking in the middle of the array.
"""
from __future__ import annotations

import struct

RESOLVE_HANDLE_TARGET = '0x004f0330'
DISPATCH_WITHOUT_SLOT = '0x0056d2c0'
DISPATCH_WITH_SLOT = '0x005c9830'
VISIT_BASE = '0x027fd2c0'
VISIT_PAIR = '0x027fe500'

ARGUMENT = '0x5eed00001234abcd'
MATCHED_RESULT = 1
UNMATCHED_RESULT = 0

# --- dispatches_on_slot_match_with_member_* ---------------------------------------------------
# Offsets in the buffer 'space'. Handles are 0x20 bytes, their target at +0x10.
HANDLE_SIZE = 0x20
HANDLE_TARGET = 0x10
ARGUMENT_HANDLE = 0x000      # the handle the third argument points at
OTHER_HANDLE = 0x020         # a handle the resolver may put in its place
FIRST_HANDLE = 0x040         # the first slot's handle
SECOND_HANDLE = 0x060        # the second slot's handle
THIRD_HANDLE = 0x080         # a handle the resolver may put in a slot
FOURTH_HANDLE = 0x0a0
VALUE = 0x0c0                # the second argument: owner pointer, kind byte at +8
VALUE_KIND = 8
OWNER = 0x0e0                # its context at +0x28
OWNER_CONTEXT = 0x28
CONTEXT = 0x110              # its slots at +0x18
CONTEXT_SLOTS = 0x18
SLOTS = 0x130                # second slot at +0x3b0, first at +0x3b8
SLOT_SECOND = 0x3b0
SLOT_FIRST = 0x3b8
HANDLE_POINTER = 0x4f0       # the third argument
OTHER_CONTEXT = 0x500
OTHER_SLOTS = 0x520
SPACE_SIZE = 0x8e0

TARGET_A = 0x7a7a00001111aaaa
TARGET_B = 0x7b7b00002222bbbb
TARGET_C = 0x7c7c00003333cccc


def word(value: int) -> str:
    return struct.pack('<Q', value & 0xffffffffffffffff).hex()


def pattern(seed: int, size: int) -> str:
    return bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()


def space_pointer(offset: int) -> dict:
    return {'pointer': f'space+0x{offset:x}'}


def resolver(*writes: dict) -> dict:
    """One call of 0x004f0330; WRITES go through its argument (the handle at ARGUMENT_HANDLE)."""
    stub = {'address': RESOLVE_HANDLE_TARGET, 'argc': 1, 'argi': [0], 'ret': '0x6b6b6b6b6b6b6b6b'}
    if writes:
        stub['writes'] = list(writes)
    return stub


def write_at(offset: int, **value) -> dict:
    """A resolver write at OFFSET in 'space' (the resolver's argument is the argument handle)."""
    return {'arg': 0, 'offset': offset - ARGUMENT_HANDLE, **value}


def dispatch_case(address: str, case_id: str, member_offset: int, kind: int, targets: dict, first: int,
                  second: int, resolves: tuple = (), seed: int = 1) -> dict:
    """TARGETS maps handle offsets to their targets; FIRST and SECOND are the slots' handles."""
    memory = [{'addr': 'buf:space+0x0', 'bytes': pattern(seed, SPACE_SIZE)}]
    for handle in (ARGUMENT_HANDLE, OTHER_HANDLE, FIRST_HANDLE, SECOND_HANDLE, THIRD_HANDLE, FOURTH_HANDLE):
        memory.append({'addr': f'buf:space+0x{handle + HANDLE_TARGET:x}', 'bytes': word(targets.get(handle, 0))})
    memory += [
        {'addr': f'buf:space+0x{VALUE:x}', **space_pointer(OWNER)},
        {'addr': f'buf:space+0x{VALUE + VALUE_KIND:x}', 'bytes': f'{kind:02x}'},
        {'addr': f'buf:space+0x{OWNER + OWNER_CONTEXT:x}', **space_pointer(CONTEXT)},
        {'addr': f'buf:space+0x{CONTEXT + CONTEXT_SLOTS:x}', **space_pointer(SLOTS)},
        {'addr': f'buf:space+0x{SLOTS + SLOT_FIRST:x}', **space_pointer(first)},
        {'addr': f'buf:space+0x{SLOTS + SLOT_SECOND:x}', **space_pointer(second)},
        {'addr': f'buf:space+0x{OTHER_CONTEXT + CONTEXT_SLOTS:x}', **space_pointer(OTHER_SLOTS)},
        {'addr': f'buf:space+0x{OTHER_SLOTS + SLOT_FIRST:x}', **space_pointer(THIRD_HANDLE)},
        {'addr': f'buf:space+0x{OTHER_SLOTS + SLOT_SECOND:x}', **space_pointer(FOURTH_HANDLE)},
        {'addr': f'buf:space+0x{HANDLE_POINTER:x}', **space_pointer(ARGUMENT_HANDLE)},
    ]
    # Every call either target could get is scripted; the comparison shows which one ran.
    stubs = list(resolves)
    stubs += [{'address': DISPATCH_WITH_SLOT, 'argc': 6, 'argi': [0, 1, 2, 3, 4, 5], 'ret': MATCHED_RESULT},
              {'address': DISPATCH_WITHOUT_SLOT, 'argc': 4, 'argi': [0, 1, 2, 3], 'ret': UNMATCHED_RESULT}]
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i8',
            'args': {'rdi': 'buf:self', 'rsi': f'buf:space+0x{VALUE:x}', 'rdx': f'buf:space+0x{HANDLE_POINTER:x}',
                     'rcx': ARGUMENT, 'rax': '0x5aa5c33c0ff0e11e'},
            'buffers': {'self': {'size': member_offset + 0x10, 'bytes': pattern(seed + 3, member_offset + 0x10)},
                        'space': {'size': SPACE_SIZE}},
            'memory': memory, 'imports': [], 'stubs': stubs}


def dispatch_cases(address: str, member_offset: int) -> list[dict]:
    a, h, o = ARGUMENT_HANDLE, FIRST_HANDLE, SECOND_HANDLE
    same = {a: TARGET_A, h: TARGET_A, o: TARGET_A}
    out = [
        # Kinds 7 and up compare the handle pointers, whatever the targets.
        dispatch_case(address, 'pointer_kind_first_slot', member_offset, 7, same, a, o),
        dispatch_case(address, 'pointer_kind_second_slot', member_offset, 8, same, h, a),
        dispatch_case(address, 'pointer_kind_neither_slot', member_offset, 0xff, same, h, o),
        dispatch_case(address, 'pointer_kind_both_slots', member_offset, 0x80, same, a, a),
        # Lower kinds compare targets, resolving a null argument target.
        dispatch_case(address, 'target_kind_first_slot', member_offset, 0, {a: TARGET_A, h: TARGET_A, o: TARGET_B},
                      h, o),
        dispatch_case(address, 'target_kind_second_slot', member_offset, 6, {a: TARGET_A, h: TARGET_B, o: TARGET_A},
                      h, o),
        dispatch_case(address, 'target_kind_neither_slot', member_offset, 3, {a: TARGET_A, h: TARGET_B, o: TARGET_C},
                      h, o),
        dispatch_case(address, 'target_kind_null_slot_target', member_offset, 2, {a: TARGET_A, o: TARGET_B}, h, o),
        dispatch_case(address, 'resolve_fills_first_target', member_offset, 0, {h: TARGET_A, o: TARGET_B}, h, o,
                      (resolver(write_at(a + HANDLE_TARGET, bytes=word(TARGET_A))),)),
        dispatch_case(address, 'resolve_fills_second_target', member_offset, 1, {h: TARGET_B, o: TARGET_C}, h, o,
                      (resolver(write_at(a + HANDLE_TARGET, bytes=word(TARGET_C))),)),
        dispatch_case(address, 'unresolved_null_target_matches_null_slot', member_offset, 0, {o: TARGET_C}, h, o,
                      (resolver(), resolver())),
        dispatch_case(address, 'unresolved_twice_then_neither', member_offset, 5, {h: TARGET_B, o: TARGET_C}, h, o,
                      (resolver(), resolver())),
        dispatch_case(address, 'second_resolve_fills_second_target', member_offset, 0, {h: TARGET_B, o: TARGET_C},
                      h, o, (resolver(), resolver(write_at(a + HANDLE_TARGET, bytes=word(TARGET_C))))),
        # The resolver changes what is read after it.
        dispatch_case(address, 'resolve_moves_first_slot', member_offset, 0,
                      {h: TARGET_B, o: TARGET_C, THIRD_HANDLE: TARGET_A}, h, o,
                      (resolver(write_at(a + HANDLE_TARGET, bytes=word(TARGET_A)),
                                write_at(SLOTS + SLOT_FIRST, **space_pointer(THIRD_HANDLE))), resolver())),
        dispatch_case(address, 'resolve_replaces_argument_handle', member_offset, 0,
                      {OTHER_HANDLE: TARGET_A, h: TARGET_A, o: TARGET_B}, h, o,
                      (resolver(write_at(HANDLE_POINTER, **space_pointer(OTHER_HANDLE))), resolver())),
        dispatch_case(address, 'resolve_switches_owner_context', member_offset, 0,
                      {h: TARGET_B, o: TARGET_C, THIRD_HANDLE: TARGET_A, FOURTH_HANDLE: TARGET_A}, h, o,
                      (resolver(write_at(a + HANDLE_TARGET, bytes=word(TARGET_A)),
                                write_at(OWNER + OWNER_CONTEXT, **space_pointer(OTHER_CONTEXT))), resolver())),
        dispatch_case(address, 'resolve_raises_kind_for_second_slot', member_offset, 0,
                      {h: TARGET_B, THIRD_HANDLE: TARGET_A}, h, THIRD_HANDLE,
                      (resolver(write_at(a + HANDLE_TARGET, bytes=word(TARGET_A)),
                                write_at(VALUE + VALUE_KIND, bytes='07')), resolver())),
        dispatch_case(address, 'second_resolve_moves_second_slot', member_offset, 0, {h: TARGET_B, o: TARGET_C},
                      h, o, (resolver(), resolver(write_at(SLOTS + SLOT_SECOND, **space_pointer(THIRD_HANDLE))))),
    ]
    for seed, value in ((5, '0x0'), (9, '0xffffffffffffffff')):
        case = dispatch_case(address, f'argument_{value}', member_offset, 0, {a: TARGET_A, h: TARGET_A}, h, o, seed=seed)
        case['args']['rcx'] = value
        out.append(case)
    return out


# --- visits_base_pairs_and_entries_at_* -------------------------------------------------------
# Offsets in the buffer 'nodes'. Entries are 0x20 bytes: previous at +8, next at +0x10, state at
# +0x18. The visitor's list (current entry at +8) is at LIST.
ENTRY_SIZE = 0x20
ENTRY_PREVIOUS = 0x8
ENTRY_NEXT = 0x10
ENTRY_STATE = 0x18
LIST = 0x000
LIST_CURRENT = 0x8
CURRENT = 0x020              # the visitor's current entry
CURRENT_NEXT = 0x040         # the entry after it
ENTRIES = 0x080              # visited entries, ENTRY_SIZE apart
NEIGHBOURS = 0x300           # each visited entry's own previous and next, 2 * ENTRY_SIZE apart
NODES_SIZE = 0x600
MAX_ENTRIES = 12
VISITOR_LIST = 0x58
SECOND_PAIR_AFTER_FIRST = 0x18
ENTRIES_AFTER_FIRST = 0x30
STAGED_ARRAY = 0x5c0         # a second array (data pointer, count) in 'nodes'


def entry(index: int) -> int:
    return ENTRIES + index * ENTRY_SIZE


def node_pointer(offset: int) -> dict:
    return {'pointer': f'nodes+0x{offset:x}'}


def visit_case(address: str, case_id: str, first_pair: int, states: dict, slots: list | None,
               pairs: tuple = (None, None, None, None), seed: int = 2) -> dict:
    """STATES maps entry indexes to states; SLOTS lists the array's entries (an index, or None for
    a null entry), None for no array; PAIRS gives the four pair pointers (entry indexes or None)."""
    entries_field = first_pair + ENTRIES_AFTER_FIRST
    memory = [{'addr': 'buf:nodes+0x0', 'bytes': pattern(seed, NODES_SIZE)},
              {'addr': f'buf:nodes+0x{LIST + LIST_CURRENT:x}', **node_pointer(CURRENT)},
              {'addr': f'buf:nodes+0x{CURRENT + ENTRY_NEXT:x}', **node_pointer(CURRENT_NEXT)},
              {'addr': f'buf:nodes+0x{CURRENT_NEXT + ENTRY_PREVIOUS:x}', **node_pointer(CURRENT)},
              {'addr': f'buf:visitor+0x{VISITOR_LIST:x}', **node_pointer(LIST)}]
    for index, state in states.items():
        before = NEIGHBOURS + index * 2 * ENTRY_SIZE
        after = before + ENTRY_SIZE
        memory += [{'addr': f'buf:nodes+0x{entry(index) + ENTRY_PREVIOUS:x}', **node_pointer(before)},
                   {'addr': f'buf:nodes+0x{entry(index) + ENTRY_NEXT:x}', **node_pointer(after)},
                   {'addr': f'buf:nodes+0x{entry(index) + ENTRY_STATE:x}', 'bytes': struct.pack('<I', state).hex()},
                   {'addr': f'buf:nodes+0x{before + ENTRY_NEXT:x}', **node_pointer(entry(index))},
                   {'addr': f'buf:nodes+0x{after + ENTRY_PREVIOUS:x}', **node_pointer(entry(index))}]
    for k, index in enumerate(pairs):
        offset = first_pair + (k // 2) * SECOND_PAIR_AFTER_FIRST + (k % 2) * 8
        memory.append({'addr': f'buf:object+0x{offset:x}',
                       **(node_pointer(entry(index)) if index is not None else {'bytes': word(0)})})
    buffers = {'object': {'size': entries_field + 0x10, 'bytes': pattern(seed + 5, entries_field + 0x10)},
               'visitor': {'size': VISITOR_LIST + 8, 'bytes': pattern(seed + 7, VISITOR_LIST + 8)},
               'nodes': {'size': NODES_SIZE}}
    if slots is None:
        memory.append({'addr': f'buf:object+0x{entries_field:x}', 'bytes': word(0)})
    else:
        buffers['array'] = {'size': 0x10}
        buffers['slots'] = {'size': max(len(slots), 1) * 8, 'bytes': pattern(seed + 9, max(len(slots), 1) * 8)}
        memory += [{'addr': f'buf:object+0x{entries_field:x}', 'pointer': 'array'},
                   {'addr': 'buf:array+0x0', 'pointer': 'slots'},
                   {'addr': 'buf:array+0x8', 'bytes': word(len(slots))}]
        for k, index in enumerate(slots):
            memory.append({'addr': f'buf:slots+0x{k * 8:x}',
                           **(node_pointer(entry(index)) if index is not None else {'bytes': word(0)})})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'args': {'rdi': 'buf:object', 'rsi': 'buf:visitor', 'rdx': '0x7e7e7e7e7e7e7e7e',
                     'rax': '0x5aa5c33c0ff0e11e'},
            'buffers': buffers, 'memory': memory, 'imports': [],
            'stubs': [{'address': VISIT_BASE, 'argc': 2, 'argi': [0, 1], 'ret': '0x3c3c3c3c3c3c3c3c'}]}


PENDING = 0x10000000
LISTED = 0x08000000


def visit_cases(address: str, first_pair: int) -> list[dict]:
    entries_field = first_pair + ENTRIES_AFTER_FIRST
    unlisted = {0: 5, 1: PENDING | 5, 2: 0x0fffffff, 3: 0x07ffffff, 4: 0x7fffffff, 5: 0xffffffff,
                6: 0x8fffffff, 7: 0x6ffffffe}
    listed = {0: PENDING | LISTED, 1: 0x78000000, 2: 0x47ffffff, 3: LISTED | 0x1234, 4: 0xf8000010}
    out = [
        visit_case(address, 'no_array', first_pair, {}, None),
        visit_case(address, 'empty_array', first_pair, {}, []),
        visit_case(address, 'unlisted_states', first_pair, unlisted, [0, None, 1, 2, None, 3, 4, 5, 6, 7]),
        visit_case(address, 'listed_entries_move_after_current', first_pair, listed, [0, 1, None, 2, 3, 4]),
        visit_case(address, 'only_null_entries', first_pair, {}, [None, None, None]),
        visit_case(address, 'entry_listed_twice', first_pair, {0: PENDING | LISTED, 1: 7}, [0, 1, 0, 1]),
        visit_case(address, 'pairs_then_entries', first_pair, {0: PENDING | LISTED, 1: 0x28000000, 2: 1,
                                                                3: 0x18000001, 4: PENDING | LISTED},
                   [4, 2], pairs=(0, 1, None, 3)),
        visit_case(address, 'pairs_without_array', first_pair, {0: PENDING | LISTED, 1: 2, 2: 0x38000000},
                   None, pairs=(None, 0, 1, 2)),
    ]

    # The visitor's current entry is visited itself: it moves after itself and points at itself.
    case = visit_case(address, 'current_entry_visited', first_pair, {0: PENDING | LISTED}, [0])
    case['memory'].append({'addr': f'buf:nodes+0x{LIST + LIST_CURRENT:x}', **node_pointer(entry(0))})
    out.append(case)

    # An entry between the current entry and the one after it.
    case = visit_case(address, 'entry_right_after_current', first_pair, {0: PENDING | LISTED}, [0])
    case['memory'] += [{'addr': f'buf:nodes+0x{entry(0) + ENTRY_PREVIOUS:x}', **node_pointer(CURRENT)},
                       {'addr': f'buf:nodes+0x{entry(0) + ENTRY_NEXT:x}', **node_pointer(CURRENT_NEXT)},
                       {'addr': f'buf:nodes+0x{CURRENT + ENTRY_NEXT:x}', **node_pointer(entry(0))},
                       {'addr': f'buf:nodes+0x{CURRENT_NEXT + ENTRY_PREVIOUS:x}', **node_pointer(entry(0))}]
    out.append(case)

    # The base visit publishes the array the entries are read from.
    case = visit_case(address, 'base_visit_publishes_array', first_pair, {0: PENDING | LISTED, 1: 3}, [0, 1])
    case['memory'].append({'addr': f'buf:object+0x{entries_field:x}', 'bytes': word(0)})
    case['stubs'][0]['writes'] = [{'arg': 0, 'offset': entries_field, 'pointer': 'array'}]
    out.append(case)

    # The second pair visit replaces the array: its entries are the ones visited.
    case = visit_case(address, 'pair_visit_replaces_array', first_pair,
                      {0: PENDING | LISTED, 1: 3, 2: PENDING | LISTED, 3: 0x10}, [0, 1])
    case['memory'] += [{'addr': f'buf:nodes+0x{STAGED_ARRAY:x}', **node_pointer(STAGED_ARRAY + 0x10)},
                       {'addr': f'buf:nodes+0x{STAGED_ARRAY + 8:x}', 'bytes': word(2)},
                       {'addr': f'buf:nodes+0x{STAGED_ARRAY + 0x10:x}', **node_pointer(entry(2))},
                       {'addr': f'buf:nodes+0x{STAGED_ARRAY + 0x18:x}', **node_pointer(entry(3))}]
    case['stubs'] += [{'address': VISIT_PAIR, 'argc': 2, 'argi': [0, 1], 'ret': 0},
                      {'address': VISIT_PAIR, 'argc': 2, 'argi': [0, 1], 'ret': 0,
                       'writes': [{'arg': 0, 'offset': ENTRIES_AFTER_FIRST - SECOND_PAIR_AFTER_FIRST,
                                   'pointer': f'nodes+0x{STAGED_ARRAY:x}'}]}]
    out.append(case)

    # Unlinking the first entry stores its next entry in the object's array pointer (its previous
    # entry is placed so that its next field is that pointer). The next entry then serves as the
    # array: its first qword is a data pointer to other entries, its previous field (the count's
    # place) a pointer. The second entry visited comes from that new data; the count stays 2.
    case = visit_case(address, 'unlink_replaces_array', first_pair,
                      {0: PENDING | LISTED, 1: 0x20, 2: PENDING | LISTED, 3: 0x40}, [0, 1])
    stand_in = NEIGHBOURS + 1 * 2 * ENTRY_SIZE   # entry 1's own previous: unused by entry 1 here
    case['memory'] += [{'addr': f'buf:nodes+0x{entry(0) + ENTRY_PREVIOUS:x}',
                        'pointer': f'object+0x{entries_field - ENTRY_NEXT:x}'},
                       {'addr': f'buf:nodes+0x{entry(0) + ENTRY_NEXT:x}', **node_pointer(stand_in)},
                       {'addr': f'buf:nodes+0x{stand_in:x}', **node_pointer(STAGED_ARRAY)},
                       {'addr': f'buf:nodes+0x{STAGED_ARRAY:x}', **node_pointer(entry(3))},
                       {'addr': f'buf:nodes+0x{STAGED_ARRAY + 8:x}', **node_pointer(entry(2))}]
    out.append(case)
    return out
