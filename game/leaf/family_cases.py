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


def returns_nothing(address: str, value: int | None) -> list[dict]:
    # `ret` keeps rax too, so the full register is compared (i64), not only the void contract.
    return untouched_state_cases(address, 'i64') + [base_case(address, 'void_contract', 'void')]


def returns_a_value(address: str, value: int | None) -> list[dict]:
    """A body that writes all of rax and nothing else (returns_zero, returns_constant and the
    argument-returning families): rax enters with a seed no u32 constant equals, so a body that
    leaves rax or writes only part of it fails; rdi and rsi point at different probe slots (and
    hold different integers), which tells the first argument from the second."""
    return untouched_state_cases(address, 'i64')


OBJECT_SIZE = 0xa0     # room for the largest positive 8-bit offset plus a qword, from either start


def field_cases(address: str, value: int | None) -> list[dict]:
    """A getter of the field at offset VALUE (0 for the first-field families): rdi points at an
    object buffer of distinct bytes, so a read at any other offset or width returns another
    value; the object starts at the buffer and 16 bytes into it, with two byte patterns. The
    other arguments point into a probe buffer and are compared after the call; rax is seeded."""
    out = []
    for case_id, seed, start in (('object_a', 3, 0), ('object_b', 101, 0), ('object_shifted', 57, 0x10)):
        case = base_case(address, case_id, 'i64')
        case['buffers'] = {'object': {'size': OBJECT_SIZE, 'bytes': probe_bytes(seed, OBJECT_SIZE)},
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
    out = []
    for case_id, seed, start in (('object_a', 5, 0), ('object_b', 133, 0), ('object_shifted', 71, 0x10)):
        pattern = bytes.fromhex(probe_bytes(seed, OBJECT_SIZE))
        field = pattern[start + offset:start + offset + 8]
        case = base_case(address, case_id, 'i64')
        case['buffers'] = {'object': {'size': OBJECT_SIZE, 'bytes': pattern.hex()},
                           'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)}}
        case['args'] = {reg: f'buf:probe+0x{8 * i:x}' for i, reg in enumerate(ARGUMENT_REGISTERS)}
        case['args']['rdi'] = f'buf:object+0x{start:x}'
        case['args']['rsi'] = hex(int.from_bytes(bytes(b ^ 0xff for b in field), 'little'))
        case['args']['rax'] = hex(RAX_SEED)
        case['observe_registers'] = list(ARGUMENT_REGISTERS)
        out.append(case)
    return out


XMM0_SEED = '1e1fe00f3cc3a55a8899aabbccddeeff'
XMM1_SEED = '00112233445566778899aabbccddeeff'

# Forwarding thunks (game/leaf/virtual_call.h): slot i of fake table t is a stub at its own
# address, so a call through the wrong slot or the wrong object's table is a different call.
STUB_BASE = 0x00500400
STUB_TABLE_STRIDE = 0x400
STUB_SLOT_STRIDE = 0x10
TABLE_SLOTS = 0x10       # room for every positive 8-bit slot offset
FORWARDED_ARGUMENTS = 6
CONSTANT_SLOT = 0x68     # the slot of the two offset-argument thunk families
CONSTANT_CALL_SLOT = 0x40
BASE_OFFSET = 0x20       # where the base thunks' base lies in the whole object
WORD_MASK = 2**64 - 1


def stub_address(table: int, slot: int) -> str:
    return f'0x{STUB_BASE + STUB_TABLE_STRIDE * table + STUB_SLOT_STRIDE * slot:08x}'


def forward_case(address: str, case_id: str, seed: int, slot: int, member: int | None = None,
                 base_offset: int = 0, argc: int = FORWARDED_ARGUMENTS, second: str | None = None) -> dict:
    """A thunk that calls slot SLOT of a virtual table: its own object's, or that of the object
    the pointer MEMBER bytes into its object points at. rdi points BASE_OFFSET bytes into the
    object (the base thunks). Every slot of every table is a stub returning its own value, so the
    wrong slot or the wrong table is a different call; the stubs compare ARGC integer arguments
    and the float in xmm0, which enters seeded. rsi..r9 hold distinct integers (SECOND, a buffer
    address, replaces rsi)."""
    case = base_case(address, case_id, 'i64')
    case['buffers'] = {'object': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed)},
                       'own_table': {'size': 8 * TABLE_SLOTS},
                       'probe': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 1)}}
    case['memory'] = [{'addr': 'buf:object+0x0', 'pointer': 'own_table'}]
    tables = ['own_table']
    if member is not None:
        case['buffers'].update({'member': {'size': PROBE_SIZE, 'bytes': probe_bytes(seed + 2)},
                                'member_table': {'size': 8 * TABLE_SLOTS}})
        case['memory'] += [{'addr': f'buf:object+0x{member:x}', 'pointer': 'member'},
                           {'addr': 'buf:member+0x0', 'pointer': 'member_table'}]
        tables.append('member_table')
    for t, table in enumerate(tables):
        for i in range(TABLE_SLOTS):
            case['memory'].append({'addr': f'buf:{table}+0x{8 * i:x}', 'guest': stub_address(t, i)})
            case['stubs'].append({'address': stub_address(t, i), 'argc': argc, 'argf32': [0],
                                  'ret': (seed << 32 | t << 8 | i) & WORD_MASK})
    for i, reg in enumerate(ARGUMENT_REGISTERS[1:]):
        case['args'][reg] = hex((seed * 0x9e3779b97f4a7c15 + 0x0123456789abcdef * (i + 1)) & WORD_MASK)
    if second is not None:
        case['args']['rsi'] = second
    case['args'].update({'rdi': f'buf:object+0x{base_offset:x}', 'rax': hex(RAX_SEED), 'xmm0': XMM0_SEED})
    del slot  # the slot only names which stub the original calls; every slot is scripted
    return case


def forward_pair(address: str, slot: int, **layout) -> list[dict]:
    return [forward_case(address, 'objects_a', 7, slot, **layout),
            forward_case(address, 'objects_b', 151, slot, **layout)]


def own_slot_cases(address: str, value: int | None) -> list[dict]:
    """Slot VALUE of the object's own table, the other arguments as they came (the flag family's
    third argument enters as an integer other than 1)."""
    return forward_pair(address, value)


def offset_argument_cases(address: str, value: int | None) -> list[dict]:
    """Slot CONSTANT_SLOT of the object's own table with VALUE added to the second argument."""
    return forward_pair(address, CONSTANT_SLOT)


def base_offset_argument_cases(address: str, value: int | None) -> list[dict]:
    """The same from a base BASE_OFFSET bytes into the object: the callee gets the whole object."""
    return forward_pair(address, CONSTANT_SLOT, base_offset=BASE_OFFSET)


def member_slot_cases(address: str, value: int | None) -> list[dict]:
    """Slot VALUE of the object the pointer at +0x10 points at (its own table is another set of
    stubs)."""
    return forward_pair(address, value, member=0x10)


def member_dword_argument_cases(address: str, value: int | None) -> list[dict]:
    """Slot VALUE of the object at +8, with the dword rsi points at (in a probe buffer) as the
    second argument."""
    return forward_pair(address, value, member=8, second='buf:probe+0x8')


def member_constant_call_cases(address: str, value: int | None) -> list[dict]:
    """A call (not a tail call) of slot CONSTANT_CALL_SLOT of the object at +8 with the constant;
    the stubs compare the two arguments it passes."""
    return forward_pair(address, CONSTANT_CALL_SLOT, member=8, argc=2)


def exported_member_cases(address: str, value: int | None) -> list[dict]:
    """The pointer at +0x20 stored through rsi (into a probe buffer, compared), then slot 8 of the
    object it points at."""
    return forward_pair(address, 8, member=0x20, second='buf:probe+0x10')


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
    'reads_qword_field': field_cases,
    'reads_dword_field': field_cases,
    'reads_word_field': field_cases,
    'reads_byte_field': field_cases,
    'field_address': field_cases,
    'tail_calls_virtual_function': own_slot_cases,
    'tail_jumps_through_virtual_slot': own_slot_cases,
    'passes_flag_to_virtual_function': own_slot_cases,
    'passes_part_at_0xe8_to_virtual_0x68': offset_argument_cases,
    'adjusts_base_and_passes_part_at_0xe8': base_offset_argument_cases,
    'tail_calls_member_at_0x10': member_slot_cases,
    'passes_dword_argument_to_member_at_8': member_dword_argument_cases,
    'calls_member_at_8_with_constant': member_constant_call_cases,
    'exports_member_at_0x20_and_tail_calls_it': exported_member_cases,
    'reads_first_qword': field_cases,
    'reads_first_dword': field_cases,
    'writes_qword_field': setter_cases,
    'writes_dword_field': setter_cases,
    'writes_word_field': setter_cases,
    'writes_byte_field': setter_cases,
    'writes_first_qword': setter_cases,
    'writes_first_dword': setter_cases,
    'returns_first_argument': returns_a_value,
    'returns_second_argument': returns_a_value,
    'returns_float_zero': returns_float_zero,
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
    # Field getters: the neighbouring offset's replacement is the off-by-offset mutant.
    'reads_qword_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'reads_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'reads_word_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'reads_byte_field': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    'field_address': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument'],
    # Forwarding thunks: the neighbouring slot's (or constant's) replacement calls another stub;
    # a body that does not call returns the wrong value and records no call; the exact ones
    # meet the same call without the part offset or without the store.
    'tail_calls_virtual_function': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'tail_jumps_through_virtual_slot': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'passes_flag_to_virtual_function': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'passes_part_at_0xe8_to_virtual_0x68': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                            'bb_mutant_virtual_0x68_without_part'],
    'adjusts_base_and_passes_part_at_0xe8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                             'bb_mutant_base_virtual_0x68_without_part'],
    'tail_calls_member_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'passes_dword_argument_to_member_at_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_member_at_8_with_constant': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'exports_member_at_0x20_and_tail_calls_it': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                                 'bb_mutant_tail_calls_member_at_0x20_without_export'],
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
    'returns_first_argument': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_returns_second_argument'],
    'returns_second_argument': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_returns_first_argument'],
    'returns_float_zero': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_returns_float_one',
                           'bb_mutant_zeroes_low_float_only', 'bb_mutant_zeroes_xmm0_and_xmm1'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    if family not in CASES:
        raise KeyError(f'no case generator for family {family}')
    return CASES[family](address, value)
