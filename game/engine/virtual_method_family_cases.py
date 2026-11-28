#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/virtual_method_families.cpp's families, which family_cases.py runs on
every member at its own address. Every callee is a scripted stub comparing its arguments; vtables
are buffers whose slots point at fake addresses that the cases stub. Generated fixtures, not game
data.

forwards_to_referenced_node_slot_0x28: a reference (kind at +0x8, index at +0xa, the other bytes
filled) resolved in a context by a stub that returns a node, whose slot 0x28 stub returns the
result. Kinds 0, 1, 2 and with the top bit set; indices 0, positive, -1, -2 and the 16-bit limits
(the index is sign-extended, the kind zero-extended); flags 0, 1, 0x80 and 0xff under other bits
in the register (only the byte is passed on); results null, a buffer and a large value.

posts_event_9_when_accepted: a handler (vtable slot 0x78 asks whether it accepts the event) and an
event whose dword at +0x18 holds the flags. The base handler is always called first; flags with
either bit of 0x90000000 stop there, other bits do not; the answer is tested as a byte (0x100 is
no); event 9 is posted with the source, 0 and the event (the post is scripted in every case,
so one the member does not make is seen). Two cases whose base handler rewrites the
flag byte show that it is read after the base handler (with a control mutant).

syncs_selected_id_and_index and selects_entry_id_by_index: a selector (flags byte +0xc, id +0x20,
the id slot +0x28, the list +0x30 whose begin and end at +0x18 and +0x20 bound 24-byte entries
with an id at +0x10). One more entry lies past the end. The sync: no slot, the same id, an id
found first, inside, last, twice (the first is taken) and absent, an empty list and one with null
bounds. The selection: the lock bit set (other flag bits do not lock), indices inside, at and past
the count, with other bits in the register, and an empty list.

releases_counted_reference_at_0x20: a holder whose +0x20 holds a counted object (vtable slots 0x18
destroy and 0x20 free; the counts dword at +0x8: strong count in bits 0-14, weak count in bits
15-29, bit 30 alive) or null. Both counts drop and wrap from 0; a strong count reaching 0 with the
alive bit destroys and clears the bit; a weak count reaching 0 frees, with the weak bits set. The
stubs compare the counts as they are at each call; two cases whose destroy rewrites the counts show
they are read again after it (with a control mutant).

registers_new_entry_at_0x30: the owner stored at +0x28, a 32-byte entry from the function the
global 0x056c45c8 points at (a stub; null as well), constructed when there is one, stored at +0x30,
given the value, then registered with the owner. Two cases pass the object itself as the value, so
the value stub can rewrite +0x28 and +0x30: they are read again after it (with a control mutant).
"""
from __future__ import annotations

import struct

MASK = 0xffffffffffffffff


def _hex(value: int) -> str:
    return hex(value & MASK)


def _u32(value: int) -> str:
    return struct.pack('<I', value & 0xffffffff).hex()


def _case(address: str, cid: str, returns: str, args: dict, buffers: dict, memory: list, stubs: list) -> dict:
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def _filled(size: int, fill: str, at: dict[int, bytes]) -> str:
    data = bytearray(bytes.fromhex(fill) * size)
    for offset, value in at.items():
        data[offset:offset + len(value)] = value
    return data.hex()


# forwards_to_referenced_node_slot_0x28
RESOLVE_NODE = '0x02c4f560'
FAKE_NODE_METHOD = '0x00500400'
REFERENCE_KIND, REFERENCE_INDEX = 0x8, 0xa
NODE_METHOD_SLOT = 0x28


def _forward_case(address: str, cid: str, kind: int, index: int, argument: str, flag: int, result: str,
                  fill: str = '5c') -> dict:
    buffers = {'reference': {'size': 0x10, 'bytes': _filled(0x10, fill, {
                   REFERENCE_KIND: struct.pack('<H', kind), REFERENCE_INDEX: struct.pack('<h', index)})},
               'context': {'size': 0x20, 'bytes': 'a7' * 0x20},
               'node': {'size': 0x10, 'bytes': '00' * 0x10},
               'node_vtable': {'size': 0x30, 'bytes': '00' * 0x30},
               'result': {'size': 0x10, 'bytes': '11' * 0x10}}
    memory = [{'addr': 'buf:node+0x0', 'pointer': 'node_vtable'},
              {'addr': f'buf:node_vtable+{NODE_METHOD_SLOT:#x}', 'guest': FAKE_NODE_METHOD}]
    # The kind and index are 16-bit arguments widened to 32 bits; the flag is a byte.
    stubs = [{'address': RESOLVE_NODE, 'argc': 3, 'argi': [0], 'argu32': [1, 2], 'ret': 'buf:node'},
             {'address': FAKE_NODE_METHOD, 'argc': 4, 'argi': [0, 1, 2], 'argu8': [3], 'ret': result}]
    args = {'rdi': 'buf:reference', 'rsi': 'buf:context', 'rdx': argument, 'rcx': _hex(flag)}
    return _case(address, cid, 'i64', args, buffers, memory, stubs)


def forward_cases(address: str, value: int | None) -> list[dict]:
    return [_forward_case(address, 'kind_1_index_0', 1, 0, '0x0', 0, 'buf:result'),
            _forward_case(address, 'kind_2_index_5_flag_1', 2, 5, '0xdeadbeefcafef00d', 1, '0x0'),
            _forward_case(address, 'index_minus_1', 0, -1, 'buf:context', 0xff, '0xffffffff00000001'),
            _forward_case(address, 'index_minus_2', 1, -2, 'buf:result', 0x7766554433221101, 'buf:result', 'ff'),
            _forward_case(address, 'index_lowest', 0xffff, -0x8000, '0x1', 0x80, 'buf:result'),
            _forward_case(address, 'index_highest', 0x8001, 0x7fff, '0x8000000000000000',
                          0xfedcba9876543200, 'buf:node', '00'),
            _forward_case(address, 'kind_top_bit_flag_bits_above', 0x8000, 0x1234, '0x0',
                          0xffffffffffffff01, '0x7fffffffffffffff'),
            _forward_case(address, 'index_0x8000_unsigned_kind', 0xfffe, -0x7fff, '0x2', 0x100, 'buf:result')]


# posts_event_9_when_accepted
HANDLE_EVENT_BASE = '0x0139c240'
POST_HANDLER_EVENT = '0x0139bc30'
FAKE_ACCEPTS = '0x00500420'
ACCEPTS_SLOT = 0x78
EVENT_FLAGS = 0x18
EVENT_FLAG_HIGH_BYTE = 0x1b
STOP_BITS = 0x90000000


def _event_case(address: str, cid: str, flags: int, accepts: int | None, base_writes_byte: int | None = None,
                source: str = 'buf:source') -> dict:
    buffers = {'handler': {'size': 0x10, 'bytes': '00' * 0x10},
               'handler_vtable': {'size': 0x80, 'bytes': '00' * 0x80},
               'event': {'size': 0x20, 'bytes': _filled(0x20, '3e', {EVENT_FLAGS: struct.pack('<I', flags)})},
               'source': {'size': 0x10, 'bytes': '71' * 0x10}}
    memory = [{'addr': 'buf:handler+0x0', 'pointer': 'handler_vtable'},
              {'addr': f'buf:handler_vtable+{ACCEPTS_SLOT:#x}', 'guest': FAKE_ACCEPTS}]
    base = {'address': HANDLE_EVENT_BASE, 'argc': 3, 'argi': [0, 1, 2]}
    if base_writes_byte is not None:
        base['writes'] = [{'arg': 2, 'offset': EVENT_FLAG_HIGH_BYTE, 'bytes': f'{base_writes_byte:02x}'}]
        flags = (flags & 0x00ffffff) | (base_writes_byte << 24)
    stubs = [base]
    if not flags & STOP_BITS:
        stubs.append({'address': FAKE_ACCEPTS, 'argc': 2, 'argi': [0, 1], 'ret': _hex(accepts)})
    # The post is always scripted, so a post the member does not make is a recorded call that
    # differs (an unscripted call would reach the real function). The kind is a 32-bit argument;
    # the fourth is a full qword (0).
    stubs.append({'address': POST_HANDLER_EVENT, 'argc': 5, 'argi': [0, 2, 3, 4], 'argu32': [1]})
    args = {'rdi': 'buf:handler', 'rsi': source, 'rdx': 'buf:event'}
    return _case(address, cid, 'void', args, buffers, memory, stubs)


def event_cases(address: str, value: int | None) -> list[dict]:
    return [_event_case(address, 'clear_accepted', 0, 1),
            _event_case(address, 'clear_refused', 0, 0),
            _event_case(address, 'answer_0x100_is_no', 0, 0x100),
            _event_case(address, 'answer_0xff', 0x6f000000, 0xff, source='0x0'),
            _event_case(address, 'answer_2_low_bits_set', 0x00ffffff, 0xffffffff00000002, source='0x123456789'),
            _event_case(address, 'flag_0x80000000_stops', 0x80000000, None),
            _event_case(address, 'flag_0x10000000_stops', 0x10000000, None),
            _event_case(address, 'both_flags_stop', 0xffffffff, None),
            _event_case(address, 'other_high_bits_go_on', 0x6f00ffff, 1),
            # The base handler sets a stop bit, or clears the set ones: the byte is read after it.
            _event_case(address, 'base_sets_stop_bit', 0, 1, base_writes_byte=0x10),
            _event_case(address, 'base_clears_stop_bits', 0x90000000, 1, base_writes_byte=0x00)]


# syncs_selected_id_and_index and selects_entry_id_by_index
APPLY_SELECTED_INDEX = '0x02752100'
APPLY_SELECTED_ID = '0x02762360'
SELECTOR_FLAGS, SELECTOR_ID, SELECTOR_SLOT, SELECTOR_LIST = 0xc, 0x20, 0x28, 0x30
LIST_BEGIN, LIST_END = 0x18, 0x20
ENTRY_SIZE, ENTRY_ID = 0x18, 0x10


def _selector(ids: list[int], current: int, slot: int | None, flags: int = 0, null_bounds: bool = False,
              past_end: int = 0x0bad0bad):
    """The selector, its slot (holding `slot`, or none) and its list over `ids` and one entry past the end."""
    entries = bytearray(b'\x33' * (ENTRY_SIZE * (len(ids) + 1)))
    for i, ident in enumerate(ids + [past_end]):
        entries[ENTRY_SIZE * i + ENTRY_ID:ENTRY_SIZE * i + ENTRY_ID + 4] = struct.pack('<I', ident & 0xffffffff)
    buffers = {'selector': {'size': 0x38, 'bytes': _filled(0x38, 'e1', {
                   SELECTOR_FLAGS: bytes([flags]), SELECTOR_ID: struct.pack('<I', current & 0xffffffff),
                   SELECTOR_SLOT: bytes(8), SELECTOR_LIST: bytes(8)})},
               'slot': {'size': 0x8, 'bytes': _u32(slot if slot is not None else 0) + '9d9d9d9d'},
               'list': {'size': 0x28, 'bytes': _filled(0x28, '4b', {LIST_BEGIN: bytes(16)})},
               'entries': {'size': len(entries), 'bytes': entries.hex()}}
    memory = [{'addr': f'buf:selector+{SELECTOR_LIST:#x}', 'pointer': 'list'}]
    if slot is not None:
        memory.append({'addr': f'buf:selector+{SELECTOR_SLOT:#x}', 'pointer': 'slot'})
    if not null_bounds:
        memory += [{'addr': f'buf:list+{LIST_BEGIN:#x}', 'pointer': 'entries'},
                   {'addr': f'buf:list+{LIST_END:#x}', 'pointer': f'entries+{ENTRY_SIZE * len(ids):#x}'}]
    return buffers, memory


def _sync_case(address: str, cid: str, ids: list[int], current: int, slot: int | None,
               null_bounds: bool = False) -> dict:
    buffers, memory = _selector(ids, current, slot, null_bounds=null_bounds)
    stubs = []
    if slot is not None and (slot & 0xffffffff) != (current & 0xffffffff):
        # The index is a 32-bit argument (-1 when the id is not in the list).
        stubs.append({'address': APPLY_SELECTED_INDEX, 'argc': 2, 'argi': [0], 'argu32': [1]})
    return _case(address, cid, 'void', {'rdi': 'buf:selector'}, buffers, memory, stubs)


def sync_cases(address: str, value: int | None) -> list[dict]:
    ids = [0x11, 0x2222, 0x0, 0xffffffff, 0x80000000, 0x2222, 0x77]
    return [_sync_case(address, 'no_slot', ids, 5, None),
            _sync_case(address, 'same_id', ids, 0x77, 0x77),
            _sync_case(address, 'found_first', ids, 5, 0x11),
            _sync_case(address, 'found_inside', ids, 5, 0xffffffff),
            _sync_case(address, 'found_last', ids, 5, 0x77),
            _sync_case(address, 'found_twice_first_taken', ids, 5, 0x2222),
            _sync_case(address, 'found_id_0', ids, 0x80000000, 0),
            _sync_case(address, 'absent', ids, 5, 0x12345678),
            _sync_case(address, 'absent_past_end_only', ids, 5, 0x0bad0bad),
            _sync_case(address, 'empty_list', [], 1, 2),
            _sync_case(address, 'null_bounds', [], 0xffffffff, 0),
            _sync_case(address, 'one_entry', [0x80000000], 0, 0x80000000)]


def _select_case(address: str, cid: str, ids: list[int], index: int, flags: int = 0) -> dict:
    buffers, memory = _selector(ids, 0x5151, 0x6262, flags=flags)
    stubs = []
    if not flags & 1 and (index & 0xffffffff) < len(ids):
        stubs.append({'address': APPLY_SELECTED_ID, 'argc': 1, 'argi': [0]})
    return _case(address, cid, 'void', {'rdi': 'buf:selector', 'rsi': _hex(index)}, buffers, memory, stubs)


def select_cases(address: str, value: int | None) -> list[dict]:
    ids = [0xa1, 0xffffffff, 0x0, 0x80000001, 0xa5]
    return [_select_case(address, 'first', ids, 0),
            _select_case(address, 'inside', ids, 2),
            _select_case(address, 'last', ids, 4),
            _select_case(address, 'at_count', ids, 5),
            _select_case(address, 'past_count', ids, 6),
            _select_case(address, 'index_minus_1', ids, 0xffffffff),
            _select_case(address, 'other_bits_in_register', ids, 0xdead000000000001),
            _select_case(address, 'other_bits_past_count', ids, 0x0000000100000007),
            _select_case(address, 'locked', ids, 1, flags=0x01),
            _select_case(address, 'locked_all_flags', ids, 0, flags=0xff),
            _select_case(address, 'other_flags_unlocked', ids, 3, flags=0xfe),
            _select_case(address, 'empty_list', [], 0),
            _select_case(address, 'one_entry', [0x7], 0, flags=0x80)]


# releases_counted_reference_at_0x20
TAIL_RELEASE = '0x02759cc0'
FAKE_DESTROY, FAKE_FREE = '0x00500440', '0x00500460'
DESTROY_SLOT, FREE_SLOT = 0x18, 0x20
HOLDER_COUNTED, HOLDER_CLEARED = 0x20, 0x28
COUNTS = 0x8
STRONG_MASK, WEAK_SHIFT, ALIVE = 0x7fff, 15, 0x40000000


def _counts(strong: int, weak: int, alive: bool, high: int = 0) -> int:
    return (strong & STRONG_MASK) | ((weak & STRONG_MASK) << WEAK_SHIFT) | (ALIVE if alive else 0) | high


def _release_case(address: str, cid: str, counts: int | None, destroy_sets: int | None = None) -> dict:
    buffers = {'holder': {'size': 0x38, 'bytes': _filled(0x38, '6d', {HOLDER_COUNTED: bytes(8)})},
               'counted': {'size': 0x10, 'bytes': _filled(0x10, 'c4', {COUNTS: struct.pack('<I', counts or 0)})},
               'counted_vtable': {'size': 0x28, 'bytes': '00' * 0x28}}
    memory = [{'addr': 'buf:counted+0x0', 'pointer': 'counted_vtable'},
              {'addr': f'buf:counted_vtable+{DESTROY_SLOT:#x}', 'guest': FAKE_DESTROY},
              {'addr': f'buf:counted_vtable+{FREE_SLOT:#x}', 'guest': FAKE_FREE}]
    stubs = []
    if counts is not None:
        memory.append({'addr': f'buf:holder+{HOLDER_COUNTED:#x}', 'pointer': 'counted'})
        # Each call compares the counts as they are when it is made.
        seen = [{'arg': 0, 'offset': COUNTS, 'size': 4}]
        bits = (counts & 0xffff8000) | ((counts + STRONG_MASK) & STRONG_MASK)
        if not (counts + STRONG_MASK) & STRONG_MASK and counts & ALIVE:
            destroy = {'address': FAKE_DESTROY, 'argc': 1, 'argi': [0], 'argmem': seen}
            if destroy_sets is not None:
                destroy['writes'] = [{'arg': 0, 'offset': COUNTS, 'bytes': _u32(destroy_sets)}]
                bits = destroy_sets
            stubs.append(destroy)
            bits &= ~ALIVE & 0xffffffff
        if not ((bits >> WEAK_SHIFT) + STRONG_MASK) & STRONG_MASK:
            stubs.append({'address': FAKE_FREE, 'argc': 1, 'argi': [0], 'argmem': seen})
    stubs.append({'address': TAIL_RELEASE, 'argc': 1, 'argi': [0]})
    return _case(address, cid, 'void', {'rdi': 'buf:holder'}, buffers, memory, stubs)


def release_cases(address: str, value: int | None) -> list[dict]:
    return [_release_case(address, 'no_object', None),
            _release_case(address, 'both_drop', _counts(3, 2, True)),
            _release_case(address, 'strong_to_0_destroys', _counts(1, 2, True)),
            _release_case(address, 'both_to_0_destroy_and_free', _counts(1, 1, True)),
            _release_case(address, 'strong_to_0_not_alive', _counts(1, 2, False)),
            _release_case(address, 'weak_to_0_frees', _counts(4, 1, True)),
            _release_case(address, 'not_alive_both_to_0', _counts(1, 1, False)),
            _release_case(address, 'strong_wraps_from_0', _counts(0, 3, True)),
            _release_case(address, 'weak_wraps_from_0', _counts(2, 0, True)),
            _release_case(address, 'destroy_then_weak_wraps', _counts(1, 0, True)),
            _release_case(address, 'top_bit_kept', _counts(1, 1, True, 0x80000000)),
            _release_case(address, 'all_bits', 0xffffffff),
            _release_case(address, 'large_counts', _counts(0x7fff, 0x7fff, False, 0x80000000)),
            # The destroy rewrites the counts: the weak count dropped is the new one.
            _release_case(address, 'destroy_raises_weak', _counts(1, 1, True), destroy_sets=_counts(0, 5, True)),
            _release_case(address, 'destroy_leaves_weak_1', _counts(1, 4, True), destroy_sets=_counts(9, 1, False)),
            _release_case(address, 'destroy_sets_top_bits', _counts(1, 2, True), destroy_sets=0xc0000000)]


# registers_new_entry_at_0x30
ENTRY_ALLOCATOR = 0x056c45c8
FAKE_ALLOCATE = '0x00500480'
CONSTRUCT_ENTRY, SET_ENTRY_VALUE, REGISTER_ENTRY = '0x02aebf90', '0x02aebf60', '0x02aeb740'
ENTRY_SIZE_BYTES = 0x20
OWNER_REGISTRY, OWNER_ENTRY = 0x28, 0x30


def _register_case(address: str, cid: str, allocated: bool, value: str, registry: str,
                   value_writes: list | None = None) -> dict:
    buffers = {'owner': {'size': 0x40, 'bytes': 'b8' * 0x40},
               'entry': {'size': ENTRY_SIZE_BYTES, 'bytes': '2f' * ENTRY_SIZE_BYTES},
               'other_entry': {'size': ENTRY_SIZE_BYTES, 'bytes': '1e' * ENTRY_SIZE_BYTES},
               'registry': {'size': 0x20, 'bytes': '0d' * 0x20},
               'other_registry': {'size': 0x20, 'bytes': '0e' * 0x20}}
    memory = [{'addr': f'0x{ENTRY_ALLOCATOR:08x}', 'guest': FAKE_ALLOCATE}]
    stubs = [{'address': FAKE_ALLOCATE, 'argc': 1, 'argi': [0], 'ret': 'buf:entry' if allocated else '0x0'}]
    if allocated:
        stubs.append({'address': CONSTRUCT_ENTRY, 'argc': 1, 'argi': [0]})
    setter = {'address': SET_ENTRY_VALUE, 'argc': 2, 'argi': [0, 1]}
    if value_writes:
        setter['writes'] = value_writes
    stubs += [setter, {'address': REGISTER_ENTRY, 'argc': 3, 'argi': [0, 1, 2]}]
    args = {'rdi': 'buf:owner', 'rsi': value, 'rdx': registry}
    return _case(address, cid, 'void', args, buffers, memory, stubs)


def register_cases(address: str, value: int | None) -> list[dict]:
    rewrite = [{'arg': 1, 'offset': OWNER_REGISTRY, 'pointer': 'other_registry'},
               {'arg': 1, 'offset': OWNER_ENTRY, 'pointer': 'other_entry'}]
    return [_register_case(address, 'allocated', True, '0x1234', 'buf:registry'),
            _register_case(address, 'allocation_fails', False, '0xfedcba9876543210', 'buf:registry'),
            _register_case(address, 'no_registry', True, '0x0', '0x0'),
            _register_case(address, 'failed_no_registry', False, '0x0', '0x0'),
            # The value is the owner itself, and setting it rewrites the owner's +0x28 and +0x30.
            _register_case(address, 'value_rewrites_owner', True, 'buf:owner', 'buf:registry', rewrite),
            _register_case(address, 'failed_value_rewrites_owner', False, 'buf:owner', 'buf:registry', rewrite)]
