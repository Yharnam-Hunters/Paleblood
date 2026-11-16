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


FIELD_FILL = 0x5a   # no stored constant (0, 1, all bits) has this byte


def constant_setter_cases(address: str, value: int | None) -> list[dict]:
    """A body that stores a fixed value in the field at offset VALUE (0 for the first-field
    families): rdi points at an object of distinct bytes whose eight bytes at the field are all
    FIELD_FILL, so the store shows in every byte it covers, and a store at another offset or of
    another width changes other bytes. rax is seeded (the body leaves it), the other arguments
    point into a probe buffer and the argument registers are compared."""
    offset = value or 0
    out = []
    for case_id, seed, start in (('object_a', 9, 0), ('object_b', 151, 0), ('object_shifted', 83, 0x10)):
        pattern = bytearray.fromhex(probe_bytes(seed, OBJECT_SIZE))
        pattern[start + offset:start + offset + 8] = bytes([FIELD_FILL]) * 8
        case = base_case(address, case_id, 'i64')
        case['buffers'] = {'object': {'size': OBJECT_SIZE, 'bytes': pattern.hex()},
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
    'writes_dword_field': setter_cases,
    'writes_word_field': setter_cases,
    'writes_byte_field': setter_cases,
    'writes_first_qword': setter_cases,
    'writes_first_dword': setter_cases,
    'stores_one_in_byte_field': constant_setter_cases,
    'clears_byte_field': constant_setter_cases,
    'clears_qword_field': constant_setter_cases,
    'clears_dword_field': constant_setter_cases,
    'stores_one_in_dword_field': constant_setter_cases,
    'stores_all_bits_in_dword_field': constant_setter_cases,
    'clears_first_qword': constant_setter_cases,
    'clears_first_dword': constant_setter_cases,
    'clears_first_byte': constant_setter_cases,
    'stores_one_in_first_byte': constant_setter_cases,
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
    'writes_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'writes_word_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'writes_byte_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'writes_first_qword': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_dword', 'bb_mutant_returns_zero',
                           'bb_mutant_writes_first_qword_clears_eax'],
    'writes_first_dword': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_qword', 'bb_mutant_returns_zero'],
    # Constant setters: the neighbouring offset's replacement stores at the wrong place; the
    # first-field ones also meet a store of the other constant or of the wrong width.
    'stores_one_in_byte_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'clears_byte_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'clears_qword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'clears_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'stores_one_in_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'stores_all_bits_in_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'clears_first_qword': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_clears_first_dword'],
    'clears_first_dword': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_clears_first_qword',
                           'bb_mutant_clears_first_byte'],
    'clears_first_byte': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_stores_one_in_first_byte',
                          'bb_mutant_clears_first_dword'],
    'stores_one_in_first_byte': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_clears_first_byte',
                                 'bb_mutant_stores_one_in_first_dword'],
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
