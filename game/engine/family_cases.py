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
"""
from __future__ import annotations

import copy
import importlib.util
import os

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


def string_owner_cases(address: str, value: int | None) -> list[dict]:
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


def sized_free_cases(address: str, value: int | None) -> list[dict]:
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


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'destroys_second_argument_then_frees_it_to_owning_heap': owning_heap_cases,
         'stores_vtable_0x05541c80_releases_members_0x10_and_0x8': counted_holder_cases(True, False),
         'stores_vtable_0x05541c80_releases_members_then_deletes': counted_holder_cases(True, True),
         'stores_vtable_0x05541b90_releases_member_at_8_then_deletes': counted_holder_cases(False, True),
         'frees_string_at_0x10_through_allocator_at_0x30_then_deletes': string_owner_cases,
         'stores_minus_2_at_0x10_through_setter_then_returns_zero': setter_cases,
         'calls_0x022dc040_then_sets_byte_0x60': flag_cases,
         'frees_0x10_bytes_through_0x05ac8288': sized_free_cases}

MUTANTS = {
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
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
