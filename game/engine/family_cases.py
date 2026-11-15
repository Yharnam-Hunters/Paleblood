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
import struct
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


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'frees_object_through_thread_allocator': thread_free_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # No call (another rax, no recorded free), and the neighbouring class size's replacement.
    'frees_object_through_thread_allocator': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
