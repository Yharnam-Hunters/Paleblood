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
         'thunk_minus_0x20_destroys_with_0x005696e0_then_frees': adjusting_free_cases,
         'queries_interface_0x04b2f0f0_or_0x04b2f320': interface_query_cases,
         'copies_entry_found_by_key_at_0x30': entry_copy_cases,
         'stores_entry_by_key_at_0x30': entry_store_cases,
         'frees_layout_block_align_16': layout_free_cases,
         'frees_layout_block_align_4': layout_free_cases,
         'allocates_layout_block_for_stored_count': layout_allocate_cases(False),
         'allocates_layout_block_for_count': layout_allocate_cases(True)}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    'thunk_minus_0x20_destroys_with_0x005696e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'queries_interface_0x04b2f0f0_or_0x04b2f320': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'copies_entry_found_by_key_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'stores_entry_by_key_at_0x30': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'frees_layout_block_align_16': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'frees_layout_block_align_4': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'allocates_layout_block_for_stored_count': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'allocates_layout_block_for_count': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
