#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases and mutants for the leaf function families (symbols/families.csv).

tools/verify.py family imports this module: cases(family, address, value) returns the synthetic
cases one member runs (schema 1, docs/VERIFY.md), and MUTANTS names the deliberately wrong
replacements (test/family_mutants.c) each family's cases must catch. Every case seeds the
registers a correct replacement must leave alone with values no replacement could guess, so a
body that returns early, returns the wrong register or writes memory fails.

Nothing here reads the executable: the cases are the same for every member of a family, apart
from the member's address and its shape parameter.
"""
from __future__ import annotations

import hashlib

ARGUMENT_REGISTERS = ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9')
# An entry rax no correct replacement produces by accident (the harness otherwise enters with 0).
RAX_SEED = 0x5aa5c33c0ff0e11e
PROBE_SIZE = 64


def probe_bytes(seed: int, size: int = PROBE_SIZE) -> str:
    """SIZE (at most 255) distinct non-zero bytes, so a write of any value or a read at a wrong
    offset shows."""
    if size > 255:
        raise ValueError('at most 255 distinct non-zero bytes')
    return bytes(((seed + 37 * i) % 255) + 1 for i in range(size)).hex()


def base_case(address: str, case_id: str, returns: str) -> dict:
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': returns, 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def pointer_case(address: str, case_id: str, seed: int, target: str | None, target_size: int = 0xa0) -> dict:
    """An object of distinct bytes whose qword at +8 points at a TARGET buffer of distinct bytes
    (or is 0 for None); rax is seeded and the other arguments point into a probe buffer."""
    case = base_case(address, case_id, 'i64')
    pattern = bytearray.fromhex(probe_bytes(seed))
    pattern[8:16] = bytes(8)
    case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': pattern.hex()},
                       'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)}}
    if target is not None:
        case['buffers'][target] = {'size': target_size, 'bytes': probe_bytes(seed + 2, target_size)}
        case['memory'].append({'addr': 'buf:object+0x8', 'pointer': target})
    case['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
    case['args'].update({'rdi': 'buf:object', 'rax': hex(RAX_SEED)})
    case['observe_registers'] = list(ARGUMENT_REGISTERS)
    return case


def through_pointer_cases(address: str, value: int | None) -> list[dict]:
    """A read or address K bytes into the object that the pointer at +8 points at: two objects
    and targets of distinct bytes (a read at another offset or width returns another value)."""
    return [pointer_case(address, 'target_a', 41, 'target'), pointer_case(address, 'target_b', 97, 'target')]


PART_OFFSET = 0xd0


def part_pointer_cases(address: str, value: int | None) -> list[dict]:
    """The pointer at +8 plus PART_OFFSET, or null: a target large enough for the part, and a null
    pointer (the result must be 0, not PART_OFFSET). The original computes in rcx, so the cases
    compare rax and memory, not the argument registers."""
    cases = [pointer_case(address, 'owner', 43, 'owner', PART_OFFSET + 0x10),
             pointer_case(address, 'null_owner', 59, None)]
    for case in cases:
        del case['observe_registers']
    return cases


STRING_POINTER = 0x30   # where the object keeps the wide string pointer
STRING_TEXT = 0x8       # inline text, or the heap text pointer
STRING_CAPACITY = 0x20


def wide_string_cases(address: str, value: int | None) -> list[dict]:
    """The text of the wide string at +0x30: capacities 0 and 7 (inline: the string's address plus
    8) and 8 and 2**63 (the heap text pointer at +8, which points at its own buffer)."""
    out = []
    for n, capacity in enumerate((0, 7, 8, 1 << 63)):
        case = base_case(address, f'capacity_{capacity:x}', 'i64')
        pattern = bytearray.fromhex(probe_bytes(61 + n))
        pattern[STRING_POINTER:STRING_POINTER + 8] = bytes(8)
        string = bytearray.fromhex(probe_bytes(71 + n, 0x28))
        string[STRING_CAPACITY:STRING_CAPACITY + 8] = capacity.to_bytes(8, 'little')
        case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': pattern.hex()},
                           'string': {'size': 0x28, 'bytes': string.hex()},
                           'heap_text': {'size': 0x20, 'bytes': probe_bytes(81 + n, 0x20)},
                           'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(91 + n)}}
        case['memory'] = [{'addr': f'buf:object+0x{STRING_POINTER:x}', 'pointer': 'string'},
                          {'addr': f'buf:string+0x{STRING_TEXT:x}', 'pointer': 'heap_text'}]
        case['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
        case['args'].update({'rdi': 'buf:object', 'rax': hex(RAX_SEED)})
        case['observe_registers'] = list(ARGUMENT_REGISTERS)
        out.append(case)
    return out


def first_field_store_cases(address: str, value: int | None) -> list[dict]:
    """A store at the start of the object whose shape parameter is the stored constant, not an
    offset: setter_cases at offset 0 (any other store shows; rax must come back as the object)."""
    return setter_cases(address, None)


BIT_FIELD_VALUES = ((0xfffffff5, 0x12345abc), (0x0, 0xffffffff), (0x2ab, 0x7))


def bit_field_cases(address: str, value: int | None) -> list[dict]:
    """A bit-field write to the first dword of an object of distinct bytes, from esi (and edx):
    values with bits beyond the field set (which must not reach the dword), all clear and in range.
    The originals leave a scratch value in eax and change esi and edx, so the cases compare memory
    only (returns void)."""
    out = []
    for n, (first, second) in enumerate(BIT_FIELD_VALUES):
        case = base_case(address, f'values_{n}', 'void')
        case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': probe_bytes(101 + 7 * n)}}
        case['args'] = {'rdi': 'buf:object', 'rsi': hex(first), 'rdx': hex(second), 'rax': hex(RAX_SEED)}
        out.append(case)
    return out


def untouched_state_cases(address: str, returns: str) -> list[dict]:
    """Cases for a body that must not touch memory or the argument registers: every argument
    points into a probe buffer (any write through one shows), the argument registers are
    compared after the call, and rax enters with RAX_SEED."""
    pointers = base_case(address, 'argument_pointers', returns)
    pointers['buffers'] = {'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(11)}}
    pointers['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
    pointers['args']['rax'] = hex(RAX_SEED)
    pointers['observe_registers'] = list(ARGUMENT_REGISTERS)
    integers = base_case(address, 'argument_integers', returns)
    integers['args'] = {'rdi': '0xffffffffffffffff', 'rsi': '0x8000000000000000', 'rdx': '0x7fffffffffffffff',
                        'rcx': '0x1', 'r8': '0x0', 'r9': '0xfedcba9876543210', 'rax': hex(~RAX_SEED & (2**64 - 1))}
    integers['observe_registers'] = list(ARGUMENT_REGISTERS)
    return [pointers, integers]


COUNT_OFFSET = 0x8                 # where atomically_increments_dword_at_8 keeps its count
COUNTS = (0x0, 0x7fffffff, 0xfffffffe, 0xffffffff)   # around the sign bit and the wrap


def count_cases(address: str, value: int | None) -> list[dict]:
    """An atomic increment of the 32-bit count at COUNT_OFFSET that returns the old count: the
    object is distinct bytes with the count set to each of COUNTS (0xffffffff wraps to 0 and must
    not carry into the next byte), rax enters seeded and must come back as the old count, and the
    other arguments point into a probe buffer and are compared."""
    out = []
    for n, count in enumerate(COUNTS):
        pattern = bytearray.fromhex(probe_bytes(13 + 40 * n))
        pattern[COUNT_OFFSET:COUNT_OFFSET + 4] = count.to_bytes(4, 'little')
        case = base_case(address, f'count_{count:x}', 'i64')
        case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': pattern.hex()},
                           'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(14 + 40 * n)}}
        case['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
        case['args']['rdi'] = 'buf:object'
        case['args']['rax'] = hex(RAX_SEED)
        case['observe_registers'] = list(ARGUMENT_REGISTERS)
        out.append(case)
    return out


TAGGED_FILL = 0xc3     # a field of these bytes has its low bit set
UNTAGGED_FILL = 0x3c   # and of these, clear


def tagged_pointer_cases(address: str, value: int | None) -> list[dict]:
    """A getter of the field at offset VALUE with bit 0 cleared: the objects of field_cases, with
    the eight bytes at the field all TAGGED_FILL in the first and all UNTAGGED_FILL in the second,
    so a getter that keeps the low bit fails the first and one that clears more fails both."""
    out = field_cases(address, value)
    for case, fill in zip(out, (TAGGED_FILL, UNTAGGED_FILL)):
        pattern = bytearray.fromhex(case['buffers']['object']['bytes'])
        start = int(case['args']['rdi'].split('+')[1], 16) + (value or 0)
        pattern[start:start + 8] = bytes([fill]) * 8
        case['buffers']['object']['bytes'] = pattern.hex()
    return out


def returns_nothing(address: str, value: int | None) -> list[dict]:
    # `ret` keeps rax too, so the full register is compared (i64), not only the void contract.
    return untouched_state_cases(address, 'i64') + [base_case(address, 'void_contract', 'void')]


KEY_OFFSET = 0x18        # where matches_sixteen_byte_key's object keeps its key
KEY_SIZE = 0x10
OBJECT_KEY = (0x1122334455667788, 0x99aabbccddeeff01)


def key_case(address: str, case_id: str, object_key: tuple[int, int], key: tuple[int, int], size: int) -> dict:
    case = base_case(address, case_id, 'i64')
    pattern = bytearray.fromhex(probe_bytes(17))
    pattern[KEY_OFFSET:KEY_OFFSET + KEY_SIZE] = b''.join(half.to_bytes(8, 'little') for half in object_key)
    case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': pattern.hex()},
                       'key': {'size': KEY_SIZE, 'bytes': b''.join(half.to_bytes(8, 'little') for half in key).hex()}}
    case['args'] = {'rdi': 'buf:object', 'rsi': 'buf:key', 'rdx': hex(size), 'rax': hex(RAX_SEED)}
    return case


def key_cases(address: str, value: int | None) -> list[dict]:
    """A key match (object, key, size): the same key (1), each half different (0), sizes 15, 17
    and 0 (0) and 16 with upper bits set in rdx (1: only edx is compared), and a key whose first
    half is 0, which matches whatever the second halves are (1) unless the object's first half
    is not 0. The body may change rcx, rdx and rsi (the original does), so the cases compare rax
    and memory only."""
    first, second = OBJECT_KEY
    return [
        key_case(address, 'same_key', OBJECT_KEY, OBJECT_KEY, KEY_SIZE),
        key_case(address, 'second_half_differs', OBJECT_KEY, (first, second ^ 1), KEY_SIZE),
        key_case(address, 'first_half_differs', OBJECT_KEY, (first ^ 1 << 63, second), KEY_SIZE),
        key_case(address, 'size_15', OBJECT_KEY, OBJECT_KEY, KEY_SIZE - 1),
        key_case(address, 'size_17', OBJECT_KEY, OBJECT_KEY, KEY_SIZE + 1),
        key_case(address, 'size_0', OBJECT_KEY, OBJECT_KEY, 0),
        key_case(address, 'size_16_upper_bits', OBJECT_KEY, OBJECT_KEY, 0xffffffff00000000 | KEY_SIZE),
        key_case(address, 'zero_first_half_any_second', (0, second), (0, ~second & (2**64 - 1)), KEY_SIZE),
        key_case(address, 'zero_first_half_same_second', (0, second), (0, second), KEY_SIZE),
        key_case(address, 'zero_key_nonzero_object', OBJECT_KEY, (0, second), KEY_SIZE),
    ]


LIST_OFFSET = 0x30      # the first node pointer in the object
NODE_NEXT = 0x10        # each node's next pointer
NODE_VALUE = 0x18       # each node's 32-bit value
NODE_SIZE = 0x20
NODE_VALUES = (0x80000001, 0x7ffffffe, 0xffffffff, 0x12345678)


def list_case(address: str, case_id: str, length: int, index: int | None = None) -> dict:
    """An object whose list has LENGTH nodes in separate buffers of distinct bytes, linked through
    memory entries; the last next pointer is 0. rsi is INDEX when given."""
    case = base_case(address, case_id, 'i64')
    pattern = bytearray.fromhex(probe_bytes(23))
    pattern[LIST_OFFSET:LIST_OFFSET + 8] = bytes(8)
    case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': pattern.hex()}}
    for n in range(length):
        node = bytearray.fromhex(probe_bytes(31 + 20 * n, NODE_SIZE))
        node[NODE_NEXT:NODE_NEXT + 8] = bytes(8)
        node[NODE_VALUE:NODE_VALUE + 4] = NODE_VALUES[n].to_bytes(4, 'little')
        case['buffers'][f'node{n}'] = {'size': NODE_SIZE, 'bytes': node.hex()}
        where = f'buf:object+0x{LIST_OFFSET:x}' if n == 0 else f'buf:node{n - 1}+0x{NODE_NEXT:x}'
        case['memory'].append({'addr': where, 'pointer': f'node{n}'})
    case['args'] = {'rdi': 'buf:object', 'rax': hex(RAX_SEED)}
    if index is not None:
        case['args']['rsi'] = hex(index)
    return case


def list_length_cases(address: str, value: int | None) -> list[dict]:
    """The length of lists of 0, 1, 2 and 4 nodes. The body may change rcx (the original does), so
    the cases compare rax and memory only."""
    return [list_case(address, f'length_{n}', n) for n in (0, 1, 2, 4)]


def list_lookup_cases(address: str, value: int | None) -> list[dict]:
    """Node values of a 4-node list at indexes 0..3 (values around the sign bit and all bits), 4
    (past the end: 0), 2**32 - 1 (a long way past: 0) and 2**32 + 1 in rsi (only esi counts: node
    1), and index 0 of an empty list (0). The body may change rcx and rsi (the original does), so
    the cases compare rax and memory only."""
    cases = [list_case(address, f'index_{i}', 4, i) for i in range(5)]
    cases.append(list_case(address, 'index_all_bits', 4, 0xffffffff))
    cases.append(list_case(address, 'index_upper_bits', 4, 0x100000001))
    cases.append(list_case(address, 'empty', 0, 0))
    return cases


def returns_a_value(address: str, value: int | None) -> list[dict]:
    """A body that writes all of rax and nothing else (returns_zero, returns_constant and the
    argument-returning families): rax enters with a seed no u32 constant equals, so a body that
    leaves rax or writes only part of it fails; rdi and rsi point at different probe slots (and
    hold different integers), which tells the first argument from the second."""
    return untouched_state_cases(address, 'i64')


OBJECT_SIZE = 0xa0     # room for the largest positive 8-bit offset plus a qword, from either start
SHIFTED_START = 0x10  # where the shifted cases' object starts in its buffer


def object_size(offset: int) -> int:
    """OBJECT_SIZE, or for a far field (the _far families) room for the field plus a qword
    after it from the shifted start."""
    return max(OBJECT_SIZE, SHIFTED_START + offset + 0x10)


def object_bytes(seed: int, size: int) -> str:
    """probe_bytes for an object of up to 255 bytes. A larger one (a far field) is SHA-256 of
    the seed and the block number, block after block: no repeating pattern, so a read or a
    store at a wrong offset still shows."""
    if size <= 255:
        return probe_bytes(seed, size)
    blocks = (hashlib.sha256(f'{seed}:{block}'.encode()).digest() for block in range((size + 31) // 32))
    return b''.join(blocks)[:size].hex()


def field_cases(address: str, value: int | None) -> list[dict]:
    """A getter of the field at offset VALUE (0 for the first-field families): rdi points at an
    object buffer of distinct bytes, so a read at any other offset or width returns another
    value; the object starts at the buffer and 16 bytes into it, with two byte patterns. The
    other arguments point into a probe buffer and are compared after the call; rax is seeded."""
    out = []
    size = object_size(value or 0)
    for case_id, seed, start in (('object_a', 3, 0), ('object_b', 101, 0), ('object_shifted', 57, SHIFTED_START)):
        case = base_case(address, case_id, 'i64')
        case['buffers'] = {'object': {'size': size, 'bytes': object_bytes(seed, size)},
                           'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)}}
        case['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
        case['args']['rdi'] = f'buf:object+0x{start:x}'
        case['args']['rax'] = hex(RAX_SEED)
        case['observe_registers'] = list(ARGUMENT_REGISTERS)
        out.append(case)
    return out


def setter_cases(address: str, value: int | None) -> list[dict]:
    """A setter of the field at offset VALUE (0 for the first-field setters) from rsi: rdi points
    at an object of distinct bytes, and rsi holds the bitwise complement of the eight bytes at the
    field, so a store of any width at the field changes every byte it covers and a store at any
    other offset changes other bytes. rax is seeded (a setter leaves it), the other arguments point
    into a probe buffer, and the argument registers are compared."""
    offset = value or 0
    size = object_size(offset)
    out = []
    for case_id, seed, start in (('object_a', 5, 0), ('object_b', 133, 0), ('object_shifted', 71, SHIFTED_START)):
        pattern = bytes.fromhex(object_bytes(seed, size))
        field = pattern[start + offset:start + offset + 8]
        case = base_case(address, case_id, 'i64')
        case['buffers'] = {'object': {'size': size, 'bytes': pattern.hex()},
                           'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)}}
        case['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
        case['args']['rdi'] = f'buf:object+0x{start:x}'
        case['args']['rsi'] = hex(int.from_bytes(bytes(b ^ 0xff for b in field), 'little'))
        case['args']['rax'] = hex(RAX_SEED)
        case['observe_registers'] = list(ARGUMENT_REGISTERS)
        out.append(case)
    return out


RECORD_SIZE = 0x80
RECORD_PADDING = 0x10   # bytes on each side of the record, which a correct body leaves


def record_cases(address: str, value: int | None) -> list[dict]:
    """A body that clears the 15 words at rsi, stores rdi at rsi+0x78 and returns 0 in all of rax:
    rsi points into a buffer of distinct bytes with 16 bytes on each side of the record (a store
    past either end shows), rdi is a pointer, an integer with every bit set and zero, rax enters
    seeded, and the other arguments point into a probe buffer and are compared."""
    out = []
    for case_id, seed, start, owner in (('record_owner_pointer', 7, 0, 'buf:probe+0x18'),
                                        ('record_owner_all_bits', 91, 0, '0xffffffffffffffff'),
                                        ('record_owner_zero', 173, 0, '0x0'),
                                        ('record_unaligned', 29, 3, '0x0123456789abcdef')):
        size = RECORD_SIZE + 2 * RECORD_PADDING
        case = base_case(address, case_id, 'i64')
        case['buffers'] = {'record': {'size': size, 'bytes': probe_bytes(seed, size)},
                           'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)}}
        case['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
        case['args']['rdi'] = owner
        case['args']['rsi'] = f'buf:record+0x{RECORD_PADDING + start:x}'
        case['args']['rax'] = hex(RAX_SEED)
        out.append(case)
    return out


XMM0_SEED = '1e1fe00f3cc3a55a8899aabbccddeeff'
XMM1_SEED = '00112233445566778899aabbccddeeff'


def returns_float_zero(address: str, value: int | None) -> list[dict]:
    """A body that zeroes xmm0 and nothing else: xmm0 and xmm1 enter seeded and are compared whole
    (vec), and the untouched-state cases check rax, memory and the argument registers."""
    vector = base_case(address, 'vector_registers', 'vec')
    vector['args'] = {'xmm0': XMM0_SEED, 'xmm1': XMM1_SEED, 'rax': hex(RAX_SEED)}
    return [vector] + untouched_state_cases(address, 'i64')


CASES = {
    'returns_nothing': returns_nothing,
    'returns_zero': returns_a_value,
    'returns_constant': returns_a_value,
    'matches_sixteen_byte_key': key_cases,
    'counts_list_nodes': list_length_cases,
    'reads_nth_list_node_dword': list_lookup_cases,
    'reads_qword_field': field_cases,
    'reads_dword_field': field_cases,
    'stores_constant_in_first_dword_returning_object': first_field_store_cases,
    'reads_dword_through_pointer_at_8': through_pointer_cases,
    'reads_qword_through_pointer_at_8': through_pointer_cases,
    'address_through_pointer_at_8': through_pointer_cases,
    'returns_all_bits': returns_a_value,
    'resets_first_qword_returning_object': setter_cases,
    'adjusts_pointer_at_8_unless_null': part_pointer_cases,
    'reads_wide_string_text_at_0x30': wide_string_cases,
    'writes_top_four_bits_of_first_dword': bit_field_cases,
    'writes_low_ten_bits_of_first_dword': bit_field_cases,
    'packs_first_dword_from_two_bit_fields': bit_field_cases,
    'reads_word_field': field_cases,
    'reads_byte_field': field_cases,
    'field_address': field_cases,
    'reads_first_qword': field_cases,
    'reads_first_dword': field_cases,
    'writes_qword_field': setter_cases,
    'writes_wide_string_at_0x28_to_sink': lambda address, value: sink_cases(address, 'writes_wide_string_at_0x28_to_sink'),
    'writes_wide_string_through_0x20_to_sink': lambda address, value: sink_cases(address, 'writes_wide_string_through_0x20_to_sink'),
    'writes_text_at_0x20_to_sink': lambda address, value: sink_cases(address, 'writes_text_at_0x20_to_sink'),
    'toggles_flag_at_8_through_first_virtual': lambda address, value: toggle_cases(address, 8),
    'toggles_flag_at_0x20_through_first_virtual': lambda address, value: toggle_cases(address, 0x20),
    'resets_wide_string_at_8': lambda address, value: reset_cases(address, 8),
    'resets_wide_string_at_0x10': lambda address, value: reset_cases(address, 0x10),
    'tail_calls_slot_0_of_nth_node_at_0x30': lambda address, value: nth_node_cases(address),
    'shifts_arguments_into_virtual_0x88': lambda address, value: shifted_arguments_cases(address),
    'calls_0x78_stores_dword_at_0x20_calls_0xa8': lambda address, value: restart_cases(address),
    'flushes_pending_through_member_at_0x40': lambda address, value: flush_cases(address),
    'tail_calls_0x68_of_what_0x38_returns': lambda address, value: returned_object_cases(address),
    'erases_subtree_with_wide_string_at_0x28': lambda address, value: tree_cases(address, True),
    'writes_dword_field': setter_cases,
    'writes_word_field': setter_cases,
    'writes_byte_field': setter_cases,
    'writes_first_qword': setter_cases,
    'writes_first_dword': setter_cases,
    'returns_first_argument': returns_a_value,
    'atomically_increments_dword_at_8': count_cases,
    'reads_pointer_without_low_bit': tagged_pointer_cases,
    'clears_first_two_qwords': setter_cases,
    'clears_first_four_qwords': setter_cases,
    'clears_two_qwords_and_a_dword': setter_cases,
    'returns_second_argument': returns_a_value,
    'returns_float_zero': returns_float_zero,
    'zeroes_record_and_stores_first_argument': record_cases,
}

# Deliberately wrong replacements (test/family_mutants.c) every member's cases must catch.
MUTANTS = {
    'returns_nothing': ['bb_mutant_writes_first_argument', 'bb_mutant_returns_zero',
                        'bb_mutant_clobbers_second_argument'],
    'returns_zero': ['bb_mutant_does_nothing', 'bb_mutant_returns_one', 'bb_mutant_returns_zero_clobbers_second_argument'],
    # A constant family's mutants include the neighbouring value's replacement (tools/family_verify.py);
    # returns_one or returns_zero would be correct for the members whose constant is 1 or 0.
    'returns_constant': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument',
                         'bb_mutant_clobbers_second_argument'],
    # Key match and list walks: the size check, the zero-key rule, one node off, the first node
    # skipped.
    'matches_sixteen_byte_key': ['bb_mutant_returns_zero', 'bb_mutant_returns_one',
                                 'bb_mutant_key_ignores_size', 'bb_mutant_key_compares_both_halves'],
    'counts_list_nodes': ['bb_mutant_does_nothing', 'bb_mutant_returns_one', 'bb_mutant_counts_from_second_node'],
    'reads_nth_list_node_dword': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_reads_next_list_node'],
    # Field getters: the neighbouring offset's replacement is the off-by-offset mutant.
    'reads_qword_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'reads_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    # Constructors, reads through the pointer at +8 and small records: the neighbouring value's
    # replacement, a missing store, the wrong return, the text always inline, a whole-dword store.
    'stores_constant_in_first_dword_returning_object': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument',
                                                        'bb_mutant_returns_zero'],
    'reads_dword_through_pointer_at_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'reads_qword_through_pointer_at_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'address_through_pointer_at_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'returns_all_bits': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_returns_one',
                         'bb_mutant_returns_low_all_bits'],
    'resets_first_qword_returning_object': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument',
                                            'bb_mutant_returns_zero'],
    'adjusts_pointer_at_8_unless_null': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                         'bb_mutant_returns_first_argument'],
    'reads_wide_string_text_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                       'bb_mutant_wide_string_text_inline'],
    'writes_top_four_bits_of_first_dword': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_dword'],
    'writes_low_ten_bits_of_first_dword': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_dword'],
    'packs_first_dword_from_two_bit_fields': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_dword'],
    'reads_word_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'reads_byte_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'field_address': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'reads_first_qword': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument',
                          'bb_mutant_reads_first_dword'],
    'reads_first_dword': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument',
                          'bb_mutant_reads_first_qword'],
    # Setters: for the four {d8} families the neighbouring offset's replacement stores at the wrong
    # place; store-then-clear-eax shows a setter must leave rax.
    'writes_qword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    # Records that call virtual functions: no call, and the one rule each family keeps (the heap
    # text, the value, the state check, the free) left out.
    'writes_wide_string_at_0x28_to_sink': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_sink_gets_inline_text_at_0x28',
        'bb_mutant_wide_string_at_0x28_write_read_after_value', 'bb_mutant_wide_string_at_0x28_text_read_after_value'],
    'writes_wide_string_through_0x20_to_sink': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_sink_gets_inline_text_through_0x20',
        'bb_mutant_wide_string_through_0x20_write_read_after_value', 'bb_mutant_wide_string_through_0x20_text_read_after_value'],
    'writes_text_at_0x20_to_sink': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_sink_gets_text_without_value',
        'bb_mutant_text_at_0x20_write_read_after_value', 'bb_mutant_text_at_0x20_text_read_after_value'],
    'toggles_flag_at_8_through_first_virtual': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_toggle_at_8_ignores_state'],
    'toggles_flag_at_0x20_through_first_virtual': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_toggle_at_0x20_ignores_state'],
    'resets_wide_string_at_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_resets_wide_string_at_8_without_free'],
    'resets_wide_string_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_resets_wide_string_at_0x10_without_free'],
    'tail_calls_slot_0_of_nth_node_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_calls_node_after_the_nth'],
    'shifts_arguments_into_virtual_0x88': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_virtual_0x88_keeps_second_argument'],
    'calls_0x78_stores_dword_at_0x20_calls_0xa8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_restarts_without_storing_dword'],
    'flushes_pending_through_member_at_0x40': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_flushes_while_flushing',
        'bb_mutant_flushes_through_table_read_before_prepare', 'bb_mutant_flushes_with_flags_read_before_calls'],
    'tail_calls_0x68_of_what_0x38_returns': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_calls_0x68_of_object_itself'],
    'erases_subtree_with_wide_string_at_0x28': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_erases_nodes_without_their_strings',
        'bb_mutant_erases_left_spine_only'],
    'writes_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'writes_word_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'writes_byte_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'writes_first_qword': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_dword', 'bb_mutant_returns_zero',
                           'bb_mutant_writes_first_qword_clears_eax'],
    'writes_first_dword': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_qword', 'bb_mutant_returns_zero'],
    'returns_first_argument': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_returns_second_argument'],
    # Small records: the count's new value or a 64-bit increment; clears that miss or pass the
    # record's end.
    'atomically_increments_dword_at_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                         'bb_mutant_increment_returns_new_count',
                                         'bb_mutant_increments_qword_at_8'],
    'reads_pointer_without_low_bit': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'clears_first_two_qwords': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_clears_first_8_bytes',
                                'bb_mutant_clears_first_24_bytes'],
    'clears_first_four_qwords': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_clears_first_24_bytes'],
    'clears_two_qwords_and_a_dword': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                      'bb_mutant_clears_first_16_bytes', 'bb_mutant_clears_first_24_bytes'],
    'returns_second_argument': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_returns_first_argument'],
    'returns_float_zero': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_returns_float_one',
                           'bb_mutant_zeroes_low_float_only', 'bb_mutant_zeroes_xmm0_and_xmm1'],
    'zeroes_record_and_stores_first_argument': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_record_last_word_left',
                                                'bb_mutant_record_owner_is_record', 'bb_mutant_record_returns_owner'],
}


# Records that call virtual functions (game/leaf/virtual_records.cpp): every slot of every fake
# table is a stub at its own address returning its own value, so a call through the wrong slot or
# the wrong object is a different call.
RECORD_STUB_BASE = 0x00502000
RECORD_TABLE_STRIDE = 0x400
RECORD_TABLE_SLOTS = 0x20
WIDE_HEAP_CAPACITY = 8
WIDE_CAPACITIES = (7, 8, 1 << 63)   # inline, the smallest heap one, and one that is negative signed


def record_stub(table: int, slot: int) -> str:
    return f'0x{RECORD_STUB_BASE + RECORD_TABLE_STRIDE * table + 0x10 * slot:08x}'


def stubbed_table(case: dict, name: str, table: int, seed: int, argc: dict[int, int],
                  returns: dict[int, int] | None = None, attach: bool = True) -> None:
    """Buffer NAME gets a table of stubs at +0: slot offset K's stub compares argc[K] arguments
    (1 for the rest) and returns returns[K] (else its own value). Unattached, the table is only
    buffer NAME_table, for a callback to store."""
    case['buffers'][f'{name}_table'] = {'size': 8 * RECORD_TABLE_SLOTS}
    if attach:
        case['memory'].append({'addr': f'buf:{name}+0x0', 'pointer': f'{name}_table'})
    for slot in range(RECORD_TABLE_SLOTS):
        case['memory'].append({'addr': f'buf:{name}_table+0x{8 * slot:x}', 'guest': record_stub(table, slot)})
        value = (returns or {}).get(8 * slot, seed << 32 | table << 8 | slot)
        case['stubs'].append({'address': record_stub(table, slot), 'argc': argc.get(8 * slot, 1), 'ret': value})


def wide_string_bytes(seed: int, capacity: int, size: int = 0x20) -> bytearray:
    """SIZE probe bytes whose wide string at 0 has CAPACITY (its size distinct bytes)."""
    pattern = bytearray.fromhex(probe_bytes(seed, size))
    pattern[0x18:0x20] = capacity.to_bytes(8, 'little')
    return pattern


SINK_WRITE = 0x90
RECORD_VALUE = 0x60


def sink_case(address: str, family: str, case_id: str, seed: int, capacity: int) -> dict:
    """A record and a sink, each with its own table of stubs: the record's slot 0x60 returns a
    value, and the sink's slot 0x90 (compared: sink, text, value) returns the result. The text is
    a wide string's (inline below capacity 8, else a heap buffer) or a plain pointer."""
    case = base_case(address, case_id, 'i64')
    record = bytearray.fromhex(probe_bytes(seed, 0x60))
    case['buffers'] = {'sink': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)},
                       'heap': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 2)}}
    heap = capacity >= WIDE_HEAP_CAPACITY
    if family == 'writes_wide_string_at_0x28_to_sink':
        record[0x28:0x48] = wide_string_bytes(seed + 3, capacity)
        if heap:
            case['memory'].append({'addr': 'buf:record+0x28', 'pointer': 'heap'})
    elif family == 'writes_wide_string_through_0x20_to_sink':
        case['buffers']['string'] = {'size': 0x28, 'bytes': (bytes(8) + wide_string_bytes(seed + 3, capacity)).hex()}
        case['memory'].append({'addr': 'buf:record+0x20', 'pointer': 'string'})
        if heap:
            case['memory'].append({'addr': 'buf:string+0x8', 'pointer': 'heap'})
    else:
        case['memory'].append({'addr': 'buf:record+0x20', 'pointer': 'heap'})
    case['buffers']['record'] = {'size': 0x60, 'bytes': record.hex()}
    stubbed_table(case, 'record', 0, seed, {RECORD_VALUE: 1})
    stubbed_table(case, 'sink', 1, seed, {SINK_WRITE: 3})
    for i, reg in enumerate(ARGUMENT_REGISTERS[2:]):
        case['args'][reg] = hex((seed * 0x9e3779b97f4a7c15 + 0x0123456789abcdef * (i + 1)) & (2**64 - 1))
    case['args'].update({'rdi': 'buf:record', 'rsi': 'buf:sink', 'rax': hex(RAX_SEED)})
    return case


def callback_rewrite_case(address: str, family: str, seed: int, capacity: int) -> dict:
    """The record is its own sink, and its value callback (slot 0x60) swaps the record's table for
    another and points the text somewhere else: the write function and the text read before the
    call are the ones passed on, so a member that reads either again after the call differs."""
    case = sink_case(address, family, f'callback_rewrites_{capacity:x}_{seed}', seed, capacity)
    case['args']['rsi'] = 'buf:record'
    del case['buffers']['sink'], case['buffers']['sink_table']
    case['memory'] = [m for m in case['memory'] if not m['addr'].startswith('buf:sink')]
    case['stubs'] = [s for s in case['stubs'] if int(s['address'], 16) < RECORD_STUB_BASE + RECORD_TABLE_STRIDE]
    for stub in case['stubs']:
        if stub['address'] == record_stub(0, SINK_WRITE // 8):
            stub['argc'] = 3
    stubbed_table(case, 'swapped', 2, seed, {RECORD_VALUE: 1, SINK_WRITE: 3}, attach=False)
    case['buffers']['moved'] = {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 5)}
    writes = [{'arg': 0, 'offset': 0, 'pointer': 'swapped_table'}]
    if family == 'writes_wide_string_through_0x20_to_sink':
        case['buffers']['moved_string'] = {'size': 0x28, 'bytes': (bytes(8) + wide_string_bytes(seed + 6, capacity)).hex()}
        case['memory'].append({'addr': 'buf:moved_string+0x8', 'pointer': 'moved'})
        writes.append({'arg': 0, 'offset': 0x20, 'pointer': 'moved_string'})
    else:
        writes.append({'arg': 0, 'offset': 0x28 if family == 'writes_wide_string_at_0x28_to_sink' else 0x20,
                       'pointer': 'moved'})
    for stub in case['stubs']:
        if stub['address'] == record_stub(0, RECORD_VALUE // 8):
            stub['writes'] = writes
    return case


def sink_cases(address: str, family: str) -> list[dict]:
    """Two seeds; the wide strings at each capacity of WIDE_CAPACITIES; and per seed a value
    callback that rewrites the write function and the text (heap text, so its pointer is read)."""
    rewrites = [callback_rewrite_case(address, family, seed, WIDE_HEAP_CAPACITY) for seed in (19, 113)]
    if family == 'writes_text_at_0x20_to_sink':
        return [sink_case(address, family, f'text_{seed}', seed, 0) for seed in (19, 113)] + rewrites
    return [sink_case(address, family, f'capacity_{capacity:x}_{seed}', seed, capacity)
            for seed in (19, 113) for capacity in WIDE_CAPACITIES] + rewrites


def toggle_case(address: str, state_at: int, case_id: str, on: int, state: int, result: int) -> dict:
    """Switch ON (the second argument) with the state byte STATE; slot 0 returns RESULT (compared:
    the object and the second argument)."""
    case = base_case(address, case_id, 'i64')
    pattern = bytearray.fromhex(probe_bytes(29 + state_at))
    pattern[state_at] = state
    case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': pattern.hex()}}
    stubbed_table(case, 'object', 0, 29, {0: 2}, {0: result})
    case['args'] = {'rdi': 'buf:object', 'rsi': hex(on), 'rdx': '0x7fffffffffffffff', 'rax': hex(RAX_SEED)}
    return case


def toggle_cases(address: str, state_at: int) -> list[dict]:
    """On from off (and with only upper bits in rsi: still on, passed whole), on when already on
    (no call), on refused (a result with low bits: returned, state kept), a result with only upper
    bits (success: 0), off from on and from off (off always calls), and off refused."""
    return [toggle_case(address, state_at, 'on', 1, 0, 0),
            toggle_case(address, state_at, 'on_upper_bits', 1 << 32, 0, 0),
            toggle_case(address, state_at, 'already_on', 1, 1, 0),
            toggle_case(address, state_at, 'on_refused', 1, 0, 0x500000005),
            toggle_case(address, state_at, 'on_result_upper_bits', 1, 0, 0x500000000),
            toggle_case(address, state_at, 'off', 0, 1, 0),
            toggle_case(address, state_at, 'already_off', 0, 0, 0),
            toggle_case(address, state_at, 'off_refused', 0, 1, 9)]


FREE_SLOT = 0x70


def reset_case(address: str, string_at: int, case_id: str, seed: int, capacity: int) -> dict:
    """A wide string at STRING_AT with CAPACITY and its allocator after it (a table of stubs; slot
    0x70 compares the allocator and the heap text). Compared as void: memory and the calls."""
    case = base_case(address, case_id, 'void')
    pattern = bytearray.fromhex(probe_bytes(seed, 0x60))
    pattern[string_at:string_at + 0x20] = wide_string_bytes(seed + 1, capacity)
    case['buffers'] = {'object': {'size': 0x60, 'bytes': pattern.hex()},
                       'allocator': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 2)},
                       'heap': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 3)}}
    case['memory'].append({'addr': f'buf:object+0x{string_at + 0x20:x}', 'pointer': 'allocator'})
    if capacity >= WIDE_HEAP_CAPACITY:
        case['memory'].append({'addr': f'buf:object+0x{string_at:x}', 'pointer': 'heap'})
    stubbed_table(case, 'allocator', 0, seed, {FREE_SLOT: 2})
    case['args'] = {'rdi': 'buf:object', 'rax': hex(RAX_SEED)}
    return case


def reset_cases(address: str, string_at: int) -> list[dict]:
    return [reset_case(address, string_at, f'capacity_{capacity:x}', 37, capacity) for capacity in WIDE_CAPACITIES]


LIST_NODES = 3
NODE_TABLE_SLOTS = 4     # a node's table: slot 0 and its neighbours (the harness takes 64 stubs)
ARGUMENT_SEEDS = (0x0123456789abcdef, 0xfedcba9876543210, 0x00ff00ff00ff00ff, 0x8000000000000081)


def seeded_arguments(case: dict, seed: int) -> None:
    """rdx..r9 hold distinct integers."""
    for i, reg in enumerate(ARGUMENT_REGISTERS[2:]):
        case['args'][reg] = hex((seed * 0x9e3779b97f4a7c15 + ARGUMENT_SEEDS[i]) & (2**64 - 1))


def nth_node_case(address: str, case_id: str, index: int, nodes: int = LIST_NODES) -> dict:
    """A list of NODES nodes at +0x30 (each with its own small table of stubs comparing six
    arguments, so the wrong node is a different call) and the index INDEX in rsi."""
    case = base_case(address, case_id, 'i64')
    case['buffers'] = {'object': {'size': 0x40, 'bytes': probe_bytes(41, 0x40)}}
    for n in range(nodes):
        case['buffers'][f'node{n}'] = {'size': 0x18, 'bytes': probe_bytes(43 + n, 0x18)}
        case['memory'].append({'addr': 'buf:object+0x30' if n == 0 else f'buf:node{n - 1}+0x10',
                               'pointer': f'node{n}'})
        case['buffers'][f'node{n}_table'] = {'size': 8 * NODE_TABLE_SLOTS}
        case['memory'].append({'addr': f'buf:node{n}+0x0', 'pointer': f'node{n}_table'})
        for slot in range(NODE_TABLE_SLOTS):
            case['memory'].append({'addr': f'buf:node{n}_table+0x{8 * slot:x}', 'guest': record_stub(n, slot)})
            case['stubs'].append({'address': record_stub(n, slot), 'argc': 6, 'ret': 41 << 32 | n << 8 | slot})
    if nodes:
        case['memory'].append({'addr': f'buf:node{nodes - 1}+0x10', 'bytes': '00' * 8})
    else:
        case['memory'].append({'addr': 'buf:object+0x30', 'bytes': '00' * 8})
    seeded_arguments(case, 41)
    case['args'].update({'rdi': 'buf:object', 'rsi': hex(index), 'rax': hex(RAX_SEED)})
    return case


def nth_node_cases(address: str) -> list[dict]:
    """Each node, one past the end and the largest index (0), only upper bits besides the index
    (32 bits), and an empty list."""
    return ([nth_node_case(address, f'node_{i}', i) for i in range(LIST_NODES + 1)]
            + [nth_node_case(address, 'index_all_bits', 0xffffffff),
               nth_node_case(address, 'index_upper_bits', 1 << 32 | 1),
               nth_node_case(address, 'empty', 0, nodes=0)])


def own_table_case(address: str, case_id: str, seed: int, argc: dict[int, int], returns: str = 'i64',
                   size: int = 0x40) -> dict:
    """An object with a table of stubs at +0 and seeded argument registers."""
    case = base_case(address, case_id, returns)
    case['buffers'] = {'object': {'size': size, 'bytes': probe_bytes(seed, size)}}
    stubbed_table(case, 'object', 0, seed, argc)
    seeded_arguments(case, seed)
    case['args'].update({'rdi': 'buf:object', 'rsi': hex(seed * 0x1000193 | 1 << 40), 'rax': hex(RAX_SEED)})
    return case


def shifted_arguments_cases(address: str) -> list[dict]:
    """Slot 0x88 compares all six arguments; r9 has more than its low byte set."""
    return [own_table_case(address, f'seed_{seed}', seed, {0x88: 6}) for seed in (47, 149)]


def restart_cases(address: str) -> list[dict]:
    """Slots 0x78 (two arguments) and 0xa8 (one); the dword at +0x20 and the byte at +8 change."""
    return [own_table_case(address, f'seed_{seed}', seed, {0x78: 2, 0xa8: 1}) for seed in (53, 157)]


PENDING_HAS_WORK = 1
PENDING_FLUSHING = 2


def flush_case(address: str, case_id: str, flags: int = PENDING_HAS_WORK, count: int = 5,
               holder: bool = True, pending: bool = True) -> dict:
    """The object's +0x40 points at a holder whose first qword points at the pending object
    (FLAGS at +0xc, COUNT at +0x10, a table of stubs: 0x18 and 0x20 compare two arguments)."""
    case = base_case(address, case_id, 'void')
    case['buffers'] = {'object': {'size': 0x48, 'bytes': probe_bytes(59, 0x48)},
                       'holder': {'size': 8, 'bytes': probe_bytes(61, 8)}}
    pattern = bytearray.fromhex(probe_bytes(67, 0x18))
    pattern[0xc] = flags
    pattern[0x10:0x14] = count.to_bytes(4, 'little')
    case['buffers']['pending'] = {'size': 0x18, 'bytes': pattern.hex()}
    stubbed_table(case, 'pending', 0, 59, {0x18: 2, 0x20: 2})
    case['memory'].append({'addr': 'buf:object+0x40', 'pointer': 'holder'} if holder
                          else {'addr': 'buf:object+0x40', 'bytes': '00' * 8})
    case['memory'].append({'addr': 'buf:holder+0x0', 'pointer': 'pending'} if pending
                          else {'addr': 'buf:holder+0x0', 'bytes': '00' * 8})
    seeded_arguments(case, 59)
    case['args'].update({'rdi': 'buf:object', 'rsi': '0x8877665544332211', 'rax': hex(RAX_SEED)})
    return case


PENDING_REWRITTEN_FLAGS = 0xb5      # bit 1 clear, others mixed


def flush_rewrite_case(address: str, case_id: str, by_prepare: bool) -> dict:
    """The prepare callback (slot 0x18) swaps the pending object's table, so flush (slot 0x20) is
    the new table's; and the prepare or the flush callback rewrites the flags, which the member
    reads again to clear bit 1. A member that keeps the table or the flags from before the calls
    differs."""
    case = flush_case(address, case_id)
    stubbed_table(case, 'swapped', 1, 61, {0x18: 2, 0x20: 2}, attach=False)
    flags = {'arg': 0, 'offset': 0xc, 'bytes': f'{PENDING_REWRITTEN_FLAGS:02x}'}
    prepare, flush = record_stub(0, 0x18 // 8), record_stub(1, 0x20 // 8)
    for stub in case['stubs']:
        if stub['address'] == prepare:
            stub['writes'] = [{'arg': 0, 'offset': 0, 'pointer': 'swapped_table'}] + ([flags] if by_prepare else [])
        elif stub['address'] == flush and not by_prepare:
            stub['writes'] = [flags]
    return case


def flush_cases(address: str) -> list[dict]:
    """A flush (bit 0 alone, and every other flag bit but 1 set), and each reason not to: no
    holder, no pending object, no work bit, count 0, already flushing; and callbacks that swap
    the table and rewrite the flags (in the prepare, and in the flush)."""
    return [flush_rewrite_case(address, 'prepare_rewrites', True),
            flush_rewrite_case(address, 'flush_rewrites_flags', False),
            flush_case(address, 'flush'),
            flush_case(address, 'flush_other_flags', flags=0xfd, count=0x80000000),
            flush_case(address, 'no_holder', holder=False),
            flush_case(address, 'no_pending', pending=False),
            flush_case(address, 'no_work', flags=0xfc),
            flush_case(address, 'count_zero', count=0),
            flush_case(address, 'already_flushing', flags=PENDING_HAS_WORK | PENDING_FLUSHING)]


def returned_object_cases(address: str) -> list[dict]:
    """Slot 0x38 of the object (two arguments) returns another object with its own table; its
    slot 0x68 compares two arguments."""
    out = []
    for seed in (71, 163):
        case = own_table_case(address, f'seed_{seed}', seed, {0x38: 2})
        case['buffers']['inner'] = {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)}
        stubbed_table(case, 'inner', 1, seed, {0x68: 2})
        for stub in case['stubs']:
            if stub['address'] == record_stub(0, 0x38 // 8):
                stub['ret'] = 'buf:inner'
        out.append(case)
    return out


# Tree erases: a tree of five nodes (n0 over n1 and n2, n1's right child n3, n2's left child n4)
# and the nil node. The allocator's slot 0x70 compares the allocator and the node, so the order
# of the frees shows.
TREE_NODES = {'n0': ('n1', 'n2'), 'n1': ('nil', 'n3'), 'n2': ('n4', 'nil'), 'n3': ('nil', 'nil'),
              'n4': ('nil', 'nil')}
NODE_SIZE = 0x50
NODE_IS_NIL = 0x19
NODE_STRING = 0x28
NODE_CAPACITIES = {'n0': 7, 'n1': 8, 'n2': 1 << 63, 'n3': 7, 'n4': 8}


def tree_case(address: str, case_id: str, start: str, strings: bool) -> dict:
    """Erase from node START; with STRINGS each node holds a wide string at +0x28 (capacities
    from NODE_CAPACITIES, the heap ones pointing at their own text) with a second allocator
    after it. Compared as void: the frees, in order."""
    case = base_case(address, case_id, 'void')
    case['buffers'] = {'tree': {'size': 0x20, 'bytes': probe_bytes(73, 0x20)},
                       'alloc': {'size': PROBE_SIZE, 'bytes': probe_bytes(79)}}
    nil = bytearray.fromhex(probe_bytes(83, NODE_SIZE))
    nil[NODE_IS_NIL] = 1
    case['buffers']['nil'] = {'size': NODE_SIZE, 'bytes': nil.hex()}
    for i, (node, (left, right)) in enumerate(TREE_NODES.items()):
        pattern = bytearray.fromhex(probe_bytes(89 + i, NODE_SIZE))
        pattern[NODE_IS_NIL] = 0
        if strings:
            pattern[NODE_STRING:NODE_STRING + 0x20] = wide_string_bytes(97 + i, NODE_CAPACITIES[node])
            if NODE_CAPACITIES[node] >= WIDE_HEAP_CAPACITY:
                case['buffers'][f'{node}_text'] = {'size': PROBE_SIZE, 'bytes': probe_bytes(101 + i)}
                case['memory'].append({'addr': f'buf:{node}+0x{NODE_STRING:x}', 'pointer': f'{node}_text'})
            case['memory'].append({'addr': f'buf:{node}+0x{NODE_STRING + 0x20:x}', 'pointer': 'strings'})
        case['buffers'][node] = {'size': NODE_SIZE, 'bytes': pattern.hex()}
        case['memory'] += [{'addr': f'buf:{node}+0x0', 'pointer': left},
                           {'addr': f'buf:{node}+0x10', 'pointer': right}]
    case['memory'].append({'addr': 'buf:tree+0x18', 'pointer': 'alloc'})
    stubbed_table(case, 'alloc', 0, 73, {FREE_SLOT: 2})
    if strings:
        case['buffers']['strings'] = {'size': PROBE_SIZE, 'bytes': probe_bytes(107)}
        stubbed_table(case, 'strings', 1, 73, {FREE_SLOT: 2})
    # A stub entry scripts one call: slot 0x70 gets one entry per node freed (and per heap text).
    freed = subtree(start)
    frees = {record_stub(0, FREE_SLOT // 8): len(freed),
             record_stub(1, FREE_SLOT // 8): sum(NODE_CAPACITIES[n] >= WIDE_HEAP_CAPACITY for n in freed)}
    for stub in list(case['stubs']):
        for _ in range(frees.get(stub['address'], 1) - 1):
            case['stubs'].append(dict(stub))
    case['args'] = {'rdi': 'buf:tree', 'rsi': f'buf:{start}', 'rax': hex(RAX_SEED)}
    return case


def subtree(node: str) -> list[str]:
    if node == 'nil':
        return []
    left, right = TREE_NODES[node]
    return [node] + subtree(left) + subtree(right)


def tree_cases(address: str, strings: bool) -> list[dict]:
    """The whole tree, a leaf, a node with only a left child, and the nil node (no free)."""
    return [tree_case(address, f'from_{start}', start, strings) for start in ('n0', 'n3', 'n2', 'nil')]


def cases(family: str, address: str, value: int | None) -> list[dict]:
    if family not in CASES:
        raise KeyError(f'no case generator for family {family}')
    return CASES[family](address, value)


# The far field families are the field getters and setters with a 32-bit displacement (an offset
# of 0x80 or more): the same cases, with the object sized for the offset, and the same mutants.
FAR_FIELD_FAMILIES = {
    'reads_far_qword_field': 'reads_qword_field',
    'reads_far_dword_field': 'reads_dword_field',
    'reads_far_word_field': 'reads_word_field',
    'reads_far_byte_field': 'reads_byte_field',
    'far_field_address': 'field_address',
    'writes_far_qword_field': 'writes_qword_field',
    'writes_far_dword_field': 'writes_dword_field',
    'writes_far_word_field': 'writes_word_field',
    'writes_far_byte_field': 'writes_byte_field',
}
for far, near in FAR_FIELD_FAMILIES.items():
    CASES[far] = CASES[near]
    MUTANTS[far] = MUTANTS[near]
