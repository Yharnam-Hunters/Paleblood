#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/owner_block_families.cpp's and forwarding_method_families.cpp's
families, which family_cases.py runs on every member at its own address. Every callee is a scripted
stub comparing its arguments; vtables are buffers whose slots point at fake addresses that the
cases stub. Generated fixtures, not game data.

fills_owner_block_with_two_addresses and fills_owner_block_with_one_address: a block of 0x90
bytes (the last 0x10 must stay as they were) filled with zeros, all bits or a pattern, also placed
8 bytes into a larger buffer; owners null, all bits, a value and buffers (the block itself too).
The result (32 bits) is compared.

updates_and_calls_slot_0x38_unless_refused: an object whose +0xe0 points at a source (target at
+0x18, value at +0x24). No target; targets whose check answers 1, -1, 0x80000000 (go on) or 0 and
0x100000000 (stop: only the low 32 bits count); values 0 and all bits. Two cases whose update
replaces the object's table show that slot 0x38 is read after the update (with a control mutant).

adopts_buffer_at_0x10_after_slot_0x78: a holder (slot 0x78 is a stub told 0) with an old buffer at
+0x10 or none, given a new buffer (or null) and a count (0, 1, all bits, and with other bits in the
register's upper half). The free function is the global 0x056c45d0's: a fake set per case, and the
function the image loads there in one case; it is given the block 8 bytes before the old buffer.
Cases whose slot 0x78 replaces or clears the old buffer, or writes the counts, show that +0x10 is
read and the counts stored after it (with control mutants).

destroys_with_0x0139bf90_then_deletes: an object, null and a large value, each destroyed then
deleted.

clears_0x28_and_passes_0x18_to_0x027286b0: a holder whose +0x18 is null, another buffer, all bits
or the holder itself (the callee then sees +0x28 already cleared); +0x28 zero or set.

passes_float_to_0x01b5f7f0_when_0x010edc70_agrees: the check answers 1, 2, 0xff, 0 or with only
upper bits set (a byte is tested); floats 1.0, -0.0, a NaN, a denormal and the largest, with other
bytes in xmm0 above the float.
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


def _case(address: str, cid: str, returns: str, args: dict, buffers: dict, memory: list, stubs: list) -> dict:
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


# fills_owner_block_with_two_addresses, fills_owner_block_with_one_address
BLOCK_SIZE = 0x90


def _block_case(address: str, cid: str, owner: str, fill: str, block_at: int = 0) -> dict:
    size = BLOCK_SIZE + block_at
    content = _pattern(size, 0x29) if fill == 'pattern' else fill * size
    buffers = {'block': {'size': size, 'bytes': content},
               'owner': {'size': 0x10, 'bytes': '77' * 0x10}}
    args = {'rdi': owner, 'rsi': f'buf:block+{block_at:#x}' if block_at else 'buf:block'}
    return _case(address, cid, 'i32', args, buffers, [], [])


def block_cases(address: str, value: int | None) -> list[dict]:
    return [_block_case(address, 'zeros_owner_buffer', 'buf:owner', '00'),
            _block_case(address, 'all_bits_owner_null', '0x0', 'ff'),
            _block_case(address, 'pattern_owner_all_bits', hex(MASK), 'pattern'),
            _block_case(address, 'pattern_owner_value', '0x123456789abcdef0', 'pattern'),
            _block_case(address, 'owner_is_block', 'buf:block', 'a5'),
            _block_case(address, 'block_inside_buffer', 'buf:owner', 'pattern', block_at=8)]


# updates_and_calls_slot_0x38_unless_refused
CHECK_TARGET, UPDATE_OBJECT = '0x01c3f3b0', '0x020ce2f0'
FAKE_METHOD_38, FAKE_OTHER_METHOD_38 = '0x00500500', '0x00500520'
OBJECT_SOURCE, SOURCE_TARGET, SOURCE_VALUE, METHOD_SLOT_38 = 0xe0, 0x18, 0x24, 0x38


def _update_case(address: str, cid: str, target: bool, answer: int = 1, value: int = 7,
                 update_replaces_table: bool = False) -> dict:
    buffers = {'object': {'size': 0xf0, 'bytes': _pattern(0xf0, 0x51)},
               'vtable': {'size': 0x40, 'bytes': 'e3' * 0x40},
               'other_vtable': {'size': 0x40, 'bytes': 'e4' * 0x40},
               'source': {'size': 0x30, 'bytes': _pattern(0x30, 0x17)},
               'target': {'size': 0x10, 'bytes': '3a' * 0x10}}
    memory = [{'addr': 'buf:object+0x0', 'pointer': 'vtable'},
              {'addr': f'buf:object+{OBJECT_SOURCE:#x}', 'pointer': 'source'},
              {'addr': f'buf:vtable+{METHOD_SLOT_38:#x}', 'guest': FAKE_METHOD_38},
              {'addr': f'buf:other_vtable+{METHOD_SLOT_38:#x}', 'guest': FAKE_OTHER_METHOD_38},
              {'addr': f'buf:source+{SOURCE_VALUE:#x}', 'bytes': _u32(value)}]
    if target:
        memory.append({'addr': f'buf:source+{SOURCE_TARGET:#x}', 'pointer': 'target'})
    else:
        memory.append({'addr': f'buf:source+{SOURCE_TARGET:#x}', 'bytes': _qword(0)})
    stubs = []
    if target:
        stubs.append({'address': CHECK_TARGET, 'argc': 2, 'argi': [0, 1], 'ret': hex(answer)})
    if not target or answer & 0xffffffff:
        update = {'address': UPDATE_OBJECT, 'argc': 1, 'argi': [0]}
        if update_replaces_table:
            update['writes'] = [{'arg': 0, 'offset': 0, 'pointer': 'other_vtable'}]
        stubs.append(update)
        method = FAKE_OTHER_METHOD_38 if update_replaces_table else FAKE_METHOD_38
        stubs.append({'address': method, 'argc': 1, 'argi': [0], 'ret': '0x5a5a5a5a00000001'})
    return _case(address, cid, 'void', {'rdi': 'buf:object'}, buffers, memory, stubs)


def update_cases(address: str, value: int | None) -> list[dict]:
    return [_update_case(address, 'no_target', False),
            _update_case(address, 'target_accepted', True, 1),
            _update_case(address, 'target_answers_minus_one', True, 0xffffffff, value=0xffffffff),
            _update_case(address, 'target_answers_top_bit', True, 0x80000000, value=0),
            _update_case(address, 'target_refused', True, 0),
            _update_case(address, 'target_refused_upper_bits', True, 0x100000000, value=0x80000001),
            # the update replaces the object's table: slot 0x38 is read after it
            _update_case(address, 'update_replaces_table', True, 1, update_replaces_table=True),
            _update_case(address, 'no_target_update_replaces_table', False, update_replaces_table=True)]


# adopts_buffer_at_0x10_after_slot_0x78
FREE_SLOT = '0x056c45d0'
LOADED_FREE = '0x02b11cb0'   # the function the image's relocation puts in the global
FAKE_FREE, FAKE_METHOD_78 = '0x00500540', '0x00500560'
HOLDER_BUFFER, METHOD_SLOT_78, BUFFER_HEADER = 0x10, 0x78, 8


def _adopt_case(address: str, cid: str, old: str | None, new: str, count: int, method_writes: list | None = None,
                after_method: str | None = 'same', loaded_free: bool = False) -> dict:
    """old: the buffer the holder has ('old' or 'other_old' buffer name, None for none);
    after_method: the buffer at +0x10 once slot 0x78 has run ('same' when unchanged)."""
    buffers = {'holder': {'size': 0x30, 'bytes': _pattern(0x30, 0x63)},
               'vtable': {'size': 0x80, 'bytes': 'e5' * 0x80},
               'old': {'size': 0x20, 'bytes': '41' * 0x20},
               'other_old': {'size': 0x20, 'bytes': '42' * 0x20},
               'new': {'size': 0x20, 'bytes': '43' * 0x20}}
    memory = [{'addr': 'buf:holder+0x0', 'pointer': 'vtable'},
              {'addr': f'buf:vtable+{METHOD_SLOT_78:#x}', 'guest': FAKE_METHOD_78}]
    if old:
        memory.append({'addr': f'buf:holder+{HOLDER_BUFFER:#x}', 'pointer': f'{old}+{BUFFER_HEADER:#x}'})
    else:
        memory.append({'addr': f'buf:holder+{HOLDER_BUFFER:#x}', 'bytes': _qword(0)})
    free = LOADED_FREE if loaded_free else FAKE_FREE
    if not loaded_free:
        memory.append({'addr': FREE_SLOT, 'guest': FAKE_FREE})
    method = {'address': FAKE_METHOD_78, 'argc': 2, 'argi': [0, 1]}
    if method_writes:
        method['writes'] = method_writes
    stubs = [method]
    freed = old if after_method == 'same' else after_method
    if freed:
        stubs.append({'address': free, 'argc': 1, 'argi': [0]})
    args = {'rdi': 'buf:holder', 'rsi': new, 'rdx': hex(count & MASK)}
    return _case(address, cid, 'void', args, buffers, memory, stubs)


def adopt_cases(address: str, value: int | None) -> list[dict]:
    clear_buffer = [{'arg': 0, 'offset': HOLDER_BUFFER, 'bytes': _qword(0)}]
    move_buffer = [{'arg': 0, 'offset': HOLDER_BUFFER, 'pointer': f'other_old+{BUFFER_HEADER:#x}'}]
    write_counts = [{'arg': 0, 'offset': 0x18, 'bytes': _u32(0x1111) + _u32(0x2222) + _u32(0x3333)}]
    return [_adopt_case(address, 'old_buffer_replaced', 'old', 'buf:new', 5),
            _adopt_case(address, 'no_old_buffer', None, 'buf:new', 1),
            _adopt_case(address, 'new_buffer_null', 'old', '0x0', 0),
            _adopt_case(address, 'count_all_bits', 'old', 'buf:new', 0xffffffff),
            _adopt_case(address, 'count_upper_bits', None, 'buf:new', 0xdeadbeef00000009),
            _adopt_case(address, 'new_is_old', 'old', f'buf:old+{BUFFER_HEADER:#x}', 3),
            _adopt_case(address, 'free_function_as_loaded', 'old', 'buf:new', 2, loaded_free=True),
            # slot 0x78 clears or replaces the old buffer: +0x10 is read after it
            _adopt_case(address, 'method_clears_buffer', 'old', 'buf:new', 4, clear_buffer, after_method=None),
            _adopt_case(address, 'method_moves_buffer', 'old', 'buf:new', 4, move_buffer, after_method='other_old'),
            _adopt_case(address, 'method_gives_buffer', None, 'buf:new', 4, move_buffer, after_method='other_old'),
            # slot 0x78 writes the counts: they are stored after it
            _adopt_case(address, 'method_writes_counts', 'old', 'buf:new', 6, write_counts)]


# destroys_with_0x0139bf90_then_deletes
DESTROY_OBJECT, OPERATOR_DELETE = '0x0139bf90', '0x02fbe588'


def _delete_case(address: str, cid: str, target: str) -> dict:
    buffers = {'object': {'size': 0x50, 'bytes': _pattern(0x50, 0x0b)}}
    stubs = [{'address': DESTROY_OBJECT, 'argc': 1, 'argi': [0], 'ret': '0x5a5a5a5a00000002'},
             {'address': OPERATOR_DELETE, 'argc': 1, 'argi': [0]}]
    return _case(address, cid, 'void', {'rdi': target}, buffers, [], stubs)


def delete_cases(address: str, value: int | None) -> list[dict]:
    return [_delete_case(address, 'object', 'buf:object'),
            _delete_case(address, 'null', '0x0'),
            _delete_case(address, 'large_value', '0xfedcba9876543210')]


# clears_0x28_and_passes_0x18_to_0x027286b0
PASS_TARGET = '0x027286b0'
HOLDER_TARGET, HOLDER_CLEARED = 0x18, 0x28


def _clear_case(address: str, cid: str, target: str, cleared: int) -> dict:
    buffers = {'holder': {'size': 0x40, 'bytes': _pattern(0x40, 0x35)},
               'target': {'size': 0x10, 'bytes': '5e' * 0x10}}
    memory = [{'addr': f'buf:holder+{HOLDER_CLEARED:#x}', 'bytes': _qword(cleared)}]
    if target.startswith('buf:'):
        memory.append({'addr': f'buf:holder+{HOLDER_TARGET:#x}', 'pointer': target[4:]})
    else:
        memory.append({'addr': f'buf:holder+{HOLDER_TARGET:#x}', 'bytes': _qword(int(target, 16))})
    stubs = []
    if target != '0x0':
        stub = {'address': PASS_TARGET, 'argc': 1, 'argi': [0], 'ret': '0x5a5a5a5a00000003'}
        if target == 'buf:holder':
            # the holder itself: +0x28 is already clear when it is passed on
            stub['argmem'] = [{'arg': 0, 'offset': 0, 'size': 0x40}]
        stubs.append(stub)
    return _case(address, cid, 'void', {'rdi': 'buf:holder'}, buffers, memory, stubs)


def clear_cases(address: str, value: int | None) -> list[dict]:
    return [_clear_case(address, 'no_target', '0x0', 0x1234),
            _clear_case(address, 'no_target_cleared', '0x0', 0),
            _clear_case(address, 'target', 'buf:target', MASK),
            _clear_case(address, 'target_all_bits', hex(MASK), 0x8000000000000000),
            _clear_case(address, 'target_is_holder', 'buf:holder', 0x0102030405060708)]


# passes_float_to_0x01b5f7f0_when_0x010edc70_agrees
AGREES, TAKE_FLOAT = '0x010edc70', '0x01b5f7f0'


def _float_case(address: str, cid: str, answer: int, value: int, upper: str = '00' * 12) -> dict:
    buffers = {'object': {'size': 0x20, 'bytes': _pattern(0x20, 0x47)}}
    stubs = [{'address': AGREES, 'argc': 1, 'argi': [0], 'ret': hex(answer)}]
    if answer & 0xff:
        stubs.append({'address': TAKE_FLOAT, 'argc': 1, 'argi': [0], 'argf32': [0], 'ret': '0x5a5a5a5a00000004'})
    args = {'rdi': 'buf:object', 'xmm0': _u32(value) + upper}
    return _case(address, cid, 'void', args, buffers, [], stubs)


def float_cases(address: str, value: int | None) -> list[dict]:
    return [_float_case(address, 'agrees_one', 1, 0x3f800000),
            _float_case(address, 'agrees_two', 2, 0x80000000),
            _float_case(address, 'agrees_byte_all_bits', 0xff, 0x7fc00001, 'a1' * 12),
            _float_case(address, 'agrees_upper_bits', 0xffffff01, 0x00000001),
            _float_case(address, 'largest_float', 1, 0x7f7fffff, '5c' * 12),
            _float_case(address, 'refuses', 0, 0x3f800000),
            _float_case(address, 'refuses_upper_bits', 0x100, 0xbf800000),
            _float_case(address, 'refuses_all_upper_bits', 0xffffffffffffff00, 0x40490fdb, 'ff' * 12)]
