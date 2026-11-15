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
    'reads_first_qword': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument',
                          'bb_mutant_reads_first_dword'],
    'reads_first_dword': ['bb_mutant_does_nothing', 'bb_mutant_returns_first_argument',
                          'bb_mutant_reads_first_qword'],
    # Setters: for the four {d8} families the neighbouring offset's replacement stores at the wrong
    # place; store-then-clear-eax shows a setter must leave rax.
    'writes_qword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    # Records that call virtual functions: no call, and the one rule each family keeps (the heap
    # text, the value, the state check, the free) left out.
    'writes_wide_string_at_0x28_to_sink': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_sink_gets_inline_text_at_0x28'],
    'writes_wide_string_through_0x20_to_sink': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_sink_gets_inline_text_through_0x20'],
    'writes_text_at_0x20_to_sink': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_sink_gets_text_without_value'],
    'toggles_flag_at_8_through_first_virtual': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_toggle_at_8_ignores_state'],
    'toggles_flag_at_0x20_through_first_virtual': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_toggle_at_0x20_ignores_state'],
    'resets_wide_string_at_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_resets_wide_string_at_8_without_free'],
    'resets_wide_string_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_resets_wide_string_at_0x10_without_free'],
    'tail_calls_slot_0_of_nth_node_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_calls_node_after_the_nth'],
    'shifts_arguments_into_virtual_0x88': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_virtual_0x88_keeps_second_argument'],
    'calls_0x78_stores_dword_at_0x20_calls_0xa8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_restarts_without_storing_dword'],
    'flushes_pending_through_member_at_0x40': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_flushes_while_flushing'],
    'tail_calls_0x68_of_what_0x38_returns': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_calls_0x68_of_object_itself'],
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
                  returns: dict[int, int] | None = None) -> None:
    """Buffer NAME gets a table of stubs at +0: slot offset K's stub compares argc[K] arguments
    (1 for the rest) and returns returns[K] (else its own value)."""
    case['buffers'][f'{name}_table'] = {'size': 8 * RECORD_TABLE_SLOTS}
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


def sink_cases(address: str, family: str) -> list[dict]:
    """Two seeds; the wide strings at each capacity of WIDE_CAPACITIES."""
    if family == 'writes_text_at_0x20_to_sink':
        return [sink_case(address, family, f'text_{seed}', seed, 0) for seed in (19, 113)]
    return [sink_case(address, family, f'capacity_{capacity:x}_{seed}', seed, capacity)
            for seed in (19, 113) for capacity in WIDE_CAPACITIES]


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


def flush_cases(address: str) -> list[dict]:
    """A flush (bit 0 alone, and every other flag bit but 1 set), and each reason not to: no
    holder, no pending object, no work bit, count 0, already flushing."""
    return [flush_case(address, 'flush'),
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


def cases(family: str, address: str, value: int | None) -> list[dict]:
    if family not in CASES:
        raise KeyError(f'no case generator for family {family}')
    return CASES[family](address, value)
