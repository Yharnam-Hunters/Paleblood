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
destroys_heap_buffer_owner_directly, destroys_heap_buffer_owner_through_base_at_0x38,
destroys_heap_buffer_owner_through_base_at_0x48 and frees_to_global_heap: the copies of a
destructor body entered at the owner or through one of two bases, and of a free to the heap the
global at 0x057e7320 points at (heap_buffer_owner_families.cpp). Their cases are
heap_buffer_owner_family_cases.py's, at each member's own address.
destroys_with_0x010f6190_then_deletes and the seven other destroys_with_..._then_deletes families
(destroy_then_delete_families.cpp): the copies of eight deleting destructors, one destructor each.
Their cases are destroy_then_delete_family_cases.py's, at each member's own address.

dispatches_on_slot_match_with_member_0x68/_0x70 and visits_base_pairs_and_entries_at_0x70/_0x78:
four scaleform families, two pairs that differ only in one member offset; their cases are in
game/engine/tests/scaleform_visit_dispatch_cases.py and their control mutants in
game/engine/tests/scaleform_visit_dispatch_mutants.c.

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

reallocates_array_of_8_byte_4_aligned_elements, reallocates_array_of_16_byte_elements,
reallocates_array_of_24_byte_handle_records, erases_range_of_24_byte_handle_records and
resizes_heap_array_of_8_byte_elements: the copies of an engine array's reallocation for three
element types, of a range erase from an array of the third, and of a resize of an array whose buffer comes from the heap at 0x057e7320
(array_resize_families.cpp). Their cases are array_resize_family_cases.py's, at each member's
own address.

The kind-sized storage families (game/engine/kind_sized_storage.cpp) run every kind and count rule
(kinds 4 and 6 with and without the ignored low bit, other kinds, a second answer that differs from
the first, empty, negative and wrapping counts), kind calls that change the count, the vtable or
the allocator pointer (the order controls) and failing allocations.

removes_debug_menu_entry_keyed_by_object, formats_one_of_three_texts_for_slot_0x80 and
releases_counted_member_at_0x30_then_base: their cases are in
game/engine/tests/debug_menu_release_cases.py and their control mutants in
game/engine/tests/debug_menu_release_mutants.c.

forwards_to_referenced_node_slot_0x28, posts_event_9_when_accepted, syncs_selected_id_and_index,
selects_entry_id_by_index, releases_counted_reference_at_0x20 and registers_new_entry_at_0x30:
the copies of six virtual methods (virtual_method_families.cpp). Their cases are
virtual_method_family_cases.py's, at each member's own address.

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

fills_owner_block_with_two_addresses and fills_owner_block_with_one_address
(owner_block_families.cpp), updates_and_calls_slot_0x38_unless_refused,
adopts_buffer_at_0x10_after_slot_0x78, destroys_with_0x0139bf90_then_deletes,
clears_0x28_and_passes_0x18_to_0x027286b0 and passes_float_to_0x01b5f7f0_when_0x010edc70_agrees
(forwarding_method_families.cpp): the copies of seven short functions. Their cases are
owner_block_and_forwarding_family_cases.py's, at each member's own address.

destroys_holder_of_shared_work (shared_work_holder_family.cpp): no work, user and reference
counts on both sides of 1 (and their extremes), and signal sets that clear the holder's work or
swap in another (the work is read again after the signal).

destroys_owner_of_wide_string_then_base_0x0139b630 and
deletes_owner_of_wide_string_after_base_0x0139b630 (wide_string_owner_family.cpp): the owned
flag clear or set (1, 0x80, 0xff), no string, capacities around the inline room and at the
extremes, and heap lookups that change the string's capacity or allocator (both read after it).

visits_children_of_list_at_0x40 and secondary_base_visits_children_of_list_at_0x40
(child_list_visit_family.cpp): no list, empty, negative and smallest counts, one to eight
children, and slot calls that swap the list or its array, change the count (read once) or a
later child.

selects_index_of_changed_id_with_0x01348fe0 (changed_id_index_family.cpp): no followed id, an
unchanged id, a match first, in the middle, last, none, an empty table, duplicates (the first
wins) and extreme ids.

initializes_owner_block_six_pointers_to_0x05ac5cac,
initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc,
initializes_owner_block_first_pointer_to_0x05ac74a3, destroys_pair_owner_with_vtable_0x0570fb90,
destroys_entry_storage_with_vtable_0x057045e0 and releases_counted_object_at_0x10
(block_init_and_owned_release_families.cpp): the copies of six short functions. Their cases are
block_init_and_owned_release_family_cases.py's, at each member's own address.

frees_8_byte_block_through_0x05ac8288, frees_sized_block_aligned_8_through_0x05ac8288,
allocates_aligned_8_through_0x05ac8288, allocates_with_alignment_through_0x05ac8288,
resets_with_0x02c375e0_and_returns_object, releases_value_at_0x8_with_0x005c14d0_then_clears_kind
and equals_by_kind_and_16_bytes_at_0x10 (allocator_forward_and_equality_families.cpp): the copies
of seven short functions. Their cases are allocator_forward_and_equality_family_cases.py's, at
each member's own address.
"""
from __future__ import annotations

import copy
import struct
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


THREAD_SLOT = '0x057e40f8'      # the thread-local offset the deleting functions read
THREAD_BLOCK = 0x80              # the thread pointer's offset in the tls buffer
HOLDER_SLOT = -0x30              # the thread-local offset of the allocator holder's pointer
SIZED_FREE = '0x004000a0'        # a stand-in for the allocator's slot 0x18, stubbed
SIZE_DEFAULT = 0xffff


def thread_free_case(address: str, case_id: str, header: int, seed: int) -> dict:
    """The thread pointer (gs) points into a fake thread block whose slot at HOLDER_SLOT points
    at a holder; its allocator's slot 0x18 is a stub comparing the allocator, the object and the
    size. HEADER is the object's dword at +8 (count, then allocation size)."""
    pattern = bytearray((seed * 7 + i) & 0xff for i in range(0x20))
    pattern[8:12] = header.to_bytes(4, 'little')
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i64',
            'buffers': {'tls': {'size': THREAD_BLOCK + 0x10}, 'holder': {'size': 0x60},
                        'allocator': {'size': 0x10}, 'allocator_vt': {'size': 0x20},
                        'object': {'size': 0x20, 'bytes': pattern.hex()}},
            'gs': f'buf:tls+{THREAD_BLOCK}',
            'memory': [{'addr': f'buf:tls+{THREAD_BLOCK}', 'pointer': f'tls+{THREAD_BLOCK}'},
                       {'addr': THREAD_SLOT, 'bytes': struct.pack('<q', HOLDER_SLOT).hex()},
                       {'addr': f'buf:tls+{THREAD_BLOCK + HOLDER_SLOT}', 'pointer': 'holder'},
                       {'addr': 'buf:holder+0x58', 'pointer': 'allocator'},
                       {'addr': 'buf:allocator', 'pointer': 'allocator_vt'},
                       {'addr': 'buf:allocator_vt+0x18', 'guest': SIZED_FREE}],
            'imports': [],
            'stubs': [{'address': SIZED_FREE, 'argc': 3, 'ret': seed << 32 | 0x5eed}],
            'args': {'rdi': 'buf:object', 'rsi': '0x1111111111111111', 'rdx': '0xffffffffffffffff',
                     'rax': '0x5aa5c33c0ff0e11e'}}


def thread_free_cases(address: str, value: int | None) -> list[dict]:
    """The allocation size the object gives (small, 0xfffe, 0) and size_default (the class
    size, the parameter); the count beside it set to all ones or 0."""
    return [thread_free_case(address, 'own_size', 0x0028_0001, 3),
            thread_free_case(address, 'own_size_largest', 0xfffe_ffff, 5),
            thread_free_case(address, 'own_size_zero', 0x0000_0007, 7),
            thread_free_case(address, 'class_size', SIZE_DEFAULT << 16 | 1, 11),
            thread_free_case(address, 'class_size_count_zero', SIZE_DEFAULT << 16, 13)]


STACK_GUARD_SLOT = '0x057e3be0'   # the import slot that points at the stack guard
HANDLER = '0x00400140'             # a stand-in for the delegate's function, stubbed
EVENT_SIZE = 0x10
HANDLER_SLOT = 0x20                # the virtual delegate's table offset


def delegate_case(address: str, case_id: str, function: str | None, seed: int) -> dict:
    """An object with a delegate at +0x50: FUNCTION None (unset), 'direct' (the handler's
    address) or 'virtual' (1 + HANDLER_SLOT into the bound object's table). The bound object
    is the target buffer plus an adjustment of 0x10. The handler compares the object, the
    context and the 16-byte event its second argument points at (on the stack)."""
    poster = bytearray((seed * 13 + i) & 0xff for i in range(0x70))
    poster[0x58:0x60] = bytes(8)
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'guard': {'size': 8, 'bytes': struct.pack('<Q', 0x2b992ddfa232 ^ seed).hex()},
                        'poster': {'size': 0x70, 'bytes': poster.hex()},
                        'target': {'size': 0x30, 'bytes': bytes((seed + i) & 0xff for i in range(0x30)).hex()},
                        'target_table': {'size': 0x30}},
            'memory': [{'addr': STACK_GUARD_SLOT, 'pointer': 'guard'}],
            'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:poster', 'rsi': hex(0xa5a5a5a5_00000000 | seed * 0x01010101),
                     'rdx': hex(0x0123456789abcdef ^ seed << 56), 'rcx': '0x7', 'rax': '0x5aa5c33c0ff0e11e'}}
    if function:
        case['memory'] += [{'addr': 'buf:poster+0x50', 'pointer': 'target'},
                           {'addr': 'buf:poster+0x60', 'bytes': struct.pack('<q', 0x10).hex()},
                           {'addr': 'buf:poster+0x68', 'bytes': struct.pack('<I', 0x900d0000 | seed).hex()},
                           {'addr': 'buf:target+0x10', 'pointer': 'target_table'},
                           {'addr': f'buf:target_table+0x{HANDLER_SLOT:x}', 'guest': HANDLER}]
        if function == 'direct':
            case['memory'].append({'addr': 'buf:poster+0x58', 'guest': HANDLER})
        else:
            case['memory'].append({'addr': 'buf:poster+0x58', 'bytes': struct.pack('<Q', HANDLER_SLOT + 1).hex()})
        case['stubs'].append({'address': HANDLER, 'argc': 3, 'argi': [0, 2], 'ret': 0x77,
                              'argmem': [{'arg': 1, 'size': EVENT_SIZE}]})
    return case


def delegate_event_cases(address: str, value: int | None) -> list[dict]:
    """No delegate (no call), a direct handler and a virtual one, with two seeds."""
    return [delegate_case(address, 'unset', None, 3),
            delegate_case(address, 'direct', 'direct', 5),
            delegate_case(address, 'virtual', 'virtual', 7),
            delegate_case(address, 'direct_other', 'direct', 11)]


FALLBACK = '0x00400180'            # stand-ins for the argument's slot 0x80 and the delegate, stubbed
FORWARDED_HANDLER = '0x004001c0'


def forward_case(address: str, case_id: str, function: str | None, seed: int) -> dict:
    """An owner with a delegate at +0x18 (FUNCTION as in delegate_case; the bound object is the
    target plus 0x10) and a second argument with a table whose slot 0x80 is a stub (comparing it
    and the address it gets). The handler compares the bound object, the argument and the
    context."""
    owner = bytearray((seed * 17 + i) & 0xff for i in range(0x48))
    owner[0x30:0x38] = bytes(8)
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i64',
            'buffers': {'owner': {'size': 0x48, 'bytes': owner.hex()},
                        'argument': {'size': 0x20, 'bytes': bytes((seed * 3 + i) & 0xff for i in range(0x20)).hex()},
                        'argument_table': {'size': 0x90},
                        'target': {'size': 0x30, 'bytes': bytes((seed + i) & 0xff for i in range(0x30)).hex()},
                        'target_table': {'size': 0x30}},
            'memory': [{'addr': 'buf:argument+0x0', 'pointer': 'argument_table'},
                       {'addr': 'buf:argument_table+0x80', 'guest': FALLBACK}],
            'imports': [],
            'stubs': [{'address': FALLBACK, 'argc': 2, 'ret': seed << 32 | 0x80}],
            'args': {'rdi': 'buf:owner', 'rsi': 'buf:argument', 'rdx': hex(0x0123456789abcdef ^ seed << 56),
                     'rax': '0x5aa5c33c0ff0e11e'}}
    if function:
        case['memory'] += [{'addr': 'buf:owner+0x18', 'pointer': 'target'},
                           {'addr': 'buf:owner+0x38', 'bytes': struct.pack('<q', 0x10).hex()},
                           {'addr': 'buf:owner+0x40', 'bytes': struct.pack('<I', 0x900d0000 | seed).hex()},
                           {'addr': 'buf:target+0x10', 'pointer': 'target_table'},
                           {'addr': f'buf:target_table+0x{HANDLER_SLOT:x}', 'guest': FORWARDED_HANDLER}]
        if function == 'direct':
            case['memory'].append({'addr': 'buf:owner+0x30', 'guest': FORWARDED_HANDLER})
        else:
            case['memory'].append({'addr': 'buf:owner+0x30', 'bytes': struct.pack('<Q', HANDLER_SLOT + 1).hex()})
        case['stubs'].append({'address': FORWARDED_HANDLER, 'argc': 3, 'ret': seed << 32 | 0x18})
    return case


def delegate_forward_cases(address: str, value: int | None) -> list[dict]:
    """No delegate (slot 0x80 of the argument), a direct handler and a virtual one."""
    return [forward_case(address, 'unset', None, 3),
            forward_case(address, 'direct', 'direct', 5),
            forward_case(address, 'virtual', 'virtual', 7),
            forward_case(address, 'unset_other', None, 11)]


def destructor_cases(address: str, value: int | None) -> list[dict]:
    """The thread allocator as in thread_free_case; the object's first qword (the vtable store)
    is compared with the memory, and the free gets 0x28 whatever the header says."""
    out = []
    for case_id, header, seed in (('header_own_size', 0x0030_0001, 17), ('header_default', SIZE_DEFAULT << 16, 19)):
        case = thread_free_case(address, case_id, header, seed)
        out.append(case)
    return out


WCSLEN = '0x02fbf228'
WIDE_STRING_ASSIGN = '0x02a2a310'


def text_event_case(address: str, case_id: str, text: str, function: str | None, seed: int) -> dict:
    """The poster's +0x48 points at TEXT (UTF-16, terminated); wcslen (when the text is not
    empty) and the wide string assign are stubs comparing the text; the delegate is as in
    delegate_case. The handler compares the bound object, the context and the event's kind and
    data (its middle dword is unset in the originals)."""
    case = delegate_case(address, case_id, function, seed)
    encoded = (text + '\0').encode('utf-16-le')
    case['buffers']['text'] = {'size': len(encoded), 'bytes': encoded.hex()}
    case['buffers']['string'] = {'size': 0x38, 'bytes': bytes((seed * 5 + i) & 0xff for i in range(0x38)).hex()}
    case['memory'].append({'addr': 'buf:poster+0x48', 'pointer': 'text'})
    case['args']['rsi'] = 'buf:string'
    stubs = []
    if text:
        stubs.append({'address': WCSLEN, 'argc': 1, 'ret': len(text),
                      'argmem': [{'arg': 0, 'size': len(encoded)}]})
    stubs.append({'address': WIDE_STRING_ASSIGN, 'argc': 3, 'ret': 0,
                  'argmem': [{'arg': 1, 'size': len(encoded)}]})
    for stub in case['stubs']:
        stub['argmem'] = [{'arg': 1, 'size': 4}, {'arg': 1, 'offset': 8, 'size': 8}]
    case['stubs'] = stubs + case['stubs']
    return case


def text_event_cases(address: str, value: int | None) -> list[dict]:
    """Text and empty text (no wcslen), each with the delegate unset, direct and virtual."""
    return [text_event_case(address, f'{name}_{function or "unset"}', text, function, seed)
            for name, text in (('text', 'Hunter'), ('empty', ''))
            for function, seed in ((None, 23), ('direct', 29), ('virtual', 31))]


CONSTRUCT_EVENT_BASE = '0x01400810'
DESTROY_EVENT = '0x013a75d0'
def record_event_compared(arg: int) -> list[dict]:
    return [{'arg': arg, 'size': 8}, {'arg': arg, 'offset': 0x20, 'size': 0x10}]


def record_event_case(address: str, case_id: str, record: str, function: str | None, seed: int) -> dict:
    """A poster with a delegate at +8 (object +8, function +0x10, adjustment +0x18, context +0x20):
    FUNCTION None leaves the bound object null; 'direct' or 'virtual' as in delegate_case. RECORD:
    'none' (a null second argument), 'no_second', 'no_first' or 'full'. The event's constructor,
    the handler and the event's destructor are stubs; the event's bytes the base constructor would
    set (+8..+0x20) are not compared, as the stub does not set them."""
    poster = bytearray((seed * 13 + i) & 0xff for i in range(0x30))
    poster[0x8:0x10] = bytes(8)
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'guard': {'size': 8, 'bytes': struct.pack('<Q', 0x2b992ddfa232 ^ seed).hex()},
                        'poster': {'size': 0x30, 'bytes': poster.hex()},
                        'record': {'size': 0x18, 'bytes': bytes((seed * 5 + i) & 0xff for i in range(0x18)).hex()},
                        'first': {'size': 0x10}, 'second': {'size': 0x10},
                        'target': {'size': 0x30, 'bytes': bytes((seed + i) & 0xff for i in range(0x30)).hex()},
                        'target_table': {'size': 0x30}},
            'memory': [{'addr': STACK_GUARD_SLOT, 'pointer': 'guard'}],
            'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:poster', 'rsi': '0x0' if record == 'none' else 'buf:record',
                     'rdx': '0x2222222222222222', 'rax': '0x5aa5c33c0ff0e11e'}}
    case['memory'] += [{'addr': 'buf:record+0x0', 'bytes': '00' * 8} if record == 'no_first'
                       else {'addr': 'buf:record+0x0', 'pointer': 'first'},
                       {'addr': 'buf:record+0x10', 'bytes': '00' * 8} if record == 'no_second'
                       else {'addr': 'buf:record+0x10', 'pointer': 'second'}]
    if function:
        case['memory'] += [{'addr': 'buf:poster+0x8', 'pointer': 'target'},
                           {'addr': 'buf:poster+0x18', 'bytes': struct.pack('<q', 0x10).hex()},
                           {'addr': 'buf:poster+0x20', 'bytes': struct.pack('<I', 0x900d0000 | seed).hex()},
                           {'addr': 'buf:target+0x10', 'pointer': 'target_table'},
                           {'addr': f'buf:target_table+0x{HANDLER_SLOT:x}', 'guest': HANDLER}]
        if function == 'direct':
            case['memory'].append({'addr': 'buf:poster+0x10', 'guest': HANDLER})
        else:
            case['memory'].append({'addr': 'buf:poster+0x10', 'bytes': struct.pack('<Q', HANDLER_SLOT + 1).hex()})
        if record == 'full':
            case['stubs'] = [{'address': CONSTRUCT_EVENT_BASE, 'argc': 1, 'argi': [], 'ret': 0},
                             {'address': HANDLER, 'argc': 3, 'argi': [0, 2], 'ret': 0x77,
                              'argmem': record_event_compared(1)},
                             {'address': DESTROY_EVENT, 'argc': 1, 'argi': [], 'ret': 0, 'argmem': record_event_compared(0)}]
    return case


def record_event_cases(address: str, value: int | None) -> list[dict]:
    """A full record with a direct and a virtual delegate; no bound object; and the record null,
    without its +0x10 and without its +0."""
    return [record_event_case(address, 'direct', 'full', 'direct', 3),
            record_event_case(address, 'virtual', 'full', 'virtual', 5),
            record_event_case(address, 'no_object', 'full', None, 7),
            record_event_case(address, 'no_record', 'none', 'direct', 11),
            record_event_case(address, 'no_second', 'no_second', 'direct', 13),
            record_event_case(address, 'no_first', 'no_first', 'virtual', 17)]


def sized_delete_cases(address: str, value: int | None) -> list[dict]:
    """The thread allocator as in thread_free_case: two objects (the free gets the size K, whatever
    the object says), and a null pointer (no call)."""
    out = []
    for case_id, seed in (('object', 3), ('other_object', 5), ('null', 7)):
        case = thread_free_case(address, case_id, SIZE_DEFAULT << 16 | seed, seed)
        case['returns'] = 'void'
        if case_id == 'null':
            case['args']['rdi'] = '0x0'
        out.append(case)
    return out


ALLOCATE = '0x00400020'          # a stand-in for the allocator's slot 0x10, stubbed


def allocate_cases(address: str, value: int | None) -> list[dict]:
    """The thread allocator as in thread_free_case, its slot 0x10 a stub comparing the allocator
    and the size and returning the allocation."""
    out = []
    for case_id, seed in (('allocate', 3), ('allocate_other', 5)):
        case = thread_free_case(address, case_id, 0, seed)
        case['memory'].append({'addr': 'buf:allocator_vt+0x10', 'guest': ALLOCATE})
        case['stubs'] = [{'address': ALLOCATE, 'argc': 2, 'ret': 0x7f00_0000_0000 | seed << 12}]
        case['args']['rdi'] = hex(0x0101010101010101 * seed)
        out.append(case)
    return out






SHARED_DESTRUCTOR = '0x024b5a60'

OPERATOR_DELETE = '0x02fbe588'

VIRTUAL_DESTRUCTOR = '0x00400240'
   # a stand-in for the object's slot 0x28, stubbed
DESTRUCTOR_SLOT = 0x28



def object_buffer(seed: int, size: int = 0x40) -> dict:
    return {'size': size, 'bytes': bytes((seed * 11 + i) & 0xff for i in range(size)).hex()}



OBJECT_ALLOCATOR = '0x057e7320'
    # the global that points at the allocator
ALLOCATOR_FREE = '0x00400280'
      # a stand-in for the allocator's slot 0x68, stubbed


def destroy_then_delete_cases(destructor: str | None, freed: bool = False):
    """The destructor, then operator delete (or, FREED, slot 0x68 of the allocator the global
    points at, comparing the allocator and the object), all stubs comparing the object; two
    seeds (the object's contents must come through untouched). DESTRUCTOR None: an empty
    function, run unstubbed."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for seed in (3, 5):
            case = {'schema': 1, 'address': address, 'id': f'object_{seed}', 'returns': 'i64' if freed else 'void',
                    'buffers': {'object': object_buffer(seed)}, 'memory': [], 'imports': [],
                    'stubs': [{'address': destructor, 'argc': 1, 'ret': seed}] if destructor else [],
                    'args': {'rdi': 'buf:object', 'rsi': hex(0x1111111111111111 * (seed & 0xf)),
                             'rax': '0x5aa5c33c0ff0e11e'}}
            if freed:
                case['buffers'].update({'allocator': object_buffer(seed + 1, 0x20), 'allocator_table': {'size': 0x80}})
                case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                                   {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                                   {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
                case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
            else:
                case['stubs'].append({'address': OPERATOR_DELETE, 'argc': 1, 'ret': 0})
            out.append(case)
        return out
    return cases



destroys_then_deletes_cases = destroy_then_delete_cases(SHARED_DESTRUCTOR)



def virtual_delete_case(case_id: str, address: str, flag: int, seed: int) -> dict:
    """An object whose table's slot 0x28 is a stub comparing the object; FLAG is the whole second
    argument (only its low byte decides operator delete, a stub comparing the object)."""
    stubs = [{'address': VIRTUAL_DESTRUCTOR, 'argc': 1, 'ret': seed << 8}]
    if flag & 0xff:
        stubs.append({'address': OPERATOR_DELETE, 'argc': 1, 'ret': 0})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'object': object_buffer(seed), 'table': {'size': 0x40}},
            'memory': [{'addr': 'buf:object+0x0', 'pointer': 'table'},
                       {'addr': f'buf:table+0x{DESTRUCTOR_SLOT:x}', 'guest': VIRTUAL_DESTRUCTOR}],
            'imports': [], 'stubs': stubs,
            'args': {'rdi': 'buf:object', 'rsi': hex(flag), 'rdx': '0x2222222222222222',
                     'rax': '0x5aa5c33c0ff0e11e'}}



def virtual_delete_cases(address: str, value: int | None) -> list[dict]:
    """The flag 1, 0, 0x100 (a set bit above the low byte: no delete) and 0xffffff80."""
    return [virtual_delete_case('delete', address, 1, 3),
            virtual_delete_case('keep', address, 0, 5),
            virtual_delete_case('keep_high_bits', address, 0x100, 7),
            virtual_delete_case('delete_high_bits', address, 0xffffff80, 11)]





RELEASE_COUNTED = '0x027ee1e0'

DESTROY_BASE = '0x006f92f0'
      # a stand-in for the allocator's slot 0x68, stubbed


def member_release_case(case_id: str, address: str, member: str | int, count: int | None, seed: int,
                        freed: bool = False) -> dict:
    """A holder whose +0xc0 is MEMBER: 0, a tagged value, or 'counted' (a buffer whose dword at
    +0x20 is COUNT). The release function (when the masked count is not 0) and the base
    destructor are stubs comparing their object and the bytes behind it (the vtable already
    stored, the count already decremented)."""
    holder = bytearray((seed * 13 + i) & 0xff for i in range(0xd0))
    holder[0xc0:0xc8] = (0 if member == 'counted' else member).to_bytes(8, 'little')
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'holder': {'size': 0xd0, 'bytes': holder.hex()}},
            'memory': [], 'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:holder', 'rsi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
    if member == 'counted':
        counted = bytearray((seed * 7 + i) & 0xff for i in range(0x28))
        counted[0x20:0x24] = count.to_bytes(4, 'little')
        case['buffers']['counted'] = {'size': 0x28, 'bytes': counted.hex()}
        case['memory'].append({'addr': 'buf:holder+0xc0', 'pointer': 'counted'})
        if count & 0x3fffff:
            case['stubs'].append({'address': RELEASE_COUNTED, 'argc': 1, 'ret': seed,
                                  'argmem': [{'arg': 0, 'size': 0x28}]})
    case['stubs'].append({'address': DESTROY_BASE, 'argc': 1, 'ret': 0,
                          'argmem': [{'arg': 0, 'offset': 0xc0, 'size': 8}]})
    if freed:
        # The deleting twin: then the allocator's slot 0x68, comparing the allocator and the holder.
        case['returns'] = 'i64'
        case['buffers'].update({'allocator': {'size': 0x20, 'bytes': bytes((seed * 3 + i) & 0xff for i in range(0x20)).hex()},
                                'allocator_table': {'size': 0x80}})
        case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                           {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                           {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
        case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
    return case



def member_release_cases(address: str, value: int | None, freed: bool = False) -> list[dict]:
    """No member; a tagged one; a counted one with the count 5, 0x400000 (masked 0: no release)
    and 0xffc00001 (masked 1). FREED: the deleting twin's cases."""
    return [member_release_case('none', address, 0, None, 3, freed),
            member_release_case('tagged', address, 0x123457, None, 5, freed),
            member_release_case('counted', address, 'counted', 5, 7, freed),
            member_release_case('counted_masked_zero', address, 'counted', 0x400000, 11, freed),
            member_release_case('counted_high_bits', address, 'counted', 0xffc00001, 13, freed)]



def member_release_free_cases(address: str, value: int | None) -> list[dict]:
    return member_release_cases(address, value, freed=True)





# (object, guard) of each guarded vtable setter; the vtable is whatever the original stores.
STATIC_VTABLE_SETTERS = {'sets_static_vtable_at_0x05ac59b0': (0x05ac59b0, 0x05ac59b8), 'sets_static_vtable_at_0x05ac59c0': (0x05ac59c0, 0x05ac59c8), 'sets_static_vtable_at_0x05ac59d0': (0x05ac59d0, 0x05ac59d8)}



def static_vtable_cases(family: str):
    """The guard 0 (the vtable is stored and the guard set), 1 and 0xff (nothing happens) and
    0x100 (only the low byte is tested: stored); the object starts as a pattern."""
    obj, guard = STATIC_VTABLE_SETTERS[family]

    def cases(address: str, value: int | None) -> list[dict]:
        return [{'schema': 1, 'address': address, 'id': f'guard_{start:x}', 'returns': 'void',
                 'buffers': {},
                 'memory': [{'addr': hex(obj), 'bytes': struct.pack('<Q', 0x5a5a5a5a5a5a5a5a ^ start).hex()},
                            {'addr': hex(guard), 'bytes': struct.pack('<Q', start).hex()}],
                 'imports': [], 'stubs': [],
                 'args': {'rdi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
                for start in (0, 1, 0xff, 0x100)]
    return cases



RELEASE_FUNCTION = '0x00400240'
   # a stand-in for slot 0x18 of the member's table, stubbed
RELEASE_SLOT = 0x18



def release_member_cases(address: str, value: int | None) -> list[dict]:
    """No member at +8 (only the vtable is stored), and a member whose table's slot 0x18 is a stub
    comparing the member; the object starts all ones and as a pattern."""
    out = []
    for seed, fill in ((3, 0xff), (5, None)):
        body = bytes(fill if fill is not None else (seed * 11 + i) & 0xff for i in range(0x20))
        for member in (False, True):
            c = {'schema': 1, 'address': address, 'id': f'{"member" if member else "no_member"}_{seed}',
                 'returns': 'void', 'buffers': {'object': {'size': 0x20, 'bytes': body.hex()}},
                 'memory': [{'addr': 'buf:object+0x8', 'bytes': bytes(8).hex()}], 'imports': [], 'stubs': [],
                 'args': {'rdi': 'buf:object', 'rsi': '0x2222222222222222', 'rax': '0x5aa5c33c0ff0e11e'}}
            if member:
                c['buffers']['member'] = {'size': 0x10, 'bytes': bytes((seed + i) & 0xff for i in range(0x10)).hex()}
                c['buffers']['table'] = {'size': 0x20}
                c['memory'] = [{'addr': 'buf:object+0x8', 'pointer': 'member'},
                               {'addr': 'buf:member+0x0', 'pointer': 'table'},
                               {'addr': f'buf:table+0x{RELEASE_SLOT:x}', 'guest': RELEASE_FUNCTION}]
                c['stubs'] = [{'address': RELEASE_FUNCTION, 'argc': 1, 'ret': seed}]
            out.append(c)
    return out





def array64_reserve_cases(address: str, value: int | None) -> list[dict]:
    """The cases of array64_reserve_cases.py (the hand-named copies) at this member's address."""
    return _load('array64_reserve_cases').reserve_cases(address)





def slot_table_cases(address: str, value: int | None) -> list[dict]:
    """A table buffer of 0x90 bytes (a pattern; the bytes past +0x80 must stay) and two owners;
    every written byte and the result are compared."""
    out = []
    for seed, owner in ((3, '0x123456789abcdef0'), (5, '0xfedcba9876543210')):
        out.append({'schema': 1, 'address': address, 'id': f'table_{seed}', 'returns': 'i64',
                    'buffers': {'table': {'size': 0x90, 'bytes': bytes((seed * 29 + i) & 0xff for i in range(0x90)).hex()}},
                    'memory': [], 'imports': [], 'stubs': [],
                    'args': {'rdi': owner, 'rsi': 'buf:table', 'rax': '0x5aa5c33c0ff0e11e'}})
    return out





MEMBER_RELEASE_0X10 = '0x02450560'

MEMBER_OPERATOR_DELETE = '0x02fbe588'

MEMBER_RELEASE_0X01401720 = '0x01401720'

MEMBER_FREE_SIZED = '0x02fa1c20'

MEMBER_OBJECT_ALLOCATOR = '0x05ac8288'



def holder(seed: int, size: int = 0x30) -> dict:
    return {'size': size, 'bytes': bytes((seed * 19 + i) & 0xff for i in range(size)).hex()}



def member_case(address: str, case_id: str, offset: int, set_member: bool, seed: int, returns: str = 'void') -> dict:
    """A holder whose member at OFFSET is a buffer (SET_MEMBER) or null."""
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': returns,
            'buffers': {'holder': holder(seed)}, 'memory': [], 'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:holder', 'rsi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
    if set_member:
        case['buffers']['member'] = holder(seed + 1, 0x20)
        case['memory'].append({'addr': f'buf:holder+0x{offset:x}', 'pointer': 'member'})
    else:
        case['memory'].append({'addr': f'buf:holder+0x{offset:x}', 'bytes': '00' * 8})
    return case



def vtable_release_cases(deletes: bool):
    """The member at +0x10 set (released with a stub comparing it, then cleared) or null; the base
    destructor (and, DELETES, operator delete) are stubs comparing the holder and its first 0x18
    bytes at the call (the table address stored, the member cleared). The base destructor is an
    empty function (returns_nothing) and runs as it is, unstubbed."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, set_member, seed in (('member', True, 3), ('no_member', False, 5)):
            case = member_case(address, case_id, 0x10, set_member, seed)
            if set_member:
                case['stubs'].append({'address': MEMBER_RELEASE_0X10, 'argc': 1, 'ret': seed})
            if deletes:
                case['stubs'].append({'address': MEMBER_OPERATOR_DELETE, 'argc': 1, 'ret': 0})
            out.append(case)
        return out
    return cases



def member_at_cases(offset: int):
    """The member at OFFSET set (released through 0x01401720, a stub comparing it, then cleared)
    or null."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, set_member, seed in (('member', True, 3), ('no_member', False, 5)):
            case = member_case(address, case_id, offset, set_member, seed)
            if set_member:
                case['stubs'].append({'address': MEMBER_RELEASE_0X01401720, 'argc': 1, 'ret': seed})
            out.append(case)
        return out
    return cases



def sized_free_cases(address: str, value: int | None) -> list[dict]:
    """The destructor (an empty function, returns_nothing, run unstubbed), then the sized free (a stub comparing the
    allocator the global points at, the object, the size and the alignment), whose result comes
    back."""
    out = []
    for seed in (3, 5):
        case = member_case(address, f'object_{seed}', 0x10, False, seed, 'i64')
        case['memory'] = []
        case['buffers']['allocator'] = holder(seed + 2, 0x20)
        case['memory'].append({'addr': MEMBER_OBJECT_ALLOCATOR, 'pointer': 'allocator'})
        case['stubs'] = [{'address': MEMBER_FREE_SIZED, 'argc': 4, 'ret': seed << 40 | 0xf3ee}]
        out.append(case)
    return out





SMALL_HEAP_OWNING = '0x0247b720'

SMALL_PATH_TABLE_FIND = '0x013e68f0'

SMALL_PATH_TABLE_RELEASE = '0x013e6ae0'

SMALL_COMPARE_MEMORY = '0x02fbe638'

SMALL_INTERFACE_ID = '0x04b2f320'

SMALL_FATAL_ERROR = '0x024b55b0'

SMALL_FATAL_ERROR_MESSAGE = '0x04b38390'

SMALL_RESOLVE = '0x02c4f560'

SMALL_TAGGED_STRING_RELEASE = '0x00836840'

SMALL_TAGGED_STRING_OWNER_BASE = '0x00d5f020'

SMALL_FLAGGED_RELEASE = '0x0271db70'
          # slot +0x18 of the table at 0x057b9530
SMALL_FLAGGED_BASE = '0x0271a600'

SMALL_OPERATOR_DELETE = '0x02fbe588'

SMALL_HAVOK_HEAP = '0x058018b0'

SMALL_RELEASE_ELEMENT = '0x02fa7940'

SMALL_STACK_GUARD_SLOT = '0x057e3be0'

# Stand-ins for the virtual functions fake tables point at.
SMALL_DESTROY, SMALL_HEAP_FREE, SMALL_SLOT = '0x00500100', '0x00500200', '0x00500300'



def seeded(size: int, seed: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}



def small_case(address: str, case_id: str, returns: str, args: dict, buffers: dict) -> dict:
    full = {'rdi': '0x1111111111111111', 'rsi': '0x2222222222222222', 'rdx': '0x3333333333333333',
            'rax': '0x5aa5c33c0ff0e11e'}
    full.update(args)
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': returns, 'buffers': buffers,
            'memory': [], 'imports': [], 'stubs': [], 'args': full}



def add_destroy_and_free(case: dict, name: str, seed: int) -> None:
    """NAME (a buffer) is destroyed through its table's second slot and freed through the heap the
    owning-heap stub returns; the destructor stub writes into the object, so its order shows."""
    case['buffers'].update({f'{name}_table': {'size': 0x10}, 'heap': seeded(0x10, seed + 7),
                            'heap_table': {'size': 0x78}})
    case['memory'] += [{'addr': f'buf:{name}+0x0', 'pointer': f'{name}_table'},
                       {'addr': f'buf:{name}_table+0x8', 'guest': SMALL_DESTROY},
                       {'addr': 'buf:heap+0x0', 'pointer': 'heap_table'},
                       {'addr': 'buf:heap_table+0x70', 'guest': SMALL_HEAP_FREE}]
    case['stubs'] += [{'address': SMALL_HEAP_OWNING, 'argc': 1, 'argi': [0], 'ret': 'buf:heap'},
                      {'address': SMALL_DESTROY, 'argc': 1, 'argi': [0],
                       'writes': [{'arg': 0, 'offset': 8, 'bytes': f'{0xd0 + seed:02x}' * 8}]},
                      {'address': SMALL_HEAP_FREE, 'argc': 2, 'argi': [0, 1], 'ret': 0}]



def owning_heap_delete_cases(address: str, value: int | None) -> list[dict]:
    """Two objects (destroyed, then freed through the heap that owns them) and null (nothing)."""
    out = []
    for seed in (3, 5):
        case = small_case(address, f'object_{seed}', 'void', {'rdi': 'buf:object'}, {'object': seeded(0x20, seed)})
        add_destroy_and_free(case, 'object', seed)
        out.append(case)
    out.append(small_case(address, 'null', 'void', {'rdi': '0x0'}, {}))
    return out



def path_entry_release_cases(address: str, value: int | None) -> list[dict]:
    """The release (a stub comparing the table at +0x68 and the entry) says last use or not, for an
    entry and for null; only a set entry at its last use is destroyed and freed."""
    out = []
    for case_id, entry, last_use, seed in (('last_use', True, 1, 3), ('still_used', True, 0, 5),
                                           ('null_last_use', False, 1, 7), ('null_still_used', False, 0, 9)):
        case = small_case(address, case_id, 'void', {'rdi': 'buf:holder', 'rsi': 'buf:entry' if entry else '0x0'},
                          {'holder': seeded(0x90, seed)})
        case['stubs'].append({'address': SMALL_PATH_TABLE_RELEASE, 'argc': 2, 'argi': [0, 1], 'ret': last_use})
        if entry:
            case['buffers']['entry'] = seeded(0x68, seed + 1)
            if last_use:
                add_destroy_and_free(case, 'entry', seed)
        out.append(case)
    return out



def path_entry_find_cases(address: str, value: int | None) -> list[dict]:
    """The find (a stub comparing the table at +0x68 and the key's text, hash and stale byte; the
    key is on the stack) returns an entry or null, which comes back."""
    out = []
    for case_id, found, seed in (('found', True, 3), ('not_found', False, 5)):
        case = small_case(address, case_id, 'i64', {'rdi': 'buf:holder', 'rsi': 'buf:text'},
                          {'holder': seeded(0x90, seed), 'text': seeded(0x20, seed + 1),
                           'guard': seeded(8, seed + 2)})
        case['memory'].append({'addr': SMALL_STACK_GUARD_SLOT, 'pointer': 'guard'})
        if found:
            case['buffers']['entry'] = seeded(0x60, seed + 3)
        case['stubs'].append({'address': SMALL_PATH_TABLE_FIND, 'argc': 2, 'argi': [0],
                              'argmem': [{'arg': 1, 'size': 0xd}], 'ret': 'buf:entry' if found else 0})
        out.append(case)
    return out



def interface_id_cases(address: str, value: int | None) -> list[dict]:
    """The id comparison (a stub comparing both pointers and the size) says equal (a reference is
    added and the object stored), greater or less (nothing); the result is the same."""
    out = []
    for case_id, compared, seed in (('equal', 0, 3), ('greater', 1, 5), ('less', 0xffffffff, 7)):
        case = small_case(address, case_id, 'i32', {'rdi': 'buf:object', 'rsi': 'buf:id', 'rdx': 'buf:out'},
                          {'object': seeded(0x18, seed), 'object_table': {'size': 0x10},
                           'id': seeded(0x10, seed + 1), 'out': seeded(8, seed + 2)})
        case['memory'] += [{'addr': 'buf:object+0x0', 'pointer': 'object_table'},
                           {'addr': 'buf:object_table+0x8', 'guest': SMALL_SLOT}]
        case['stubs'].append({'address': SMALL_COMPARE_MEMORY, 'argc': 3, 'argi': [0, 1, 2], 'ret': compared})
        if compared == 0:
            case['stubs'].append({'address': SMALL_SLOT, 'argc': 1, 'argi': [0], 'ret': 0})
        out.append(case)
    return out



def fatal_error_cases(address: str, value: int | None) -> list[dict]:
    """The report (a stub comparing the file, the line and the message)."""
    out = []
    for seed in (3, 5):
        case = small_case(address, f'report_{seed}', 'void', {'rdi': f'0x{seed:x}{seed:x}{seed:x}'}, {})
        case['stubs'].append({'address': SMALL_FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2], 'ret': 0})
        out.append(case)
    return out



def resolve_slot_cases(slot: int):
    """Kinds and indices (the index sign-extended) resolved by a stub comparing the context, the
    kind and the index; the resolved object's slot (a stub comparing it and the context) returns
    the result."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, kind, index, seed in (('kind_2', 2, 0xfffd, 3), ('kind_8001', 0x8001, 0x7fff, 5),
                                           ('index_8000', 0x7, 0x8000, 7)):
            reference = bytearray.fromhex(seeded(0x10, seed)['bytes'])
            reference[8:12] = kind.to_bytes(2, 'little') + index.to_bytes(2, 'little')
            case = small_case(address, case_id, 'i64', {'rdi': 'buf:reference', 'rsi': 'buf:context'},
                              {'reference': {'size': 0x10, 'bytes': reference.hex()},
                               'context': seeded(0x60, seed + 1), 'resolved': seeded(0x20, seed + 2),
                               'resolved_table': {'size': 0x60}})
            case['memory'] += [{'addr': 'buf:resolved+0x0', 'pointer': 'resolved_table'},
                               {'addr': f'buf:resolved_table+0x{slot:x}', 'guest': SMALL_SLOT}]
            case['stubs'] += [{'address': SMALL_RESOLVE, 'argc': 3, 'argi': [0, 1, 2], 'ret': 'buf:resolved'},
                              {'address': SMALL_SLOT, 'argc': 2, 'argi': [0, 1], 'ret': seed << 36 | 0x5107}]
            out.append(case)
        return out
    return cases



def tagged_string_destructor_cases(address: str, value: int | None) -> list[dict]:
    """The tagged string release and the base destructor are stubs comparing their argument."""
    out = []
    for seed in (3, 5):
        case = small_case(address, f'object_{seed}', 'void', {'rdi': 'buf:object'}, {'object': seeded(0x48, seed)})
        case['stubs'] += [{'address': SMALL_TAGGED_STRING_RELEASE, 'argc': 1, 'argi': [0], 'ret': 0},
                          {'address': SMALL_TAGGED_STRING_OWNER_BASE, 'argc': 1, 'argi': [0], 'ret': 0}]
        out.append(case)
    return out



def flagged_destructor_cases(deletes: bool):
    """The flag bit set or clear; the slot +0x18 call and the base destructor are stubs comparing
    the object and its first 0x10 bytes at the call (the table stored, then the flag cleared);
    DELETES: operator delete is a stub comparing the object."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, flags, seed in (('flag_set', 0xff, 3), ('flag_clear', 0x3a, 5)):
            case = small_case(address, case_id, 'void', {'rdi': 'buf:object'}, {'object': seeded(0x20, seed)})
            case['memory'].append({'addr': 'buf:object+0xb', 'bytes': f'{flags:02x}'})
            compared = [{'arg': 0, 'size': 0x10}]
            case['stubs'] += [{'address': SMALL_FLAGGED_RELEASE, 'argc': 1, 'argi': [0], 'argmem': compared, 'ret': 0},
                              {'address': SMALL_FLAGGED_BASE, 'argc': 1, 'argi': [0], 'argmem': compared, 'ret': 0}]
            if deletes:
                case['stubs'].append({'address': SMALL_OPERATOR_DELETE, 'argc': 1, 'argi': [0], 'ret': 0})
            out.append(case)
        return out
    return cases



def hk_array_cases(address: str, value: int | None) -> list[dict]:
    """Owned buffers (capacity 8, 8 with the 0x40000000 flag, 0) are freed through the Havok heap's
    slot +0x28 (a stub comparing the heap, the data and the low dword of the size); a buffer the
    array does not own (0x80000008) is not."""
    out = []
    for case_id, capacity, seed in (('owned_8', 8, 3), ('owned_flagged', 0x40000008, 5),
                                    ('owned_empty', 0, 7), ('not_owned', 0x80000008, 9)):
        case = small_case(address, case_id, 'void', {'rdi': 'buf:array'},
                          {'array': seeded(0x18, seed), 'data': seeded(0x20, seed + 1),
                           'heap_table': {'size': 0x30}})
        case['memory'] += [{'addr': 'buf:array+0x0', 'pointer': 'data'},
                           {'addr': 'buf:array+0x8', 'bytes': (5).to_bytes(4, 'little').hex() + capacity.to_bytes(4, 'little').hex()},
                           {'addr': SMALL_HAVOK_HEAP, 'pointer': 'heap_table'},
                           {'addr': 'buf:heap_table+0x28', 'guest': SMALL_HEAP_FREE}]
        if capacity < 0x80000000:
            case['stubs'].append({'address': SMALL_HEAP_FREE, 'argc': 2, 'argi': [0, 1], 'argu32': [2], 'ret': 0})
        out.append(case)
    return out



def element_array_cases(address: str, value: int | None) -> list[dict]:
    """Three, one and no elements, each released by a stub comparing it; the end becomes the begin."""
    out = []
    for count, seed in ((3, 3), (1, 5), (0, 7)):
        case = small_case(address, f'elements_{count}', 'void', {'rdi': 'buf:array'},
                          {'array': seeded(0x30, seed), 'items': seeded(0x48, seed + 1)})
        case['memory'] += [{'addr': 'buf:array+0x18', 'pointer': 'items'},
                           {'addr': 'buf:array+0x20', 'pointer': f'items+0x{count * 0x18:x}'}]
        case['stubs'] += [{'address': SMALL_RELEASE_ELEMENT, 'argc': 1, 'argi': [0], 'ret': 0} for _ in range(count)]
        out.append(case)
    return out




MEMBER_DESTRUCTOR = '0x005c9010'
BASE_DESTRUCTOR = '0x005696e0'
MEMBER_RELEASE = '0x0045a4c0'
CLASS_HEAD = 0x70                  # the bytes before the member at +0x70: the tables and +0x68


def member_destroying_object_buffer(seed: int, size: int = 0x100) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}


def member_destroying_cases(member: int, adjust: int, frees: bool, releases: bool):
    """An object (thunks get it at +ADJUST) whose member at +MEMBER goes to the member destructor,
    then the object to the base destructor (stubs comparing their argument; for RELEASES the
    destructor also sees the object's first 0x70 bytes still untouched, and the base destructor
    sees the two tables stored); RELEASES: the member at +0x68 set (released with a stub comparing
    it) or null. FREES: then slot 0x68 of the allocator the global points at, a stub comparing the
    allocator and the object, whose result comes back."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, seed, released in (('released', 3, True), ('no_release', 5, False)):
            if not releases:
                case_id = f'object_{seed}'
            case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i64' if frees else 'void',
                    'buffers': {'object': member_destroying_object_buffer(seed)}, 'memory': [], 'imports': [],
                    'stubs': [{'address': MEMBER_DESTRUCTOR, 'argc': 1, 'ret': seed},
                              {'address': BASE_DESTRUCTOR, 'argc': 1, 'ret': seed + 1}],
                    'args': {'rdi': f'buf:object+0x{adjust:x}', 'rsi': hex(0x1111111111111111 * (seed & 0xf)),
                             'rax': '0x5aa5c33c0ff0e11e'}}
            if releases:
                case['stubs'][0]['argmem'] = [{'arg': 0, 'offset': -member, 'size': CLASS_HEAD}]
                case['stubs'][1]['argmem'] = [{'arg': 0, 'offset': 0, 'size': CLASS_HEAD}]
                case['stubs'].append({'address': MEMBER_RELEASE, 'argc': 1, 'ret': seed + 2})
                if released:
                    case['buffers']['released'] = member_destroying_object_buffer(seed + 7, 0x20)
                    case['memory'].append({'addr': 'buf:object+0x68', 'pointer': 'released'})
                else:
                    case['memory'].append({'addr': 'buf:object+0x68', 'bytes': '00' * 8})
            if frees:
                case['buffers'].update({'allocator': member_destroying_object_buffer(seed + 1, 0x20), 'allocator_table': {'size': 0x80}})
                case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                                   {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                                   {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
                case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
            out.append(case)
        return out
    return cases




SLOT_STAND_IN = '0x00400240'        # a stand-in for the virtual slot a case calls, stubbed
ADJUSTED_DESTRUCTOR = '0x005696e0'
ADJUSTED_ALLOCATOR = '0x057e7320'   # the global that points at the allocator (free: slot 0x68)
COMPARE_BYTES = '0x02fbe638'
COPY_BYTES = '0x02fbe578'
FIND_ENTRY = '0x02e3b0f0'
INSERT_ENTRY = '0x02e3b210'
BLOCK_ALLOCATOR = '0x05ac8288'      # the global that points at the block allocator
ALLOCATE_ALIGNED = '0x02fa1b80'
FREE_SIZED = '0x02fa1c20'


def filled(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}


def le32(value: int) -> str:
    return (value & 0xffffffff).to_bytes(4, 'little').hex()


def make_case(address: str, case_id: str, returns: str, args: dict, buffers: dict, memory: list,
              stubs: list) -> dict:
    regs = {'rdi': '0x0', 'rsi': '0x1111111111111111', 'rdx': '0x2222222222222222', 'rcx': '0x3333333333333333',
            'rax': '0x5aa5c33c0ff0e11e'}
    regs.update(args)
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': returns, 'buffers': buffers,
            'memory': memory, 'imports': [], 'stubs': stubs, 'args': regs}


def adjusting_free_cases(address: str, value: int | None) -> list[dict]:
    """The pointer is 0x20 bytes into the object: the destructor (a stub comparing the object),
    then the allocator's slot 0x68 (a stub comparing the allocator and the object), whose result
    comes back; two seeds."""
    out = []
    for seed in (3, 5):
        buffers = {'object': filled(seed, 0x60), 'allocator': filled(seed + 1, 0x20), 'allocator_table': {'size': 0x80}}
        memory = [{'addr': ADJUSTED_ALLOCATOR, 'pointer': 'allocator'},
                  {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                  {'addr': 'buf:allocator_table+0x68', 'guest': SLOT_STAND_IN}]
        stubs = [{'address': ADJUSTED_DESTRUCTOR, 'argc': 1, 'ret': seed},
                 {'address': SLOT_STAND_IN, 'argc': 2, 'ret': seed << 40 | 0xf3ee}]
        out.append(make_case(address, f'object_{seed}', 'i64', {'rdi': 'buf:object+0x20'}, buffers, memory, stubs))
    return out


def interface_query_cases(address: str, value: int | None) -> list[dict]:
    """The key compares (stubs comparing the key, the known key and 16) equal to the first key,
    the second, or neither; results with only high bits set count as equal (the original tests
    eax). A match takes a reference (slot 0x8, a stub comparing the object) and stores the object."""
    out = []
    for case_id, first, second, seed in (('first', 0, None, 3), ('second', 1, 0, 5), ('neither', 1, 0xffffffff, 7),
                                         ('first_high_bits', 1 << 32, None, 9), ('second_high_bits', 0x80, 1 << 32, 11),
                                         ('neither_negative', 0xffffffff, 0x7fffffff, 13)):
        buffers = {'object': filled(seed, 0x20), 'table': {'size': 0x20}, 'key': filled(seed + 1, 0x10),
                   'out': filled(seed + 2, 0x10)}
        memory = [{'addr': 'buf:object+0x0', 'pointer': 'table'}, {'addr': 'buf:table+0x8', 'guest': SLOT_STAND_IN}]
        results = [first] if second is None else [first, second]
        stubs = [{'address': COMPARE_BYTES, 'argc': 3, 'ret': r} for r in results]
        if (results[-1] & 0xffffffff) == 0:
            stubs.append({'address': SLOT_STAND_IN, 'argc': 1, 'ret': seed})
        out.append(make_case(address, case_id, 'i32', {'rdi': 'buf:object', 'rsi': 'buf:key', 'rdx': 'buf:out'},
                             buffers, memory, stubs))
    return out


def store_setup(seed: int) -> tuple[dict, list]:
    """A store whose end node (+0x38) is end_node; node is an entry whose data and size are set."""
    buffers = {'store': filled(seed, 0x48), 'key': filled(seed + 1, 0x10), 'end_node': filled(seed + 2, 0x30),
               'node': filled(seed + 3, 0x30), 'source': filled(seed + 4, 0x20)}
    memory = [{'addr': 'buf:store+0x38', 'pointer': 'end_node'}, {'addr': 'buf:node+0x20', 'pointer': 'source'}]
    return buffers, memory


def find_stub(found: str, seed: int) -> dict:
    """The lookup writes the node it found at +0x10 of the iterator (a stack slot, not compared)."""
    return {'address': FIND_ENTRY, 'argc': 3, 'argi': [1, 2], 'ret': seed,
            'writes': [{'arg': 0, 'offset': 0x10, 'pointer': found}]}


def entry_copy_cases(address: str, value: int | None) -> list[dict]:
    """The key absent (the end node), or found with the caller's size below, equal to or above
    the entry's (an unsigned compare: 0x80000001 is above 0x7fffffff); the copy is a stub
    comparing the destination, the source and the size."""
    out = []
    for case_id, found, size, entry_size, seed in (('absent', 'end_node', 0x10, 8, 3), ('too_small', 'node', 3, 8, 5),
                                                   ('equal', 'node', 8, 8, 7), ('larger', 'node', 0x20, 8, 9),
                                                   ('unsigned_small', 'node', 0x7fffffff, 0x80000001, 11),
                                                   ('unsigned_large', 'node', 0x80000001, 0x10, 13)):
        buffers, memory = store_setup(seed)
        buffers.update({'size': {'size': 8, 'bytes': le32(size) + 'a5a5a5a5'}, 'destination': filled(seed + 5, 0x20)})
        memory.append({'addr': 'buf:node+0x28', 'bytes': le32(entry_size) + 'c3c3c3c3'})
        stubs = [find_stub(found, seed)]
        if found == 'node' and size >= entry_size:
            stubs.append({'address': COPY_BYTES, 'argc': 3, 'ret': 'buf:destination'})
        out.append(make_case(address, case_id, 'i32', {'rdi': 'buf:store', 'rsi': 'buf:key', 'rdx': 'buf:size',
                                                       'rcx': 'buf:destination'}, buffers, memory, stubs))
    return out


def entry_store_cases(address: str, value: int | None) -> list[dict]:
    """The key found or absent (then inserted: a stub comparing the tree and the new entry's key,
    null data and zero size, writing the inserted node), and the default key or not (the compare
    is a stub). The size's high bits are not stored."""
    out = []
    for case_id, found, compare, seed in (('found', True, 1, 3), ('found_default', True, 0, 5),
                                          ('inserted', False, 0xffffffff, 7), ('inserted_default', False, 0, 9),
                                          ('found_high_bits', True, 1 << 32, 11)):
        buffers, memory = store_setup(seed)
        stubs = [find_stub('node' if found else 'end_node', seed)]
        if not found:
            stubs.append({'address': INSERT_ENTRY, 'argc': 3, 'argi': [1], 'ret': seed + 1,
                          'argmem': [{'arg': 2, 'offset': 0, 'size': 0x1c}],
                          'writes': [{'arg': 0, 'offset': 0x10, 'pointer': 'node'}]})
        stubs.append({'address': COMPARE_BYTES, 'argc': 3, 'ret': compare})
        args = {'rdi': 'buf:store', 'rsi': 'buf:key', 'rdx': f'0x{0xabcdef01 << 32 | (seed << 8 | 0x10):x}',
                'rcx': f'0x{0x12345678 << 16 | seed << 4:x}'}
        out.append(make_case(address, case_id, 'i32', args, buffers, memory, stubs))
    return out


# (case id, layout kind results, count): the kind (& ~1) 4 gives three elements per count; 6 on
# the second query three plus two; any other one. The kind is eax: high bits do not count; 3n
# wraps in 32 bits; a negative count is sign-extended.
LAYOUTS = (('triples', [4], 5), ('triples_odd_kind', [5], 6), ('triples_high_bits', [1 << 32 | 4], 7),
           ('triples_wrap', [4], 0x40000001), ('plus_two', [6, 6], 7), ('plus_two_odd_kind', [1, 7], 9),
           ('single', [0, 0], 11), ('single_after_six', [6, 2], 13), ('single_negative', [0, 0], -3))


def layout_setup(seed: int, count: int, block: bool) -> tuple[dict, list]:
    buffers = {'object': filled(seed, 0x30), 'table': {'size': 0x100}, 'allocator': filled(seed + 1, 0x20)}
    memory = [{'addr': 'buf:object+0x0', 'pointer': 'table'},
              {'addr': 'buf:table+0x88', 'guest': SLOT_STAND_IN},
              {'addr': 'buf:object+0x18', 'bytes': le32(count)},
              {'addr': BLOCK_ALLOCATOR, 'pointer': 'allocator'}]
    if block:
        buffers['block'] = filled(seed + 2, 0x40)
        memory.append({'addr': 'buf:object+0x8', 'pointer': 'block'})
    else:
        memory.append({'addr': 'buf:object+0x8', 'bytes': '00' * 8})
    return buffers, memory


def kind_stubs(kinds: list[int]) -> list[dict]:
    """Slot 0x88 queried with the object and 0 (a stub comparing both)."""
    return [{'address': SLOT_STAND_IN, 'argc': 2, 'argi': [0], 'argu32': [1], 'ret': k} for k in kinds]


def layout_free_cases(address: str, value: int | None) -> list[dict]:
    """Each layout with a block (freed by a stub comparing the allocator, the block, the size and
    the alignment), and no block (nothing called); +0x8 and +0x10 are cleared."""
    out = []
    for n, (case_id, kinds, count) in enumerate(LAYOUTS):
        seed = 3 + 2 * n
        buffers, memory = layout_setup(seed, count, True)
        stubs = kind_stubs(kinds) + [{'address': FREE_SIZED, 'argc': 4, 'ret': seed}]
        out.append(make_case(address, case_id, 'i8', {'rdi': 'buf:object'}, buffers, memory, stubs))
    buffers, memory = layout_setup(41, 5, False)
    out.append(make_case(address, 'no_block', 'i8', {'rdi': 'buf:object'}, buffers, memory, []))
    return out


def layout_allocate_cases(sets_count: bool):
    """Each layout allocated (a stub comparing the allocator, the size and the alignment, returning
    storage), and one failing allocation. SETS_COUNT: the count comes in the second argument (its
    high bits are not stored) and the object's count is something else."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for n, (case_id, kinds, count) in enumerate(LAYOUTS + (('fails', [4], 5),)):
            seed = 3 + 2 * n
            buffers, memory = layout_setup(seed, 0x7777 if sets_count else count, False)
            buffers['storage'] = {'size': 0x10}
            ret = 0 if case_id == 'fails' else 'buf:storage'
            stubs = kind_stubs(kinds) + [{'address': ALLOCATE_ALIGNED, 'argc': 3, 'ret': ret}]
            args = {'rdi': 'buf:object'}
            if sets_count:
                args['rsi'] = f'0x{0xabcdef01 << 32 | (count & 0xffffffff):x}'
            out.append(make_case(address, case_id, 'i8', args, buffers, memory, stubs))
        return out
    return cases

CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'deletes_sized_through_thread_allocator': sized_delete_cases,
         'allocates_from_thread_allocator': allocate_cases,
         'posts_record_event_to_delegate_at_0x8': record_event_cases,
         'deleting_destructor_freeing_0x28_bytes': destructor_cases,
         'posts_text_event_to_delegate_at_0x50': text_event_cases,
         'calls_delegate_at_0x18_else_virtual_0x80': delegate_forward_cases,
         'posts_event_to_delegate_at_0x50': delegate_event_cases,
         'frees_object_through_thread_allocator': thread_free_cases,
         'destroys_then_deletes': destroys_then_deletes_cases,
         'calls_virtual_0x28_then_deletes_if_asked': virtual_delete_cases,
         'destroys_with_0x01402180_then_deletes': destroy_then_delete_cases('0x01402180'),
         'destroys_with_0x010e7ca0_then_deletes': destroy_then_delete_cases('0x010e7ca0'),
         'destroys_with_0x01401dc0_then_deletes': destroy_then_delete_cases('0x01401dc0'),
         'destroys_with_0x01640e70_then_deletes': destroy_then_delete_cases('0x01640e70'),
         'destroys_with_0x0139c4f0_then_deletes': destroy_then_delete_cases('0x0139c4f0'),
         'destroys_with_0x007164e0_then_frees': destroy_then_delete_cases('0x007164e0', freed=True),
         'destroys_with_0x005696e0_then_frees': destroy_then_delete_cases('0x005696e0', freed=True),
         'destroys_with_0x006fa840_then_frees': destroy_then_delete_cases('0x006fa840', freed=True),
         'destroys_with_0x0045a480_then_frees': destroy_then_delete_cases('0x0045a480', freed=True),
         'calls_empty_destructor_then_deletes': destroy_then_delete_cases(None),
         'destroys_with_0x010ee390_then_deletes': destroy_then_delete_cases('0x010ee390'),
         'destroys_with_0x016ea820_then_deletes': destroy_then_delete_cases('0x016ea820'),
         'destroys_with_0x02afd720_then_deletes': destroy_then_delete_cases('0x02afd720'),
         'destroys_with_0x0274a7f0_then_deletes': destroy_then_delete_cases('0x0274a7f0'),
         'destroys_with_0x006fbd50_then_frees': destroy_then_delete_cases('0x006fbd50', freed=True),

         'releases_member_at_0xc0_then_destroys_base': member_release_cases,
         'releases_member_at_0xc0_then_destroys_and_frees': member_release_free_cases,
         **{name: static_vtable_cases(name) for name in STATIC_VTABLE_SETTERS},
         'stores_vtable_0x05541b90_then_releases_member_at_8': release_member_cases,
         'reallocates_array64': array64_reserve_cases,
         'fills_3_slots_with_05ac5cac': slot_table_cases,
         'fills_1_slot_with_05ac5e6b': slot_table_cases,
         'fills_1_slot_with_05ac5cac': slot_table_cases,
         'fills_4_slots_with_05ac5cac': slot_table_cases,
         'fills_5_slots_with_05ac5cac': slot_table_cases,
         'fills_1_slot_with_05acd45c': slot_table_cases,
         'fills_1_slot_with_05acd1bc': slot_table_cases,
         'fills_2_slots_with_05acd45c': slot_table_cases,
         'fills_2_slots_with_05ac5cac': slot_table_cases,
         'fills_1_slot_with_05acee04': slot_table_cases,
         'fills_1_slot_with_05ac5dfd': slot_table_cases,
         'fills_1_slot_with_05ac5f5b': slot_table_cases,
         'fills_2_slots_05ac74a5_05ac74a3': slot_table_cases,
         'fills_2_slots_05ac74a7_05ac5cac': slot_table_cases,
         'fills_2_slots_05ac74a4_05ac74a3': slot_table_cases,
         'fills_2_slots_05ac74a9_05ac5cac': slot_table_cases,

         'stores_vtable_releases_0x10_then_destroys': vtable_release_cases(False),
         'stores_vtable_releases_0x10_destroys_then_deletes': vtable_release_cases(True),
         'releases_member_at_0x20_through_0x01401720': member_at_cases(0x20),
         'releases_member_at_0x18_through_0x01401720': member_at_cases(0x18),
         'destroys_then_frees_0x10_bytes_through_0x05ac8288': sized_free_cases,
         'destroys_and_frees_object_through_owning_heap': owning_heap_delete_cases,
         'releases_path_entry_then_deletes_it': path_entry_release_cases,
         'finds_path_entry_by_text': path_entry_find_cases,
         'adds_reference_on_interface_id_0x04b2f320': interface_id_cases,
         'reports_fatal_error_line_0xad': fatal_error_cases,
         'resolves_kind_and_index_then_calls_slot_0x38': resolve_slot_cases(0x38),
         'resolves_kind_and_index_then_calls_slot_0x58': resolve_slot_cases(0x58),
         'stores_vtable_releases_tagged_string_0x38_then_destroys': tagged_string_destructor_cases,
         'stores_vtable_calls_slot_0x18_clears_flag_then_destroys': flagged_destructor_cases(False),
         'stores_vtable_calls_slot_0x18_clears_flag_destroys_then_deletes': flagged_destructor_cases(True),
         'clears_hk_array_of_4_byte_elements': hk_array_cases,
         'releases_0x18_byte_elements_at_0x18_then_empties': element_array_cases,
         'destroys_member_0x68_then_base': member_destroying_cases(0x68, 0x0, False, False),
         'destroys_member_0x68_and_base_then_frees': member_destroying_cases(0x68, 0x0, True, False),
         'adjusts_0x20_destroys_member_0x68_then_base': member_destroying_cases(0x68, 0x20, False, False),
         'adjusts_0x20_destroys_member_0x68_and_base_then_frees': member_destroying_cases(0x68, 0x20, True, False),
         'adjusts_0x68_destroys_member_0x68_then_base': member_destroying_cases(0x68, 0x68, False, False),
         'adjusts_0x68_destroys_member_0x68_and_base_then_frees': member_destroying_cases(0x68, 0x68, True, False),
         'destroys_member_0x70_releases_0x68_then_base': member_destroying_cases(0x70, 0x0, False, True),
         'destroys_member_0x70_releases_0x68_and_base_then_frees': member_destroying_cases(0x70, 0x0, True, True),
         'adjusts_0x20_destroys_member_0x70_releases_0x68_then_base': member_destroying_cases(0x70, 0x20, False, True),
         'adjusts_0x20_destroys_member_0x70_releases_0x68_and_base_then_frees': member_destroying_cases(0x70, 0x20, True, True),
         'adjusts_0x70_destroys_member_0x70_releases_0x68_then_base': member_destroying_cases(0x70, 0x70, False, True),
         'adjusts_0x70_destroys_member_0x70_releases_0x68_and_base_then_frees': member_destroying_cases(0x70, 0x70, True, True),

         'thunk_minus_0x20_destroys_with_0x005696e0_then_frees': adjusting_free_cases,
         'queries_interface_0x04b2f0f0_or_0x04b2f320': interface_query_cases,
         'copies_entry_found_by_key_at_0x30': entry_copy_cases,
         'stores_entry_by_key_at_0x30': entry_store_cases,
         'frees_layout_block_align_16': layout_free_cases,
         'frees_layout_block_align_4': layout_free_cases,
         'allocates_layout_block_for_stored_count': layout_allocate_cases(False),
         'allocates_layout_block_for_count': layout_allocate_cases(True)}
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


CASES.update({
    'reallocates_array_of_4_byte_elements': reallocation_cases(4),
    'reallocates_array_of_12_byte_elements': reallocation_cases(12),
    'grows_full_array_of_16_byte_elements': growth_cases(16),
    'takes_and_erases_map_entry_at_0x3b0': map_take_cases,
    'releases_slot_blocks_at_0x38': slot_blocks_cases})


_VARIANT_STRING_POOL = _load('variant_string_pool_family_cases')

CASES.update({
    'sets_property_0x3ea_or_0x3eb_from_variant': _VARIANT_STRING_POOL.variant_property_cases,
    'copies_wide_string_at_0x28_on_thread_heap': _VARIANT_STRING_POOL.wide_string_copy_cases,
    'appends_blocks_to_pool_slot_5_or_16': _VARIANT_STRING_POOL.pool_append_cases,
    'assigns_created_string_unless_exception': _VARIANT_STRING_POOL.string_thunk_cases})


def _owner(name: str):
    return getattr(_load('heap_buffer_owner_family_cases'), name)


CASES.update({
    'destroys_heap_buffer_owner_directly': _owner('directly_cases'),
    'destroys_heap_buffer_owner_through_base_at_0x38': _owner('through_base_at_0x38_cases'),
    'destroys_heap_buffer_owner_through_base_at_0x48': _owner('through_base_at_0x48_cases'),
    'frees_to_global_heap': _owner('free_cases')})


def _deleting(destroy: str):
    return _load('destroy_then_delete_family_cases').deleting_cases(destroy)


CASES.update({
    'destroys_with_0x010f6190_then_deletes': _deleting('0x010f6190'),
    'destroys_with_0x02af2b00_then_deletes': _deleting('0x02af2b00'),
    'destroys_with_0x02af4240_then_deletes': _deleting('0x02af4240'),
    'destroys_with_0x02af6d40_then_deletes': _deleting('0x02af6d40'),
    'destroys_with_0x02af8220_then_deletes': _deleting('0x02af8220'),
    'destroys_with_0x02af9940_then_deletes': _deleting('0x02af9940'),
    'destroys_with_0x02afc240_then_deletes': _deleting('0x02afc240'),
    'destroys_with_0x02afee40_then_deletes': _deleting('0x02afee40')})

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # No call (another rax, no recorded free), and the neighbouring class size's replacement.
    'frees_object_through_thread_allocator': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    # No call, and the other event kind's replacement.
    'posts_event_to_delegate_at_0x50': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    # No call; and the delegate called without its adjustment (slot 0x80 given 0).
    'calls_delegate_at_0x18_else_virtual_0x80': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                                 'bb_mutant_delegate_at_0x18_without_adjustment'],
    # No write and no call. (A mutant that gets the vtable or the thread allocator wrong would need
    # the game image, which the mutant library does not see.)
    'deleting_destructor_freeing_0x28_bytes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'posts_text_event_to_delegate_at_0x50': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'posts_record_event_to_delegate_at_0x8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    # No call, and the neighbouring size's replacement.
    'deletes_sized_through_thread_allocator': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'allocates_from_thread_allocator': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    # No call; and (virtual 0x28) the destructor called but the object never deleted.
    'destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_virtual_0x28_then_deletes_if_asked': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                                 'bb_mutant_virtual_0x28_never_deletes'],
    'destroys_with_0x01402180_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x010e7ca0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x01401dc0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x01640e70_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0139c4f0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x007164e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x005696e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x006fa840_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0045a480_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_empty_destructor_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x010ee390_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x016ea820_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x02afd720_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0274a7f0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x006fbd50_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    # No call and no write. (A mutant reaching the vtable or the base destructor would need the
    # game image, which the mutant library does not see.)
    'releases_member_at_0xc0_then_destroys_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_member_at_0xc0_then_destroys_and_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    # Nothing stored. (A mutant storing the wrong vtable would need the game image.)
    'sets_static_vtable_at_0x05ac59b0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59c0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59d0': ['bb_mutant_does_nothing'],
    # Nothing stored or called; or the member released but no vtable stored.
    'stores_vtable_0x05541b90_then_releases_member_at_8': ['bb_mutant_does_nothing', 'bb_mutant_releases_member_at_8_only'],

    'reallocates_array64': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    # No write. (A mutant with the wrong pointers would need the game image.)
    'fills_3_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5e6b': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_4_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_5_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05acd45c': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05acd1bc': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_with_05acd45c': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_with_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05acee04': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5dfd': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_1_slot_with_05ac5f5b': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a5_05ac74a3': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a7_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a4_05ac74a3': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'fills_2_slots_05ac74a9_05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    'stores_vtable_releases_0x10_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_releases_0x10_destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_member_at_0x20_through_0x01401720': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_member_at_0x18_through_0x01401720': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_then_frees_0x10_bytes_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    'destroys_and_frees_object_through_owning_heap': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_path_entry_then_deletes_it': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'finds_path_entry_by_text': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adds_reference_on_interface_id_0x04b2f320': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'reports_fatal_error_line_0xad': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'resolves_kind_and_index_then_calls_slot_0x38': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'resolves_kind_and_index_then_calls_slot_0x58': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_releases_tagged_string_0x38_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_calls_slot_0x18_clears_flag_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_calls_slot_0x18_clears_flag_destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'clears_hk_array_of_4_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_0x18_byte_elements_at_0x18_then_empties': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    'destroys_member_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_member_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x68_destroys_member_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x68_destroys_member_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_member_0x70_releases_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_member_0x70_releases_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x70_releases_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x20_destroys_member_0x70_releases_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x70_destroys_member_0x70_releases_0x68_then_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'adjusts_0x70_destroys_member_0x70_releases_0x68_and_base_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    'thunk_minus_0x20_destroys_with_0x005696e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'queries_interface_0x04b2f0f0_or_0x04b2f320': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'copies_entry_found_by_key_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_entry_by_key_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'frees_layout_block_align_16': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'frees_layout_block_align_4': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'allocates_layout_block_for_stored_count': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'allocates_layout_block_for_count': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'reallocates_array_of_4_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array_reserve_8_byte_elements', 'bb_mutant_array32_reserve_keeps_old_buffer'],
    'reallocates_array_of_12_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array_reserve_8_byte_elements', 'bb_mutant_array96_reserve_keeps_old_buffer'],
    'grows_full_array_of_16_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array128_grows_by_one', 'bb_mutant_array128_grows_with_room'],
    'takes_and_erases_map_entry_at_0x3b0': ['bb_mutant_does_nothing', 'bb_mutant_map_take_keeps_entry', 'bb_mutant_map_take_without_reference', 'bb_mutant_map_take_signed_ids'],
    'releases_slot_blocks_at_0x38': ['bb_mutant_does_nothing', 'bb_mutant_slot_blocks_id_after_free', 'bb_mutant_slot_blocks_count_once', 'bb_mutant_slot_blocks_array_once'],
    # variant_string_pool_family_cases.py: the generic wrong bodies, and control mutants that
    # each move one read or step (game/engine/tests/family_mutants.c).
    'sets_property_0x3ea_or_0x3eb_from_variant': ['bb_mutant_does_nothing', 'bb_mutant_variant_property_rounds', 'bb_mutant_variant_property_skips_reference'],
    'copies_wide_string_at_0x28_on_thread_heap': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument', 'bb_mutant_wide_string_copy_reads_text_late', 'bb_mutant_wide_string_copy_reads_owns_early'],
    'appends_blocks_to_pool_slot_5_or_16': ['bb_mutant_does_nothing', 'bb_mutant_returns_one', 'bb_mutant_pool_appends_keep_first_count', 'bb_mutant_pool_appends_used_from_snapshot', 'bb_mutant_pool_appends_second_read_early'],
    'assigns_created_string_unless_exception': ['bb_mutant_does_nothing', 'bb_mutant_string_thunk_reads_exception_early', 'bb_mutant_string_thunk_drops_snapshot'],
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'destroys_heap_buffer_owner_directly': ['bb_mutant_does_nothing', 'bb_mutant_heap_buffer_owner_handle_snapshot_directly', 'bb_mutant_heap_buffer_owner_vtable_late_directly', 'bb_mutant_heap_buffer_owner_whole_state_directly', 'bb_mutant_heap_buffer_owner_no_release_directly'],
    'destroys_heap_buffer_owner_through_base_at_0x38': ['bb_mutant_does_nothing', 'bb_mutant_heap_buffer_owner_handle_snapshot_base_0x38', 'bb_mutant_heap_buffer_owner_vtable_late_base_0x38', 'bb_mutant_heap_buffer_owner_whole_state_base_0x38', 'bb_mutant_heap_buffer_owner_no_release_base_0x38'],
    'destroys_heap_buffer_owner_through_base_at_0x48': ['bb_mutant_does_nothing', 'bb_mutant_heap_buffer_owner_handle_snapshot_base_0x48', 'bb_mutant_heap_buffer_owner_vtable_late_base_0x48', 'bb_mutant_heap_buffer_owner_whole_state_base_0x48', 'bb_mutant_heap_buffer_owner_no_release_base_0x48'],
    'frees_to_global_heap': ['bb_mutant_does_nothing', 'bb_mutant_global_heap_frees_null'],
    # Doing nothing makes neither call; each other mutant says in tests/family_mutants.c how it
    # differs from the family it is listed for.
    'destroys_with_0x010f6190_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x010f6190_kept', 'bb_mutant_deleting_dtor_0x010f6190_deletes_first'],
    'destroys_with_0x02af2b00_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af2b00_kept', 'bb_mutant_deleting_dtor_0x02af2b00_deletes_first'],
    'destroys_with_0x02af4240_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af4240_kept', 'bb_mutant_deleting_dtor_0x02af4240_deletes_first'],
    'destroys_with_0x02af6d40_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af6d40_kept', 'bb_mutant_deleting_dtor_0x02af6d40_deletes_first'],
    'destroys_with_0x02af8220_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af8220_kept', 'bb_mutant_deleting_dtor_0x02af8220_deletes_first'],
    'destroys_with_0x02af9940_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02af9940_kept', 'bb_mutant_deleting_dtor_0x02af9940_deletes_first'],
    'destroys_with_0x02afc240_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02afc240_kept', 'bb_mutant_deleting_dtor_0x02afc240_deletes_first'],
    'destroys_with_0x02afee40_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_deleting_dtor_skips_destructor', 'bb_mutant_deleting_dtor_0x02afee40_kept', 'bb_mutant_deleting_dtor_0x02afee40_deletes_first'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)


# Families from #1168.
def scaleform_cases(kind: str, offset: int):
    """The cases of tests/scaleform_visit_dispatch_cases.py's KIND_cases for one member offset."""
    def build(address: str, value: int | None) -> list[dict]:
        return getattr(_load('tests/scaleform_visit_dispatch_cases'), f'{kind}_cases')(address, offset)
    return build


# Each scaleform control moves one read across the call or store that can change it
# (game/engine/tests/scaleform_visit_dispatch_mutants.c).
SLOT_FLAWS = ['first_slot_before_resolve', 'handle_not_reloaded', 'slots_after_resolve', 'slots_kept_for_second', 'kind_kept_for_second', 'second_slot_before_resolve']


# count_every_entry is withdrawn: its kills came only from faulting cases
VISIT_FLAWS = ['entries_before_calls', 'entries_read_once']


CASES.update({
    'dispatches_on_slot_match_with_member_0x68': scaleform_cases('dispatch', 0x68),
    'dispatches_on_slot_match_with_member_0x70': scaleform_cases('dispatch', 0x70),
    'visits_base_pairs_and_entries_at_0x70': scaleform_cases('visit', 0x70),
    'visits_base_pairs_and_entries_at_0x78': scaleform_cases('visit', 0x78),
})

MUTANTS.update({
    'dispatches_on_slot_match_with_member_0x68': ['bb_mutant_does_nothing'] + [f'bb_mutant_slot_dispatch_0x68_{flaw}' for flaw in SLOT_FLAWS],
    'dispatches_on_slot_match_with_member_0x70': ['bb_mutant_does_nothing'] + [f'bb_mutant_slot_dispatch_0x70_{flaw}' for flaw in SLOT_FLAWS],
    'visits_base_pairs_and_entries_at_0x70': ['bb_mutant_does_nothing'] + [f'bb_mutant_visit_0x70_{flaw}' for flaw in VISIT_FLAWS],
    'visits_base_pairs_and_entries_at_0x78': ['bb_mutant_does_nothing'] + [f'bb_mutant_visit_0x78_{flaw}' for flaw in VISIT_FLAWS],
})


# Families from #1170.
# stores_vtable_0x0579c8a0_releases_members_then_destroys and its deleting twin
# (vtable_0x0579c8a0_destructor_family.cpp): an object (`object`, 0x2018 bytes of filler) whose
# callable slot (+0x1ff0, pointer +0x2010), wide string (+0x1fb0) and held blocks (+0xd30,
# +0x1f30) are set per case; the callable, the string's allocator and the releasers have fake
# tables whose slots are scripted stubs comparing every argument; the member and base destructors
# are stubs too. Some stubs write into the object to show which fields are read after them.
# Generated fixtures, not game data.
DESTROY_FIRST_MEMBER, DESTROY_BASE_MEMBER, DESTROY_BASE_pr1170 = '0x01f4cbb0', '0x01ed24f0', '0x01ed5390'


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
    stubs.append({'address': DESTROY_BASE_pr1170, 'argc': 1, 'ret': '0x5a5a5a5a00000001'})
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


def destructor_cases_pr1170(address: str, value: int | None) -> list[dict]:
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


TABLE_END, TABLE_BUCKETS, TABLE_MASK, TABLE_COUNT_pr1170 = 0x8, 0x28, 0x48, 0x50


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
    memory.append({'addr': f'buf:{buffer}+{off + TABLE_COUNT_pr1170:#x}', 'bytes': _qword(count)})


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


CASES.update({
    'stores_vtable_0x0579c8a0_releases_members_then_destroys': destructor_cases_pr1170,
    'stores_vtable_0x0579c8a0_releases_members_destroys_then_deletes': deleting_destructor_cases,
    'sorts_16_byte_elements_with_comparator': sort_cases,
    'finds_id_pair_in_nested_hash_maps': hash_lookup_cases,
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'stores_vtable_0x0579c8a0_releases_members_then_destroys': ['bb_mutant_does_nothing', 'bb_mutant_vtable_0x0579c8a0_destroy_reads_first'],
    'stores_vtable_0x0579c8a0_releases_members_destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_vtable_0x0579c8a0_delete_reads_first', 'bb_mutant_vtable_0x0579c8a0_delete_without_delete'],
    'sorts_16_byte_elements_with_comparator': ['bb_mutant_does_nothing', 'bb_mutant_sort_pivot_by_reference', 'bb_mutant_sort_pivot_at_first', 'bb_mutant_sort_recurses_right'],
    'finds_id_pair_in_nested_hash_maps': ['bb_mutant_does_nothing', 'bb_mutant_hash_pair_signed_ids', 'bb_mutant_hash_pair_first_only'],
})


# Families from #1171.
def _resize(name: str):
    return getattr(_load('array_resize_family_cases'), name)


CASES.update({
    'reallocates_array_of_8_byte_4_aligned_elements': _resize('reallocation_cases')(8),
    'reallocates_array_of_16_byte_elements': _resize('reallocation_cases')(16),
    'reallocates_array_of_24_byte_handle_records': _resize('handle_record_cases'),
    'erases_range_of_24_byte_handle_records': _resize('erase_cases'),
    'resizes_heap_array_of_8_byte_elements': _resize('heap_resize_cases'),
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'reallocates_array_of_8_byte_4_aligned_elements': ['bb_mutant_does_nothing', 'bb_mutant_array64_reserve_8_aligned', 'bb_mutant_array64_4_aligned_reserve_keeps_old_buffer'],
    'reallocates_array_of_16_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_array128_reserve_as_8_byte_elements', 'bb_mutant_array128_reserve_keeps_old_buffer'],
    'reallocates_array_of_24_byte_handle_records': ['bb_mutant_does_nothing', 'bb_mutant_handle_records_snapshot_ends', 'bb_mutant_handle_records_reload_copy_end', 'bb_mutant_handle_records_free_early', 'bb_mutant_handle_records_without_destroy'],
    'erases_range_of_24_byte_handle_records': ['bb_mutant_does_nothing', 'bb_mutant_erase_records_snapshot_end', 'bb_mutant_erase_records_whole_snapshot_begin', 'bb_mutant_erase_records_without_destroy'],
    'resizes_heap_array_of_8_byte_elements': ['bb_mutant_does_nothing', 'bb_mutant_heap_array_grows_to_count', 'bb_mutant_heap_array_never_shrinks', 'bb_mutant_heap_array_rounds_to_8', 'bb_mutant_heap_array_allocates_for_array'],
})


# Families from #1172.
# The kind-sized storage families (game/engine/kind_sized_storage.cpp). The object lives in
# writable image memory just past the allocator pointer, so a kind call can change the count, the
# vtable or the allocator pointer through its object argument (stub writes at fixed offsets).
KIND_ALLOCATE = '0x02fa1b80'


KIND_FREE = '0x02fa1c20'


KIND_ALLOCATOR = 0x05ac8288


KIND_OBJECT = 0x05ac82c0


KIND_CALL = '0x00500100'        # what the kind slot (vtable +0x88) points at: a stub


KIND_OTHER_CALL = '0x00500180'  # the slot of the vtable a kind call may install


KIND_SLOT = 0x88


KIND_COUNT = 0x18


def kind_holder(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 19 + i) & 0xff for i in range(size)).hex()}


def kind_word(value: int, size: int = 4) -> str:
    return (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little').hex()


def kind_case(address: str, case_id: str, returns: str, kinds: list[int], count: int,
              changes: dict | None = None) -> dict:
    """An object of COUNT entries whose kind calls answer KINDS in turn (the second only when the
    first is not kind 4); CHANGES: what the first kind call writes ('count', 'vtable',
    'allocator')."""
    changes = changes or {}
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': returns,
            'buffers': {'vtable': kind_holder(7, KIND_SLOT + 8), 'other_vtable': kind_holder(9, KIND_SLOT + 8),
                        'allocator': kind_holder(11, 0x20), 'other_allocator': kind_holder(13, 0x20)},
            'memory': [{'addr': hex(KIND_OBJECT), 'bytes': kind_holder(17, 0x20)['bytes']},
                       {'addr': hex(KIND_OBJECT), 'pointer': 'vtable'},
                       {'addr': hex(KIND_OBJECT + KIND_COUNT), 'bytes': kind_word(count)},
                       {'addr': f'buf:vtable+0x{KIND_SLOT:x}', 'guest': KIND_CALL},
                       {'addr': f'buf:other_vtable+0x{KIND_SLOT:x}', 'guest': KIND_OTHER_CALL},
                       {'addr': hex(KIND_ALLOCATOR), 'pointer': 'allocator'}],
            'imports': [], 'stubs': [],
            'args': {'rdi': f'guest:{KIND_OBJECT:#x}', 'rsi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}

    writes = []
    if 'count' in changes:
        writes.append({'arg': 0, 'offset': KIND_COUNT, 'bytes': kind_word(changes['count'])})
    if changes.get('vtable'):
        writes.append({'arg': 0, 'offset': 0, 'pointer': 'other_vtable'})
    if changes.get('allocator'):
        writes.append({'arg': 0, 'offset': KIND_ALLOCATOR - KIND_OBJECT, 'pointer': 'other_allocator'})
    asked = kinds[:1] if (kinds[0] & 0xfffffffe) == 4 or len(kinds) == 1 else kinds[:2]
    for n, kind in enumerate(asked):
        stub = {'address': KIND_OTHER_CALL if n and changes.get('vtable') else KIND_CALL, 'argc': 2,
                'argmem': [{'arg': 0, 'offset': KIND_COUNT, 'size': 4}],
                'ret': hex(0x1234567800000000 | (kind & 0xffffffff))}
        if n == 0 and writes:
            stub['writes'] = writes
        case['stubs'].append(stub)
    return case


# (id, kinds, count, changes): every kind and count rule, and kind calls that change the count,
# the vtable or the allocator pointer.
KIND_SIZE_CASES = [
    ('kind4', [4], 7, None), ('kind5', [5], 7, None), ('kind6', [6, 6], 7, None),
    ('kind7', [7, 7], 7, None), ('kind0', [0, 0], 7, None), ('kind2', [2, 3], 7, None),
    ('kind6_then_4', [6, 4], 7, None), ('kind0_then_6', [0, 6], 7, None),
    ('kind1_then_7', [1, 7], 5, None), ('kind_high', [0xfffffffe, 0xfffffffe], 7, None),
    ('kind4_empty', [4], 0, None), ('kind6_empty', [6, 6], 0, None), ('kind0_one', [0, 0], 1, None),
    ('kind4_negative', [4], -1, None), ('kind6_negative', [6, 6], -3, None), ('kind0_negative', [0, 0], -2, None),
    ('kind4_wraps', [4], 0x55555556, None), ('kind6_wraps', [6, 6], 0x55555556, None),
    ('kind0_largest', [0, 0], 0x7fffffff, None),
    ('kind4_count_changes', [4], 7, {'count': 11}), ('kind6_count_changes', [6, 6], 7, {'count': 11}),
    ('kind0_count_changes', [0, 0], 9, {'count': 3}),
    ('vtable_changes', [0, 6], 7, {'vtable': True}), ('vtable_changes_kind4', [6, 4], 7, {'vtable': True}),
    ('allocator_changes', [4], 7, {'allocator': True}), ('allocator_changes_kind6', [0, 6], 7, {'allocator': True}),
]


def kind_free_cases(address: str, value: int | None) -> list[dict]:
    """The sized free (a stub comparing the allocator, the buffer, the size and the alignment)."""
    out = []
    for case_id, kinds, count, changes in KIND_SIZE_CASES:
        case = kind_case(address, case_id, 'void', kinds, count, changes)
        case['stubs'].append({'address': KIND_FREE, 'argc': 4, 'ret': '0x0'})
        out.append(case)
    return out


def kind_allocate_cases(set_count: bool):
    """The allocation (a stub comparing the allocator, the size and the alignment) succeeding, or
    failing (the object left alone, false). SET_COUNT: the count comes in rsi (high bits ignored)
    and the object's count is something else."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for case_id, kinds, count, changes in KIND_SIZE_CASES + [('fails', [6, 6], 7, None), ('fails_kind4', [4], 5, None)]:
            case = kind_case(address, case_id, 'i8', kinds, count + 0x101 if set_count else count, changes)
            if set_count:
                case['args']['rsi'] = hex(0xdeadbeef00000000 | (count & 0xffffffff))
            case['buffers']['storage'] = kind_holder(19, 0x10)
            ret = '0x0' if case_id.startswith('fails') else 'buf:storage'
            case['stubs'].append({'address': KIND_ALLOCATE, 'argc': 3, 'ret': ret})
            out.append(case)
        return out
    return cases


CASES.update({
    'frees_kind_sized_vec4_storage_through_0x05ac8288': kind_free_cases,
    'allocates_kind_sized_dword_storage_through_0x05ac8288': kind_allocate_cases(False),
    'sets_count_and_allocates_kind_sized_dword_storage_through_0x05ac8288': kind_allocate_cases(True),
})

MUTANTS.update({
    'frees_kind_sized_vec4_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_kind_storage_free_reads_allocator_late', 'bb_mutant_kind_storage_free_reads_count_early', 'bb_mutant_kind_storage_free_asks_once', 'bb_mutant_kind_storage_free_unrounded'],
    'allocates_kind_sized_dword_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_kind_storage_allocate_reads_allocator_late', 'bb_mutant_kind_storage_allocate_reads_count_early', 'bb_mutant_kind_storage_allocate_asks_once', 'bb_mutant_kind_storage_allocate_no_entry_offset', 'bb_mutant_kind_storage_allocate_true_on_failure'],
    'sets_count_and_allocates_kind_sized_dword_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_kind_storage_set_count_skipped', 'bb_mutant_kind_storage_set_count_after_kind', 'bb_mutant_kind_storage_allocate_true_on_failure'],
})


# Families from #1173.
def debug_menu_release_cases(kind: str):
    """The cases of tests/debug_menu_release_cases.py's KIND_cases."""
    def build(address: str, value: int | None) -> list[dict]:
        return getattr(_load('tests/debug_menu_release_cases'), f'{kind}_cases')(address)
    return build


CASES.update({
    'removes_debug_menu_entry_keyed_by_object': debug_menu_release_cases('menu_entry'),
    'formats_one_of_three_texts_for_slot_0x80': debug_menu_release_cases('text'),
    'releases_counted_member_at_0x30_then_base': debug_menu_release_cases('counted_release'),
})

MUTANTS.update({
    # Each control mutant below moves one read across the call that changes it
    # (game/engine/tests/debug_menu_release_mutants.c).
    'removes_debug_menu_entry_keyed_by_object': ['bb_mutant_does_nothing', 'bb_mutant_menu_entry_manager_reloaded'],
    'formats_one_of_three_texts_for_slot_0x80': ['bb_mutant_does_nothing'],
    'releases_counted_member_at_0x30_then_base': ['bb_mutant_does_nothing', 'bb_mutant_counted_release_count_not_reloaded'],
})


# Families from #1175.
def _virtual(name: str):
    return getattr(_load('virtual_method_family_cases'), name)


CASES.update({
    'forwards_to_referenced_node_slot_0x28': _virtual('forward_cases'),
    'posts_event_9_when_accepted': _virtual('event_cases'),
    'syncs_selected_id_and_index': _virtual('sync_cases'),
    'selects_entry_id_by_index': _virtual('select_cases'),
    'releases_counted_reference_at_0x20': _virtual('release_cases'),
    'registers_new_entry_at_0x30': _virtual('register_cases'),
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'forwards_to_referenced_node_slot_0x28': ['bb_mutant_does_nothing', 'bb_mutant_node_forward_unsigned_index', 'bb_mutant_node_forward_drops_flag'],
    'posts_event_9_when_accepted': ['bb_mutant_does_nothing', 'bb_mutant_event_flags_read_before_base', 'bb_mutant_event_accepts_whole_register'],
    'syncs_selected_id_and_index': ['bb_mutant_does_nothing', 'bb_mutant_selected_id_last_match', 'bb_mutant_selected_id_not_stored'],
    'selects_entry_id_by_index': ['bb_mutant_does_nothing'],
    'releases_counted_reference_at_0x20': ['bb_mutant_does_nothing', 'bb_mutant_counted_release_no_reload', 'bb_mutant_counted_release_weak_zero', 'bb_mutant_counted_release_keeps_pointer'],
    'registers_new_entry_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_entry_registration_no_reload', 'bb_mutant_entry_registration_skips_null_entry'],
})


# Families from #1176.
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


BLOCK_ALLOCATOR_pr1176 = 0x057e7340


BLOCK_FREE = '0x005004a0'        # the allocator's free slot (+0x68)


BLOCK_OTHER_FREE = '0x005004c0'  # the free slot of the allocator a free may install


BLOCK_BASE = '0x00569a90'


BLOCK_FIELD = 0xa0


def block_case(address: str, case_id: str, block: bool, data: str | None, change: str | None = None) -> dict:
    """DATA: what the block's first field points at ('buf' a buffer, 'owner' the owner, None
    null). CHANGE: what the first free writes ('clear' or 'swap' the block field, 'allocator')."""
    memory = [{'addr': hex(BLOCK_ALLOCATOR_pr1176), 'pointer': 'allocvt'}, {'addr': hex(BLOCK_GLOBAL), 'guest': hex(BLOCK_ALLOCATOR_pr1176)},
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
            first['writes'] = [{'arg': 0, 'offset': BLOCK_GLOBAL - BLOCK_ALLOCATOR_pr1176, 'pointer': 'other_allocator'}]
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


CASES.update({
    'frees_kind_sized_dword_storage_through_0x05ac8288': dword_free_cases,
    'deletes_work_holder': work_holder_cases,
    'creates_0xe40_byte_object_with_0x020e9820': create_cases,
    'frees_block_at_0xa0_then_destroys_base_0x00569a90': block_cases,
    'destroys_owner_of_two_objects': owner_cases(False),
    'deletes_owner_of_two_objects': owner_cases(True),
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'frees_kind_sized_dword_storage_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_dword_storage_free_reads_allocator_late', 'bb_mutant_dword_storage_free_reads_count_early', 'bb_mutant_dword_storage_free_asks_once', 'bb_mutant_dword_storage_free_without_entries'],
    'deletes_work_holder': ['bb_mutant_does_nothing', 'bb_mutant_work_holder_delete_work_once', 'bb_mutant_work_holder_delete_signal_on_one', 'bb_mutant_work_holder_delete_kept'],
    'creates_0xe40_byte_object_with_0x020e9820': ['bb_mutant_does_nothing', 'bb_mutant_create_object_aligned_8', 'bb_mutant_create_object_returns_constructor_result'],
    'frees_block_at_0xa0_then_destroys_base_0x00569a90': ['bb_mutant_does_nothing', 'bb_mutant_block_at_0xa0_once', 'bb_mutant_block_at_0xa0_skips_data'],
    'destroys_owner_of_two_objects': ['bb_mutant_does_nothing', 'bb_mutant_owner_of_two_second_early', 'bb_mutant_owner_of_two_first_cleared_late', 'bb_mutant_owner_of_two_no_final_clear'],
    'deletes_owner_of_two_objects': ['bb_mutant_does_nothing', 'bb_mutant_owner_of_two_delete_second_early', 'bb_mutant_owner_of_two_delete_first_cleared_late', 'bb_mutant_owner_of_two_delete_allocator_early'],
})


# Families from #1177.
def _forwarding(name: str):
    return getattr(_load('owner_block_and_forwarding_family_cases'), name)


CASES.update({
    'fills_owner_block_with_two_addresses': _forwarding('block_cases'),
    'fills_owner_block_with_one_address': _forwarding('block_cases'),
    'updates_and_calls_slot_0x38_unless_refused': _forwarding('update_cases'),
    'adopts_buffer_at_0x10_after_slot_0x78': _forwarding('adopt_cases'),
    'destroys_with_0x0139bf90_then_deletes': _forwarding('delete_cases'),
    'clears_0x28_and_passes_0x18_to_0x027286b0': _forwarding('clear_cases'),
    'passes_float_to_0x01b5f7f0_when_0x010edc70_agrees': _forwarding('float_cases'),
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'fills_owner_block_with_two_addresses': ['bb_mutant_does_nothing', 'bb_mutant_owner_block_one_address', 'bb_mutant_owner_block_keeps_0x70', 'bb_mutant_owner_block_returns_one'],
    'fills_owner_block_with_one_address': ['bb_mutant_does_nothing', 'bb_mutant_owner_block_two_addresses', 'bb_mutant_owner_block_keeps_0x70', 'bb_mutant_owner_block_returns_one'],
    'updates_and_calls_slot_0x38_unless_refused': ['bb_mutant_does_nothing', 'bb_mutant_update_table_read_first', 'bb_mutant_update_stops_without_target'],
    'adopts_buffer_at_0x10_after_slot_0x78': ['bb_mutant_does_nothing', 'bb_mutant_adopt_buffer_read_first', 'bb_mutant_adopt_frees_buffer_itself'],
    'destroys_with_0x0139bf90_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_destroy_without_delete', 'bb_mutant_delete_without_destroy', 'bb_mutant_delete_then_destroy'],
    'clears_0x28_and_passes_0x18_to_0x027286b0': ['bb_mutant_does_nothing', 'bb_mutant_clear_after_passing'],
    'passes_float_to_0x01b5f7f0_when_0x010edc70_agrees': ['bb_mutant_does_nothing'],
})


# Families from #1179.
# Generated fixtures for the families below, not game data. Every called function is a stub that
# compares its arguments; every written byte is compared.
def _pattern(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 31 + i * 11) & 0xff for i in range(size)).hex()}


def _le(value: int, size: int = 4) -> str:
    return (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little').hex()


# destroys_holder_of_shared_work (shared_work_holder_family.cpp). One block holds work A (+0x0),
# the holder (+0x80) and work B (+0xc0), so the signal's set, called with A's signal (+0x10), can
# rewrite the holder's work pointer through its argument.
HOLDER_SIGNAL_SET = '0x0247e780'


HOLDER_FATAL_ERROR = '0x024b55b0'


HOLDER_WORK_DESTROY = '0x00500440'   # what the works' vtable slot 0 points at


HOLDER_AT, HOLDER_OTHER_WORK = 0x80, 0xc0


HOLDER_SIGNAL, HOLDER_USERS, HOLDER_REFERENCES = 0x10, 0x30, 0x8


def holder_case(address: str, case_id: str, users: int | None, references: int,
                change: str | None = None, other_references: int = 1) -> dict:
    """USERS None: the holder has no work. CHANGE: what the signal's set stores in the holder's
    work pointer ('null' or 'other')."""
    memory = [{'addr': f'buf:block+{HOLDER_AT:#x}', 'guest': '0x00500460'}]
    stubs = []
    for base, refs in ((0, references), (HOLDER_OTHER_WORK, other_references)):
        memory.append({'addr': f'buf:block+{base:#x}', 'pointer': 'workvt'})
        memory.append({'addr': f'buf:block+{base + HOLDER_REFERENCES:#x}', 'bytes': _le(refs)})
        memory.append({'addr': f'buf:block+{base + HOLDER_USERS:#x}', 'bytes': _le(users or 0)})
    if users is None:
        memory.append({'addr': f'buf:block+{HOLDER_AT + 8:#x}', 'bytes': _le(0, 8)})
    else:
        memory.append({'addr': f'buf:block+{HOLDER_AT + 8:#x}', 'pointer': 'block'})
        work = 0
        if users <= 1:
            signal = {'address': HOLDER_SIGNAL_SET, 'argc': 2, 'argi': [0], 'argu8': [1], 'ret': '0x5a5a',
                      'argmem': [{'arg': 0, 'offset': HOLDER_USERS - HOLDER_SIGNAL, 'size': 4}]}
            offset = HOLDER_AT + 8 - HOLDER_SIGNAL
            if change == 'null':
                signal['writes'] = [{'arg': 0, 'offset': offset, 'bytes': _le(0, 8)}]
                work = None
            elif change == 'other':
                signal['writes'] = [{'arg': 0, 'offset': offset, 'pointer': f'block+{HOLDER_OTHER_WORK:#x}'}]
                work, references = HOLDER_OTHER_WORK, other_references
            stubs.append(signal)
        if work is not None:
            if references == 1:
                stubs.append({'address': HOLDER_WORK_DESTROY, 'argc': 1})
            elif references <= 0:
                stubs.append({'address': HOLDER_FATAL_ERROR, 'argc': 3})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'args': {'rdi': f'buf:block+{HOLDER_AT:#x}'},
            'buffers': {'block': _pattern(21, 0x100), 'workvt': _pattern(23, 0x10)},
            'memory': memory + [{'addr': 'buf:workvt+0x0', 'guest': HOLDER_WORK_DESTROY}],
            'imports': [], 'stubs': stubs}


# (id, users, references, change, other work's references)
HOLDER_CASES = [
    ('no_work', None, 1, None, 1), ('users2_refs2', 2, 2, None, 1), ('users2_refs1', 2, 1, None, 1),
    ('users1_refs2', 1, 2, None, 1), ('users1_refs1', 1, 1, None, 1), ('users0_refs3', 0, 3, None, 1),
    ('users_negative', -5, 1, None, 1), ('users_smallest', -0x80000000, 2, None, 1),
    ('users_largest', 0x7fffffff, 1, None, 1), ('refs0', 3, 0, None, 1), ('refs_negative', 1, -2, None, 1),
    ('refs_smallest', 2, -0x80000000, None, 1), ('refs_largest', 2, 0x7fffffff, None, 1),
    ('signal_clears_work', 1, 1, 'null', 1), ('signal_swaps_work', 1, 2, 'other', 1),
    ('signal_swaps_work_kept', 0, 1, 'other', 4), ('signal_swaps_work_unref', 1, 1, 'other', 0),
]


def holder_cases(address: str, value: int | None) -> list[dict]:
    return [holder_case(address, *row) for row in HOLDER_CASES]


# destroys_owner_of_wide_string_then_base_0x0139b630 and
# deletes_owner_of_wide_string_after_base_0x0139b630 (wide_string_owner_family.cpp). The string
# object's heap comes from the heap lookup stub; its long text goes to its own allocator. Both
# allocators are buffers whose vtables point slot 0x70 at a stub; the lookup may rewrite the
# string's capacity or allocator through its argument (both are read after it).
STRING_HEAP_OWNING = '0x0247b720'


STRING_BASE_DESTROY = '0x0139b630'


STRING_OPERATOR_DELETE = '0x02fbe588'


STRING_TEXT_FREE = '0x00500400'        # slot 0x70 of the string's allocator


STRING_HEAP_FREE = '0x00500420'        # slot 0x70 of the string object's heap


STRING_OTHER_TEXT_FREE = '0x00500480'  # slot 0x70 of an allocator the lookup may install


STRING_FREE_SLOT = 0x70


STRING_AT, STRING_FLAG = 0x30, 0x44


STRING_SIZE, STRING_CAPACITY, STRING_ALLOCATOR = 0x18, 0x20, 0x28


def string_owner_case(address: str, deleting: bool, case_id: str, flag: int, has_string: bool,
                      capacity: int = 7, change: dict | None = None) -> dict:
    """FLAG: the owned flag byte; CAPACITY: the string's; CHANGE: what the heap lookup writes into
    the string ('capacity' value, 'allocator' True for the other allocator)."""
    change = change or {}
    memory = [{'addr': 'buf:owner+0x0', 'guest': '0x00500460'},
              {'addr': f'buf:owner+{STRING_FLAG:#x}', 'bytes': _le(flag, 1)},
              {'addr': 'buf:string+0x8', 'pointer': 'text'},
              {'addr': f'buf:string+{STRING_SIZE:#x}', 'bytes': _le(5, 8)},
              {'addr': f'buf:string+{STRING_CAPACITY:#x}', 'bytes': _le(capacity, 8)},
              {'addr': f'buf:string+{STRING_ALLOCATOR:#x}', 'pointer': 'allocator'},
              {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_vt'},
              {'addr': f'buf:allocator_vt+{STRING_FREE_SLOT:#x}', 'guest': STRING_TEXT_FREE},
              {'addr': 'buf:other_allocator+0x0', 'pointer': 'other_allocator_vt'},
              {'addr': f'buf:other_allocator_vt+{STRING_FREE_SLOT:#x}', 'guest': STRING_OTHER_TEXT_FREE},
              {'addr': 'buf:heap+0x0', 'pointer': 'heap_vt'},
              {'addr': f'buf:heap_vt+{STRING_FREE_SLOT:#x}', 'guest': STRING_HEAP_FREE}]
    memory.append({'addr': f'buf:owner+{STRING_AT:#x}', 'pointer': 'string'} if has_string
                  else {'addr': f'buf:owner+{STRING_AT:#x}', 'bytes': _le(0, 8)})
    stubs = []
    if flag & 0xff and has_string:
        lookup = {'address': STRING_HEAP_OWNING, 'argc': 1, 'ret': 'buf:heap'}
        writes = []
        if 'capacity' in change:
            writes.append({'arg': 0, 'offset': STRING_CAPACITY, 'bytes': _le(change['capacity'], 8)})
            capacity = change['capacity']
        if change.get('allocator'):
            writes.append({'arg': 0, 'offset': STRING_ALLOCATOR, 'pointer': 'other_allocator'})
        if writes:
            lookup['writes'] = writes
        stubs.append(lookup)
        if capacity >= 8:
            stubs.append({'address': STRING_OTHER_TEXT_FREE if change.get('allocator') else STRING_TEXT_FREE,
                          'argc': 2})
        stubs.append({'address': STRING_HEAP_FREE, 'argc': 2,
                      'argmem': [{'arg': 1, 'offset': 0x8, 'size': 0x20}]})
    stubs.append({'address': STRING_BASE_DESTROY, 'argc': 1, 'argmem': [{'arg': 0, 'offset': 0, 'size': 0x48}]})
    if deleting:
        stubs.append({'address': STRING_OPERATOR_DELETE, 'argc': 1})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'args': {'rdi': 'buf:owner'},
            'buffers': {'owner': _pattern(31, 0x60), 'string': _pattern(33, 0x40), 'text': _pattern(35, 0x40),
                        'allocator': _pattern(37, 0x10), 'allocator_vt': _pattern(39, 0x80),
                        'other_allocator': _pattern(41, 0x10), 'other_allocator_vt': _pattern(43, 0x80),
                        'heap': _pattern(45, 0x10), 'heap_vt': _pattern(47, 0x80)},
            'memory': memory, 'imports': [], 'stubs': stubs}


# (id, flag, has string, capacity, change)
STRING_OWNER_CASES = [
    ('not_owned', 0, True, 0x20, None), ('not_owned_no_string', 0, False, 0x20, None),
    ('owned_no_string', 1, False, 0x20, None), ('owned_inline', 1, True, 7, None),
    ('owned_empty_capacity', 1, True, 0, None), ('owned_long_smallest', 1, True, 8, None),
    ('owned_long', 1, True, 0x40, None), ('owned_capacity_largest', 1, True, (1 << 64) - 1, None),
    ('owned_capacity_high_bit', 1, True, 1 << 63, None), ('owned_flag_ff', 0xff, True, 0x10, None),
    ('owned_flag_80', 0x80, True, 7, None),
    ('lookup_grows_capacity', 1, True, 7, {'capacity': 8}),
    ('lookup_shrinks_capacity', 1, True, 0x10, {'capacity': 7}),
    ('lookup_swaps_allocator', 1, True, 0x10, {'allocator': True}),
    ('lookup_swaps_allocator_inline', 1, True, 7, {'allocator': True}),
]


def string_owner_cases(deleting: bool):
    def build(address: str, value: int | None) -> list[dict]:
        return [string_owner_case(address, deleting, *row) for row in STRING_OWNER_CASES]
    return build


# visits_children_of_list_at_0x40 and secondary_base_visits_children_of_list_at_0x40
# (child_list_visit_family.cpp). One block holds the object (+0x0), two lists (+0x100, +0x200)
# and two child arrays (+0x300, +0x380), so the object's slot 0x28 call, which gets the object,
# can swap the list or its array, change the count or a later child through its argument.
VISIT_PASS = '0x00b69e20'


VISIT_SLOT = '0x00500400'   # what the object's vtable slot 0x28 points at


VISIT_LIST, VISIT_OTHER_LIST = 0x100, 0x200


VISIT_ARRAY, VISIT_OTHER_ARRAY = 0x300, 0x380


VISIT_LIST_FIELD, VISIT_CHILDREN, VISIT_COUNT = 0x40, 0xa0, 0xa8


VISIT_ARRAY_SLOTS = 8


VISIT_SECONDARY = 0x48


def child_pointer(array: int, index: int) -> str:
    return f'kids+{(array // 0x80 - 6) * 0x100 + index * 0x20:#x}'


def visit_case(address: str, secondary: bool, case_id: str, count: int | None,
               change: dict | None = None) -> dict:
    """COUNT None: no list. CHANGE: what the first slot call writes ('list' to the other list,
    'array' to the other array, 'count' a new count, 'child' a new third child)."""
    change = change or {}
    memory = [{'addr': 'buf:block+0x0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+0x28', 'guest': VISIT_SLOT}]
    if count is None:
        memory.append({'addr': f'buf:block+{VISIT_LIST_FIELD:#x}', 'bytes': _le(0, 8)})
    else:
        memory.append({'addr': f'buf:block+{VISIT_LIST_FIELD:#x}', 'pointer': f'block+{VISIT_LIST:#x}'})
    for at, array, n in ((VISIT_LIST, VISIT_ARRAY, count or 0), (VISIT_OTHER_LIST, VISIT_OTHER_ARRAY, 2)):
        memory.append({'addr': f'buf:block+{at + VISIT_CHILDREN:#x}', 'pointer': f'block+{array:#x}'})
        memory.append({'addr': f'buf:block+{at + VISIT_COUNT:#x}', 'bytes': _le(n)})
    for array in (VISIT_ARRAY, VISIT_OTHER_ARRAY):
        for i in range(VISIT_ARRAY_SLOTS):
            memory.append({'addr': f'buf:block+{array + 8 * i:#x}', 'pointer': child_pointer(array, i)})
    writes = []
    if change.get('list'):
        writes.append({'arg': 0, 'offset': VISIT_LIST_FIELD, 'pointer': f'block+{VISIT_OTHER_LIST:#x}'})
    if change.get('array'):
        writes.append({'arg': 0, 'offset': VISIT_LIST + VISIT_CHILDREN, 'pointer': f'block+{VISIT_OTHER_ARRAY:#x}'})
    if 'count' in change:
        writes.append({'arg': 0, 'offset': VISIT_LIST + VISIT_COUNT, 'bytes': _le(change['count'])})
    if change.get('child'):
        writes.append({'arg': 0, 'offset': VISIT_ARRAY + 16, 'pointer': 'kids+0xf0'})
    stubs = []
    for i in range(max(count or 0, 0)):
        stubs.append({'address': VISIT_PASS, 'argc': 2})
        slot = {'address': VISIT_SLOT, 'argc': 2}
        if i == 0 and writes:
            slot['writes'] = writes
        stubs.append(slot)
    rdi = f'buf:block+{VISIT_SECONDARY:#x}' if secondary else 'buf:block'
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void', 'args': {'rdi': rdi},
            'buffers': {'block': _pattern(51, 0x400), 'vtable': _pattern(53, 0x40), 'kids': _pattern(55, 0x200)},
            'memory': memory, 'imports': [], 'stubs': stubs}


# (id, count, change)
VISIT_CASES = [
    ('no_list', None, None), ('empty', 0, None), ('negative', -1, None), ('smallest', -0x80000000, None),
    ('one', 1, None), ('two', 2, None), ('five', 5, None), ('eight', 8, None),
    ('list_swapped', 4, {'list': True}), ('array_swapped', 3, {'array': True}),
    ('count_shrinks', 4, {'count': 1}), ('count_grows', 2, {'count': 6}), ('count_cleared', 3, {'count': 0}),
    ('child_changed', 4, {'child': True}), ('list_swapped_one', 1, {'list': True}),
]


def visit_cases(secondary: bool):
    def build(address: str, value: int | None) -> list[dict]:
        return [visit_case(address, secondary, *row) for row in VISIT_CASES]
    return build


# selects_index_of_changed_id_with_0x01348fe0 (changed_id_index_family.cpp): an object following an
# id, its owner (+0x18) with the target at +0x110, and a table of 24-byte entries (id at +0x10).
SELECT_APPLY = '0x01348fe0'


SELECT_ENTRY, SELECT_ENTRY_ID = 0x18, 0x10


def select_case(address: str, case_id: str, followed: int | None, seen: int, ids: list[int]) -> dict:
    """FOLLOWED None: no followed id. IDS: the entries' ids."""
    memory = [{'addr': 'buf:follower+0x18', 'pointer': 'owner'},
              {'addr': 'buf:follower+0x20', 'bytes': _le(seen)},
              {'addr': 'buf:follower+0x30', 'pointer': 'table'},
              {'addr': 'buf:owner+0x110', 'pointer': 'target'},
              {'addr': 'buf:table+0x18', 'pointer': 'entries'},
              {'addr': 'buf:table+0x20', 'pointer': f'entries+{SELECT_ENTRY * len(ids):#x}'}]
    memory += [{'addr': f'buf:entries+{SELECT_ENTRY * i + SELECT_ENTRY_ID:#x}', 'bytes': _le(v)}
               for i, v in enumerate(ids)]
    stubs = []
    if followed is None:
        memory.append({'addr': 'buf:follower+0x28', 'bytes': _le(0, 8)})
    else:
        memory.append({'addr': 'buf:follower+0x28', 'pointer': 'id'})
        memory.append({'addr': 'buf:id+0x0', 'bytes': _le(followed)})
        # Scripted on an unchanged id too, so a call made there is recorded.
        stubs.append({'address': SELECT_APPLY, 'argc': 3, 'argi': [0], 'argu32': [1, 2], 'ret': '0x77'})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void', 'args': {'rdi': 'buf:follower'},
            'buffers': {'follower': _pattern(61, 0x40), 'owner': _pattern(63, 0x120), 'target': _pattern(65, 0x10),
                        'table': _pattern(67, 0x30), 'entries': _pattern(69, SELECT_ENTRY * 8 + 8),
                        'id': _pattern(71, 0x8)},
            'memory': memory, 'imports': [], 'stubs': stubs}


# (id, followed id, seen id, entry ids)
SELECT_CASES = [
    ('no_id', None, 3, [1, 2, 3]), ('unchanged', 5, 5, [1, 5, 9]), ('unchanged_negative', -1, -1, [-1]),
    ('first', 7, 2, [7, 8, 9]), ('middle', 8, 2, [7, 8, 9]), ('last', 9, 2, [7, 8, 9]),
    ('absent', 4, 2, [7, 8, 9]), ('empty_table', 4, 2, []), ('one_entry', 4, 2, [4]),
    ('duplicates', 6, 1, [3, 6, 6, 6]), ('seen_in_table', 6, 3, [3, 6]), ('negative_id', -2, 0, [0, -2, 5]),
    ('smallest_id', -0x80000000, 0x7fffffff, [0x7fffffff, -0x80000000]),
    ('eight_entries_last', 0x101, 0, [1, 2, 3, 4, 5, 6, 7, 0x101]), ('zero_id', 0, 1, [1, 0]),
]


def select_cases(address: str, value: int | None) -> list[dict]:
    return [select_case(address, *row) for row in SELECT_CASES]


CASES.update({
    'destroys_holder_of_shared_work': holder_cases,
    'destroys_owner_of_wide_string_then_base_0x0139b630': string_owner_cases(False),
    'deletes_owner_of_wide_string_after_base_0x0139b630': string_owner_cases(True),
    'visits_children_of_list_at_0x40': visit_cases(False),
    'secondary_base_visits_children_of_list_at_0x40': visit_cases(True),
    'selects_index_of_changed_id_with_0x01348fe0': select_cases,
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'destroys_holder_of_shared_work': ['bb_mutant_does_nothing', 'bb_mutant_holder_of_shared_work_work_once', 'bb_mutant_holder_of_shared_work_signal_on_one', 'bb_mutant_holder_of_shared_work_keeps_work'],
    'destroys_owner_of_wide_string_then_base_0x0139b630': ['bb_mutant_does_nothing', 'bb_mutant_string_owner_capacity_early', 'bb_mutant_string_owner_keeps_pointer', 'bb_mutant_string_owner_no_reset'],
    'deletes_owner_of_wide_string_after_base_0x0139b630': ['bb_mutant_does_nothing', 'bb_mutant_string_owner_delete_capacity_early', 'bb_mutant_string_owner_delete_first', 'bb_mutant_string_owner_delete_kept'],
    'visits_children_of_list_at_0x40': ['bb_mutant_does_nothing', 'bb_mutant_visit_children_reloads_count', 'bb_mutant_visit_children_caches_list', 'bb_mutant_visit_children_caches_array', 'bb_mutant_visit_children_slot_first'],
    'secondary_base_visits_children_of_list_at_0x40': ['bb_mutant_does_nothing', 'bb_mutant_secondary_visit_children_reloads_count', 'bb_mutant_secondary_visit_children_caches_list', 'bb_mutant_secondary_visit_children_slot_first'],
    'selects_index_of_changed_id_with_0x01348fe0': ['bb_mutant_does_nothing', 'bb_mutant_select_changed_id_keeps_seen', 'bb_mutant_select_changed_id_finds_last', 'bb_mutant_select_changed_id_absent_as_zero', 'bb_mutant_select_changed_id_unchanged_too'],
})


# Families from #1180.
def _block_init(name: str):
    return getattr(_load('block_init_and_owned_release_family_cases'), name)


CASES.update({
    'initializes_owner_block_six_pointers_to_0x05ac5cac': _block_init('block_cases'),
    'initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc': _block_init('block_cases'),
    'initializes_owner_block_first_pointer_to_0x05ac74a3': _block_init('block_cases'),
    'destroys_pair_owner_with_vtable_0x0570fb90': _block_init('pair_cases'),
    'destroys_entry_storage_with_vtable_0x057045e0': _block_init('storage_cases'),
    'releases_counted_object_at_0x10': _block_init('counted_cases'),
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'initializes_owner_block_six_pointers_to_0x05ac5cac': ['bb_mutant_does_nothing', 'bb_mutant_init_block_five_pointers', 'bb_mutant_init_block_returns_one'],
    'initializes_owner_block_pointers_to_0x05acd45c_and_0x05acd1bc': ['bb_mutant_does_nothing', 'bb_mutant_init_block_pair_swapped', 'bb_mutant_init_block_returns_one'],
    'initializes_owner_block_first_pointer_to_0x05ac74a3': ['bb_mutant_does_nothing', 'bb_mutant_init_block_one_keeps_0x70', 'bb_mutant_init_block_returns_one'],
    'destroys_pair_owner_with_vtable_0x0570fb90': ['bb_mutant_does_nothing', 'bb_mutant_pair_owner_second_early', 'bb_mutant_pair_owner_first_cleared_late', 'bb_mutant_pair_owner_no_final_clear'],
    'destroys_entry_storage_with_vtable_0x057045e0': ['bb_mutant_does_nothing', 'bb_mutant_entry_storage_skips_zero_count', 'bb_mutant_entry_storage_wide_bytes', 'bb_mutant_entry_storage_clears_0x20_late', 'bb_mutant_entry_storage_keeps_count'],
    'releases_counted_object_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_counted_0x10_field_once', 'bb_mutant_counted_0x10_counts_once', 'bb_mutant_counted_0x10_weak_zero', 'bb_mutant_counted_0x10_keeps_pointer'],
})


# Families from #1181.
def _forward(name: str):
    return getattr(_load('allocator_forward_and_equality_family_cases'), name)


CASES.update({
    'frees_8_byte_block_through_0x05ac8288': _forward('free_8_cases'),
    'frees_sized_block_aligned_8_through_0x05ac8288': _forward('free_sized_cases'),
    'allocates_aligned_8_through_0x05ac8288': _forward('allocate_8_cases'),
    'allocates_with_alignment_through_0x05ac8288': _forward('allocate_cases'),
    'resets_with_0x02c375e0_and_returns_object': _forward('reset_cases'),
    'releases_value_at_0x8_with_0x005c14d0_then_clears_kind': _forward('release_cases'),
    'equals_by_kind_and_16_bytes_at_0x10': _forward('equal_cases'),
})

MUTANTS.update({
    # Doing nothing makes none of the writes or calls; each other mutant says in
    # tests/family_mutants.c how it differs from the family it is listed for.
    'frees_8_byte_block_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_free_8_bytes_aligned_16', 'bb_mutant_free_8_bytes_size_16', 'bb_mutant_free_8_bytes_global_address'],
    'frees_sized_block_aligned_8_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_free_sized_aligned_16', 'bb_mutant_free_sized_fixed_size_8', 'bb_mutant_free_sized_swapped', 'bb_mutant_free_8_bytes_global_address'],
    'allocates_aligned_8_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_allocate_8_aligned_16', 'bb_mutant_allocate_8_result_dropped', 'bb_mutant_allocate_8_global_address'],
    'allocates_with_alignment_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_allocate_fixed_alignment_8', 'bb_mutant_allocate_swapped', 'bb_mutant_allocate_result_dropped'],
    'resets_with_0x02c375e0_and_returns_object': ['bb_mutant_does_nothing', 'bb_mutant_reset_returns_callee_result', 'bb_mutant_reset_skipped'],
    'releases_value_at_0x8_with_0x005c14d0_then_clears_kind': ['bb_mutant_does_nothing', 'bb_mutant_release_value_read_again', 'bb_mutant_release_clears_first', 'bb_mutant_release_keeps_kind'],
    'equals_by_kind_and_16_bytes_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_equal_whole_kind', 'bb_mutant_equal_keys_read_first', 'bb_mutant_equal_whole_answer', 'bb_mutant_equal_short_key'],
})


# Families from #1160.
THUNK_OPERATOR_DELETE = '0x02fbe588'


THUNK_HEAP_OWNING = '0x0247b720'


THUNK_FLAG_CALL = '0x022dc040'


THUNK_FREE_SIZED = '0x02fa1c20'


THUNK_BLOCK_ALLOCATOR = '0x05ac8288'   # the global that points at the block allocator


# Stand-ins for the virtual functions fake tables point at, each stubbed.
THUNK_DESTROY, THUNK_HEAP_FREE = '0x00400240', '0x00400250'


THUNK_RELEASE_BASE, THUNK_RELEASE_DERIVED = '0x00400260', '0x00400270'


def thunk_filled(seed: int, size: int) -> dict:
    return {'size': size, 'bytes': bytes((seed * 29 + i * 7) & 0xff for i in range(size)).hex()}


def thunk_case(address: str, case_id: str, returns: str, args: dict, buffers: dict) -> dict:
    regs = {'rdi': '0x1111111111111111', 'rsi': '0x2222222222222222', 'rdx': '0x3333333333333333',
            'rcx': '0x4444444444444444', 'rax': '0x5aa5c33c0ff0e11e'}
    regs.update(args)
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': returns, 'buffers': buffers,
            'memory': [], 'imports': [], 'stubs': [], 'args': regs}


def owning_heap_cases(address: str, value: int | None) -> list[dict]:
    """The second argument set (two seeds): the owning heap (a stub comparing the object), the
    destructor (the table's first slot, a stub writing into the object so its order shows) and
    the heap's free (slot 0x70, a stub comparing the heap and the object); or null: nothing. The
    first argument is not used."""
    out = []
    for seed in (3, 5):
        case = thunk_case(address, f'object_{seed}', 'void', {'rdi': f'0x{seed:x}0f0f0f0f0f0', 'rsi': 'buf:object'},
                          {'object': thunk_filled(seed, 0x20), 'object_table': {'size': 0x10},
                           'heap': thunk_filled(seed + 1, 0x10), 'heap_table': {'size': 0x78}})
        case['memory'] += [{'addr': 'buf:object+0x0', 'pointer': 'object_table'},
                           {'addr': 'buf:object_table+0x0', 'guest': THUNK_DESTROY},
                           {'addr': 'buf:heap+0x0', 'pointer': 'heap_table'},
                           {'addr': 'buf:heap_table+0x70', 'guest': THUNK_HEAP_FREE}]
        case['stubs'] += [{'address': THUNK_HEAP_OWNING, 'argc': 1, 'argi': [0], 'ret': 'buf:heap'},
                          {'address': THUNK_DESTROY, 'argc': 1, 'argi': [0],
                           'writes': [{'arg': 0, 'offset': 8, 'bytes': f'{0xd0 + seed:02x}' * 8}]},
                          {'address': THUNK_HEAP_FREE, 'argc': 2, 'argi': [0, 1],
                           'argmem': [{'arg': 1, 'offset': 8, 'size': 8}], 'ret': seed}]
        out.append(case)
    out.append(thunk_case(address, 'null', 'void', {'rsi': '0x0'}, {}))
    return out


def counted_holder_cases(derived: bool, deletes: bool):
    """The members at +0x8 and (DERIVED) +0x10 set or null; each set one is released through its
    own table's slot 0x18 (a stub comparing it), the derived member first; DELETES: operator
    delete (a stub comparing the holder and its table and members then) comes last."""
    combos = ((True, True), (True, False), (False, True), (False, False)) if derived else ((True, None), (False, None))

    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for n, (base, member) in enumerate(combos):
            seed = 3 + 2 * n
            case_id = f"base_{'set' if base else 'null'}" + ('' if member is None else f"_derived_{'set' if member else 'null'}")
            buffers = {'holder': thunk_filled(seed, 0x20)}
            case = thunk_case(address, case_id, 'void', {'rdi': 'buf:holder'}, buffers)
            for name, offset, present, stand_in in (('derived', 0x10, member, THUNK_RELEASE_DERIVED),
                                                    ('base', 0x8, base, THUNK_RELEASE_BASE)):
                if present is None:
                    continue
                if not present:
                    case['memory'].append({'addr': f'buf:holder+0x{offset:x}', 'bytes': '00' * 8})
                    continue
                buffers[name] = thunk_filled(seed + offset, 0x10)
                buffers[f'{name}_table'] = {'size': 0x20}
                case['memory'] += [{'addr': f'buf:holder+0x{offset:x}', 'pointer': name},
                                   {'addr': f'buf:{name}+0x0', 'pointer': f'{name}_table'},
                                   {'addr': f'buf:{name}_table+0x18', 'guest': stand_in}]
                case['stubs'].append({'address': stand_in, 'argc': 1, 'argi': [0], 'ret': seed << 8 | offset})
            if deletes:
                case['stubs'].append({'address': THUNK_OPERATOR_DELETE, 'argc': 1, 'argi': [0],
                                      'argmem': [{'arg': 0, 'size': 0x18}], 'ret': 0})
            out.append(case)
        return out
    return cases


def string_owner_cases_pr1160(address: str, value: int | None) -> list[dict]:
    """Capacities below, at and above 8 (an unsigned compare: 2^63 and ~0 are above): from 8 the
    buffer at +0x10 is freed through the allocator at +0x30 (slot 0x70, a stub comparing both);
    then operator delete (a stub comparing the object)."""
    out = []
    for case_id, capacity, seed in (('small_7', 7, 3), ('small_0', 0, 5), ('heap_8', 8, 7), ('heap_0x40', 0x40, 9),
                                    ('heap_top_bit', 1 << 63, 11), ('heap_all_bits', (1 << 64) - 1, 13)):
        case = thunk_case(address, case_id, 'void', {'rdi': 'buf:object'},
                          {'object': thunk_filled(seed, 0x40), 'buffer': thunk_filled(seed + 1, 0x20),
                           'allocator': thunk_filled(seed + 2, 0x10), 'allocator_table': {'size': 0x78}})
        case['memory'] += [{'addr': 'buf:object+0x10', 'pointer': 'buffer'},
                           {'addr': 'buf:object+0x28', 'bytes': capacity.to_bytes(8, 'little').hex()},
                           {'addr': 'buf:object+0x30', 'pointer': 'allocator'},
                           {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                           {'addr': 'buf:allocator_table+0x70', 'guest': THUNK_HEAP_FREE}]
        if capacity >= 8:
            case['stubs'].append({'address': THUNK_HEAP_FREE, 'argc': 2, 'argi': [0, 1], 'ret': seed})
        case['stubs'].append({'address': THUNK_OPERATOR_DELETE, 'argc': 1, 'argi': [0], 'ret': 0})
        out.append(case)
    return out


def setter_cases(address: str, value: int | None) -> list[dict]:
    """The dword at +0x10 becomes -2 (the setter the original calls is not stubbed) and the result
    is 0; the bytes around it are kept."""
    out = []
    for seed in (3, 5, 7):
        out.append(thunk_case(address, f'object_{seed}', 'i32', {'rdi': 'buf:object', 'rsi': f'0x{seed:x}{seed:x}'},
                              {'object': thunk_filled(seed, 0x20)}))
    return out


def flag_cases(address: str, value: int | None) -> list[dict]:
    """The call (a stub comparing the object and the byte at +0x60 when it is made, so the flag is
    set after it), then the byte at +0x60 is 1; the flag starts clear, other or already 1."""
    out = []
    for case_id, flag, seed in (('clear', 0, 3), ('other', 0xfe, 5), ('set', 1, 7)):
        case = thunk_case(address, case_id, 'void', {'rdi': 'buf:object'}, {'object': thunk_filled(seed, 0x70)})
        case['memory'].append({'addr': 'buf:object+0x60', 'bytes': f'{flag:02x}'})
        case['stubs'].append({'address': THUNK_FLAG_CALL, 'argc': 1, 'argi': [0],
                              'argmem': [{'arg': 0, 'offset': 0x60, 'size': 1}], 'ret': seed})
        out.append(case)
    return out


def sized_free_cases_pr1160(address: str, value: int | None) -> list[dict]:
    """The free (a stub comparing the allocator the global points at, the block, the size and the
    alignment) returns the result."""
    out = []
    for seed in (3, 5):
        case = thunk_case(address, f'block_{seed}', 'i64', {'rdi': 'buf:block'},
                          {'block': thunk_filled(seed, 0x10), 'allocator': thunk_filled(seed + 1, 0x20)})
        case['memory'].append({'addr': THUNK_BLOCK_ALLOCATOR, 'pointer': 'allocator'})
        case['stubs'].append({'address': THUNK_FREE_SIZED, 'argc': 4, 'ret': seed << 40 | 0xf3ee})
        out.append(case)
    return out


CASES.update({
    'initialize_static_wide_string': static_wide_string_cases,
    'destroys_second_argument_then_frees_it_to_owning_heap': owning_heap_cases,
    'stores_vtable_0x05541c80_releases_members_0x10_and_0x8': counted_holder_cases(True, False),
    'stores_vtable_0x05541c80_releases_members_then_deletes': counted_holder_cases(True, True),
    'stores_vtable_0x05541b90_releases_member_at_8_then_deletes': counted_holder_cases(False, True),
    'frees_string_at_0x10_through_allocator_at_0x30_then_deletes': string_owner_cases_pr1160,
    'stores_minus_2_at_0x10_through_setter_then_returns_zero': setter_cases,
    'calls_0x022dc040_then_sets_byte_0x60': flag_cases,
    'frees_0x10_bytes_through_0x05ac8288': sized_free_cases_pr1160,
})

MUTANTS.update({
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    'destroys_second_argument_then_frees_it_to_owning_heap': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_0x05541c80_releases_members_0x10_and_0x8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_0x05541c80_releases_members_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_vtable_0x05541b90_releases_member_at_8_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'frees_string_at_0x10_through_allocator_at_0x30_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_minus_2_at_0x10_through_setter_then_returns_zero': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_0x022dc040_then_sets_byte_0x60': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'frees_0x10_bytes_through_0x05ac8288': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
})


# allocates_page_into_table_from_0x02795780 (arena_page_table.h), from #1158.
CASES['allocates_page_into_table_from_0x02795780'] = _load('arena_page_table_cases').cases
# No call (and no write), and the neighbouring value's replacement (verify adds it).
MUTANTS['allocates_page_into_table_from_0x02795780'] = ['bb_mutant_does_nothing', 'bb_mutant_returns_zero']
