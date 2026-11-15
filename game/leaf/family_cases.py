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


# Delegate calls (game/leaf/delegates.cpp): a record holds an object pointer and a pointer to
# member function (Itanium C++ ABI: an address, or one plus a virtual slot's offset, then the
# adjustment added to the object). Each family's layout: the record offsets it reads, its guard,
# what it passes after the target, and which argument registers its call compares.
DELEGATE_LAYOUTS = {
    'calls_delegate_with_argument_field_0x10': {'object': 8, 'method': 16, 'value': 40, 'guard': 'method_object', 'argument': ('qword', 16), 'passes': ('argument', 'value'), 'compare': [0, 1, 2, 4, 5]},
    'calls_delegate_with_argument_field_8': {'object': 8, 'method': 16, 'value': 40, 'guard': 'method_object', 'argument': ('qword', 8), 'passes': ('argument', 'value'), 'compare': [0, 1, 2, 4, 5]},
    'calls_delegate_with_dword_at_0x20': {'object': 8, 'method': 16, 'value': 32, 'guard': 'method_object', 'passes': ('value', 'third'), 'compare': [0, 1, 4, 5]},
    'calls_delegate_at_0x18_with_dword_at_0x40': {'object': 24, 'method': 32, 'value': 64, 'guard': 'method', 'passes': ('second', 'value'), 'compare': [0, 1, 2, 4, 5]},
    'calls_flagged_delegate_with_dword_at_0x18': {'object': 16, 'method': 32, 'value': 24, 'guard': ('record_flag', 11, 64), 'passes': ('value', 'third'), 'compare': [0, 1, 4, 5]},
    'calls_delegate_with_part_of_argument_target': {'object': 8, 'method': 16, 'value': 32, 'guard': 'argument_target', 'argument': ('part', 8, 40), 'passes': ('argument', 'value'), 'compare': [0, 1, 2, 4, 5]},
    'forwards_to_delegate_at_0x10': {'object': 16, 'method': 24, 'passes': ('second', 'third'), 'compare': [0, 1, 2, 4, 5]},
    'forwards_to_delegate_at_0x18': {'object': 24, 'method': 32, 'passes': ('second', 'third'), 'compare': [0, 1, 2, 4, 5]},
    'forwards_to_delegate_at_0x20': {'object': 32, 'method': 40, 'passes': ('second', 'third'), 'compare': [0, 1, 2, 4, 5]},
    'calls_delegate_at_0x50_when_argument_flagged': {'object': 80, 'method': 88, 'value': 104, 'guard': ('argument_flag', 61, 16), 'passes': ('value', 'third'), 'compare': [0, 1, 4, 5]},
}
DELEGATE_RECORD_SIZE = 0x80
DELEGATE_OBJECT_SIZE = 0x40
DELEGATE_SLOTS = 8
DELEGATE_SLOT = 5                # the virtual slot the virtual cases call
DELEGATE_ADJUSTMENT = 0x10       # where the method's target lies in the object, virtual cases
DIRECT_ADJUSTMENT = 8            # the same for a plain function
DELEGATE_STUB_BASE = 0x00501000  # fake function addresses: virtual slot i at base + 0x10 * i
DELEGATE_FUNCTION = 0x00501800   # the plain function the direct cases call
DELEGATE_PART = 0x40             # the size of what a part argument points at


def delegate_stub(slot: int) -> str:
    return f'0x{DELEGATE_STUB_BASE + 0x10 * slot:08x}'


def delegate_case(address: str, family: str, case_id: str, seed: int, path: str) -> dict:
    """One call of a delegate family. PATH: 'virtual' (the method is slot DELEGATE_SLOT of the
    target's virtual table, the target 0x10 into the object), 'direct' (a plain function, the
    target 8 into the object), or a path the guard stops: 'no_method', 'no_object',
    'flag_clear' (every other bit of the flag byte set), 'no_argument', 'no_argument_target'.
    Every slot and the plain function are stubs returning their own values and comparing the
    registers the family's call passes; xmm0 enters seeded and its float is compared. A family
    with a guard returns nothing (on a guard exit the original leaves rax as it was, so the method
    has no result): its calls are compared as void too. An unguarded forwarder passes the
    stub's rax on (i64)."""
    layout = DELEGATE_LAYOUTS[family]
    calls = path in ('virtual', 'direct')
    case = base_case(address, case_id, 'i64' if calls and 'guard' not in layout else 'void')
    record = bytearray.fromhex(probe_bytes(seed, DELEGATE_RECORD_SIZE))
    obj, method = layout['object'], layout['method']
    record[obj:obj + 8] = bytes(8)
    adjustment = DIRECT_ADJUSTMENT if path == 'direct' else DELEGATE_ADJUSTMENT
    function = 0 if path in ('no_method', 'direct') else 8 * DELEGATE_SLOT + 1
    record[method:method + 16] = function.to_bytes(8, 'little') + adjustment.to_bytes(8, 'little')
    guard = layout.get('guard')
    if isinstance(guard, tuple) and guard[0] == 'record_flag':
        record[guard[1]] = 0xff & ~guard[2] if path == 'flag_clear' else 0xff
    case['buffers'] = {'record': {'size': DELEGATE_RECORD_SIZE, 'bytes': record.hex()},
                       'object': {'size': DELEGATE_OBJECT_SIZE, 'bytes': probe_bytes(seed + 2, DELEGATE_OBJECT_SIZE)},
                       'table': {'size': 8 * DELEGATE_SLOTS}}
    memory = case['memory']
    if path != 'no_object':
        memory.append({'addr': f'buf:record+0x{obj:x}', 'pointer': 'object'})
    memory.append({'addr': f'buf:object+0x{DELEGATE_ADJUSTMENT:x}', 'pointer': 'table'})
    if path == 'direct':
        memory.append({'addr': f'buf:record+0x{method:x}', 'guest': f'0x{DELEGATE_FUNCTION:08x}'})
    stub = {'argc': FORWARDED_DELEGATE_ARGUMENTS, 'argi': layout['compare'], 'argf32': [0]}
    for slot in range(DELEGATE_SLOTS):
        memory.append({'addr': f'buf:table+0x{8 * slot:x}', 'guest': delegate_stub(slot)})
        case['stubs'].append(dict(stub, address=delegate_stub(slot), ret=seed << 32 | slot))
    case['stubs'].append(dict(stub, address=f'0x{DELEGATE_FUNCTION:08x}', ret=seed << 32 | 0xff))
    for i, reg in enumerate(ARGUMENT_REGISTERS[1:]):
        case['args'][reg] = hex((seed * 0x9e3779b97f4a7c15 + 0x0123456789abcdef * (i + 1)) & (2**64 - 1))
    argument = layout.get('argument')
    if argument or (isinstance(guard, tuple) and guard[0] == 'argument_flag'):
        pattern = bytearray.fromhex(probe_bytes(seed + 1))
        if isinstance(guard, tuple) and guard[0] == 'argument_flag':
            pattern[guard[1]] = 0xff & ~guard[2] if path == 'flag_clear' else 0xff
        if argument and argument[0] == 'part':
            pattern[argument[1]:argument[1] + 8] = bytes(8)
            if path != 'no_argument_target':
                case['buffers']['part'] = {'size': DELEGATE_PART, 'bytes': probe_bytes(seed + 3, DELEGATE_PART)}
                memory.append({'addr': f'buf:argument+0x{argument[1]:x}', 'pointer': 'part'})
        case['buffers']['argument'] = {'size': PROBE_SIZE, 'bytes': pattern.hex()}
        case['args']['rsi'] = '0x0' if path == 'no_argument' else 'buf:argument'
    case['args'].update({'rdi': 'buf:record', 'rax': hex(RAX_SEED), 'xmm0': XMM0_SEED})
    return case


FORWARDED_DELEGATE_ARGUMENTS = 6
STOPPED_PATHS = {'method_object': ('no_method', 'no_object'), 'method': ('no_method',),
                 'argument_target': ('no_argument', 'no_argument_target', 'no_object')}


def delegate_cases(address: str, family: str) -> list[dict]:
    """The method as a virtual function (two seeds) and as a plain function, and every way the
    family's guard stops the call."""
    guard = DELEGATE_LAYOUTS[family].get('guard')
    stopped = ('flag_clear',) if isinstance(guard, tuple) else STOPPED_PATHS.get(guard, ())
    cases = [delegate_case(address, family, 'virtual_a', 13, 'virtual'),
             delegate_case(address, family, 'virtual_b', 89, 'virtual'),
             delegate_case(address, family, 'direct', 47, 'direct')]
    return cases + [delegate_case(address, family, path, 61, path) for path in stopped]


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
    'writes_dword_field': setter_cases,
    'writes_word_field': setter_cases,
    'calls_delegate_with_argument_field_0x10': lambda address, value: delegate_cases(address, 'calls_delegate_with_argument_field_0x10'),
    'calls_delegate_with_argument_field_8': lambda address, value: delegate_cases(address, 'calls_delegate_with_argument_field_8'),
    'calls_delegate_with_dword_at_0x20': lambda address, value: delegate_cases(address, 'calls_delegate_with_dword_at_0x20'),
    'calls_delegate_at_0x18_with_dword_at_0x40': lambda address, value: delegate_cases(address, 'calls_delegate_at_0x18_with_dword_at_0x40'),
    'calls_flagged_delegate_with_dword_at_0x18': lambda address, value: delegate_cases(address, 'calls_flagged_delegate_with_dword_at_0x18'),
    'calls_delegate_with_part_of_argument_target': lambda address, value: delegate_cases(address, 'calls_delegate_with_part_of_argument_target'),
    'forwards_to_delegate_at_0x10': lambda address, value: delegate_cases(address, 'forwards_to_delegate_at_0x10'),
    'forwards_to_delegate_at_0x18': lambda address, value: delegate_cases(address, 'forwards_to_delegate_at_0x18'),
    'forwards_to_delegate_at_0x20': lambda address, value: delegate_cases(address, 'forwards_to_delegate_at_0x20'),
    'calls_delegate_at_0x50_when_argument_flagged': lambda address, value: delegate_cases(address, 'calls_delegate_at_0x50_when_argument_flagged'),
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
    'writes_dword_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    'writes_word_field': ['bb_mutant_does_nothing', 'bb_mutant_writes_first_argument', 'bb_mutant_returns_zero'],
    # Delegate calls: no call, and the method called as a plain function pointer whatever the
    # guard, with the arguments as they came (a virtual method then jumps to an odd address).
    'calls_delegate_with_argument_field_0x10': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x8_0x10_called_directly'],
    'calls_delegate_with_argument_field_8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x8_0x10_called_directly'],
    'calls_delegate_with_dword_at_0x20': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x8_0x10_called_directly'],
    'calls_delegate_at_0x18_with_dword_at_0x40': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x18_0x20_called_directly'],
    'calls_flagged_delegate_with_dword_at_0x18': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x10_0x20_called_directly'],
    'calls_delegate_with_part_of_argument_target': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x8_0x10_called_directly'],
    'forwards_to_delegate_at_0x10': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x10_0x18_called_directly'],
    'forwards_to_delegate_at_0x18': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x18_0x20_called_directly'],
    'forwards_to_delegate_at_0x20': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x20_0x28_called_directly'],
    'calls_delegate_at_0x50_when_argument_flagged': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero', 'bb_mutant_delegate_0x50_0x58_called_directly'],
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
