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


STACK_GUARD_SLOT = '0x057e3be0'   # the import slot that points at the stack guard
HANDLER = '0x00400140'             # a stand-in for the delegate's function, stubbed
EVENT_SIZE = 0x10
HANDLER_SLOT = 0x20                # the virtual delegate's table offset


def delegate_case(address: str, case_id: str, function: str | None, seed: int) -> dict:
    """An object with a delegate at +0x50: FUNCTION None (unset), 'direct' (the handler's
    address) or 'virtual' (1 + HANDLER_SLOT into the bound object's table). The bound object
    is the target buffer plus an adjustment of 0x10. The handler compares the object, the
    context and the 16-byte event its second argument points at (on the stack)."""
    poster = bytearray((seed * 13 + i) & 0xff for i in range(0x70))
    poster[0x58:0x60] = bytes(8)
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'guard': {'size': 8, 'bytes': struct.pack('<Q', 0x2b992ddfa232 ^ seed).hex()},
                        'poster': {'size': 0x70, 'bytes': poster.hex()},
                        'target': {'size': 0x30, 'bytes': bytes((seed + i) & 0xff for i in range(0x30)).hex()},
                        'target_table': {'size': 0x30}},
            'memory': [{'addr': STACK_GUARD_SLOT, 'pointer': 'guard'}],
            'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:poster', 'rsi': hex(0xa5a5a5a5_00000000 | seed * 0x01010101),
                     'rdx': hex(0x0123456789abcdef ^ seed << 56), 'rcx': '0x7', 'rax': '0x5aa5c33c0ff0e11e'}}
    if function:
        case['memory'] += [{'addr': 'buf:poster+0x50', 'pointer': 'target'},
                           {'addr': 'buf:poster+0x60', 'bytes': struct.pack('<q', 0x10).hex()},
                           {'addr': 'buf:poster+0x68', 'bytes': struct.pack('<I', 0x900d0000 | seed).hex()},
                           {'addr': 'buf:target+0x10', 'pointer': 'target_table'},
                           {'addr': f'buf:target_table+0x{HANDLER_SLOT:x}', 'guest': HANDLER}]
        if function == 'direct':
            case['memory'].append({'addr': 'buf:poster+0x58', 'guest': HANDLER})
        else:
            case['memory'].append({'addr': 'buf:poster+0x58', 'bytes': struct.pack('<Q', HANDLER_SLOT + 1).hex()})
        case['stubs'].append({'address': HANDLER, 'argc': 3, 'argi': [0, 2], 'ret': 0x77,
                              'argmem': [{'arg': 1, 'size': EVENT_SIZE}]})
    return case


def delegate_event_cases(address: str, value: int | None) -> list[dict]:
    """No delegate (no call), a direct handler and a virtual one, with two seeds."""
    return [delegate_case(address, 'unset', None, 3),
            delegate_case(address, 'direct', 'direct', 5),
            delegate_case(address, 'virtual', 'virtual', 7),
            delegate_case(address, 'direct_other', 'direct', 11)]


FALLBACK = '0x00400180'            # stand-ins for the argument's slot 0x80 and the delegate, stubbed
FORWARDED_HANDLER = '0x004001c0'


def forward_case(address: str, case_id: str, function: str | None, seed: int) -> dict:
    """An owner with a delegate at +0x18 (FUNCTION as in delegate_case; the bound object is the
    target plus 0x10) and a second argument with a table whose slot 0x80 is a stub (comparing it
    and the address it gets). The handler compares the bound object, the argument and the
    context."""
    owner = bytearray((seed * 17 + i) & 0xff for i in range(0x48))
    owner[0x30:0x38] = bytes(8)
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'i64',
            'buffers': {'owner': {'size': 0x48, 'bytes': owner.hex()},
                        'argument': {'size': 0x20, 'bytes': bytes((seed * 3 + i) & 0xff for i in range(0x20)).hex()},
                        'argument_table': {'size': 0x90},
                        'target': {'size': 0x30, 'bytes': bytes((seed + i) & 0xff for i in range(0x30)).hex()},
                        'target_table': {'size': 0x30}},
            'memory': [{'addr': 'buf:argument+0x0', 'pointer': 'argument_table'},
                       {'addr': 'buf:argument_table+0x80', 'guest': FALLBACK}],
            'imports': [],
            'stubs': [{'address': FALLBACK, 'argc': 2, 'ret': seed << 32 | 0x80}],
            'args': {'rdi': 'buf:owner', 'rsi': 'buf:argument', 'rdx': hex(0x0123456789abcdef ^ seed << 56),
                     'rax': '0x5aa5c33c0ff0e11e'}}
    if function:
        case['memory'] += [{'addr': 'buf:owner+0x18', 'pointer': 'target'},
                           {'addr': 'buf:owner+0x38', 'bytes': struct.pack('<q', 0x10).hex()},
                           {'addr': 'buf:owner+0x40', 'bytes': struct.pack('<I', 0x900d0000 | seed).hex()},
                           {'addr': 'buf:target+0x10', 'pointer': 'target_table'},
                           {'addr': f'buf:target_table+0x{HANDLER_SLOT:x}', 'guest': FORWARDED_HANDLER}]
        if function == 'direct':
            case['memory'].append({'addr': 'buf:owner+0x30', 'guest': FORWARDED_HANDLER})
        else:
            case['memory'].append({'addr': 'buf:owner+0x30', 'bytes': struct.pack('<Q', HANDLER_SLOT + 1).hex()})
        case['stubs'].append({'address': FORWARDED_HANDLER, 'argc': 3, 'ret': seed << 32 | 0x18})
    return case


def delegate_forward_cases(address: str, value: int | None) -> list[dict]:
    """No delegate (slot 0x80 of the argument), a direct handler and a virtual one."""
    return [forward_case(address, 'unset', None, 3),
            forward_case(address, 'direct', 'direct', 5),
            forward_case(address, 'virtual', 'virtual', 7),
            forward_case(address, 'unset_other', None, 11)]


def destructor_cases(address: str, value: int | None) -> list[dict]:
    """The thread allocator as in thread_free_case; the object's first qword (the vtable store)
    is compared with the memory, and the free gets 0x28 whatever the header says."""
    out = []
    for case_id, header, seed in (('header_own_size', 0x0030_0001, 17), ('header_default', SIZE_DEFAULT << 16, 19)):
        case = thread_free_case(address, case_id, header, seed)
        out.append(case)
    return out


WCSLEN = '0x02fbf228'
WIDE_STRING_ASSIGN = '0x02a2a310'


def text_event_case(address: str, case_id: str, text: str, function: str | None, seed: int) -> dict:
    """The poster's +0x48 points at TEXT (UTF-16, terminated); wcslen (when the text is not
    empty) and the wide string assign are stubs comparing the text; the delegate is as in
    delegate_case. The handler compares the bound object, the context and the event's kind and
    data (its middle dword is unset in the originals)."""
    case = delegate_case(address, case_id, function, seed)
    encoded = (text + '\0').encode('utf-16-le')
    case['buffers']['text'] = {'size': len(encoded), 'bytes': encoded.hex()}
    case['buffers']['string'] = {'size': 0x38, 'bytes': bytes((seed * 5 + i) & 0xff for i in range(0x38)).hex()}
    case['memory'].append({'addr': 'buf:poster+0x48', 'pointer': 'text'})
    case['args']['rsi'] = 'buf:string'
    stubs = []
    if text:
        stubs.append({'address': WCSLEN, 'argc': 1, 'ret': len(text),
                      'argmem': [{'arg': 0, 'size': len(encoded)}]})
    stubs.append({'address': WIDE_STRING_ASSIGN, 'argc': 3, 'ret': 0,
                  'argmem': [{'arg': 1, 'size': len(encoded)}]})
    for stub in case['stubs']:
        stub['argmem'] = [{'arg': 1, 'size': 4}, {'arg': 1, 'offset': 8, 'size': 8}]
    case['stubs'] = stubs + case['stubs']
    return case


def text_event_cases(address: str, value: int | None) -> list[dict]:
    """Text and empty text (no wcslen), each with the delegate unset, direct and virtual."""
    return [text_event_case(address, f'{name}_{function or "unset"}', text, function, seed)
            for name, text in (('text', 'Hunter'), ('empty', ''))
            for function, seed in ((None, 23), ('direct', 29), ('virtual', 31))]


CONSTRUCT_EVENT_BASE = '0x01400810'
DESTROY_EVENT = '0x013a75d0'
def record_event_compared(arg: int) -> list[dict]:
    return [{'arg': arg, 'size': 8}, {'arg': arg, 'offset': 0x20, 'size': 0x10}]


def record_event_case(address: str, case_id: str, record: str, function: str | None, seed: int) -> dict:
    """A poster with a delegate at +8 (object +8, function +0x10, adjustment +0x18, context +0x20):
    FUNCTION None leaves the bound object null; 'direct' or 'virtual' as in delegate_case. RECORD:
    'none' (a null second argument), 'no_second', 'no_first' or 'full'. The event's constructor,
    the handler and the event's destructor are stubs; the event's bytes the base constructor would
    set (+8..+0x20) are not compared, as the stub does not set them."""
    poster = bytearray((seed * 13 + i) & 0xff for i in range(0x30))
    poster[0x8:0x10] = bytes(8)
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'guard': {'size': 8, 'bytes': struct.pack('<Q', 0x2b992ddfa232 ^ seed).hex()},
                        'poster': {'size': 0x30, 'bytes': poster.hex()},
                        'record': {'size': 0x18, 'bytes': bytes((seed * 5 + i) & 0xff for i in range(0x18)).hex()},
                        'first': {'size': 0x10}, 'second': {'size': 0x10},
                        'target': {'size': 0x30, 'bytes': bytes((seed + i) & 0xff for i in range(0x30)).hex()},
                        'target_table': {'size': 0x30}},
            'memory': [{'addr': STACK_GUARD_SLOT, 'pointer': 'guard'}],
            'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:poster', 'rsi': '0x0' if record == 'none' else 'buf:record',
                     'rdx': '0x2222222222222222', 'rax': '0x5aa5c33c0ff0e11e'}}
    case['memory'] += [{'addr': 'buf:record+0x0', 'bytes': '00' * 8} if record == 'no_first'
                       else {'addr': 'buf:record+0x0', 'pointer': 'first'},
                       {'addr': 'buf:record+0x10', 'bytes': '00' * 8} if record == 'no_second'
                       else {'addr': 'buf:record+0x10', 'pointer': 'second'}]
    if function:
        case['memory'] += [{'addr': 'buf:poster+0x8', 'pointer': 'target'},
                           {'addr': 'buf:poster+0x18', 'bytes': struct.pack('<q', 0x10).hex()},
                           {'addr': 'buf:poster+0x20', 'bytes': struct.pack('<I', 0x900d0000 | seed).hex()},
                           {'addr': 'buf:target+0x10', 'pointer': 'target_table'},
                           {'addr': f'buf:target_table+0x{HANDLER_SLOT:x}', 'guest': HANDLER}]
        if function == 'direct':
            case['memory'].append({'addr': 'buf:poster+0x10', 'guest': HANDLER})
        else:
            case['memory'].append({'addr': 'buf:poster+0x10', 'bytes': struct.pack('<Q', HANDLER_SLOT + 1).hex()})
        if record == 'full':
            case['stubs'] = [{'address': CONSTRUCT_EVENT_BASE, 'argc': 1, 'argi': [], 'ret': 0},
                             {'address': HANDLER, 'argc': 3, 'argi': [0, 2], 'ret': 0x77,
                              'argmem': record_event_compared(1)},
                             {'address': DESTROY_EVENT, 'argc': 1, 'argi': [], 'ret': 0, 'argmem': record_event_compared(0)}]
    return case


def record_event_cases(address: str, value: int | None) -> list[dict]:
    """A full record with a direct and a virtual delegate; no bound object; and the record null,
    without its +0x10 and without its +0."""
    return [record_event_case(address, 'direct', 'full', 'direct', 3),
            record_event_case(address, 'virtual', 'full', 'virtual', 5),
            record_event_case(address, 'no_object', 'full', None, 7),
            record_event_case(address, 'no_record', 'none', 'direct', 11),
            record_event_case(address, 'no_second', 'no_second', 'direct', 13),
            record_event_case(address, 'no_first', 'no_first', 'virtual', 17)]


CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'posts_record_event_to_delegate_at_0x8': record_event_cases,
         'deleting_destructor_freeing_0x28_bytes': destructor_cases,
         'posts_text_event_to_delegate_at_0x50': text_event_cases,
         'calls_delegate_at_0x18_else_virtual_0x80': delegate_forward_cases,
         'posts_event_to_delegate_at_0x50': delegate_event_cases,
         'frees_object_through_thread_allocator': thread_free_cases}

MUTANTS = {
    # Doing nothing leaves the string and the guard as they were and makes none of the calls; only
    # publishing the guard skips the allocator, the string and the registration on a clear guard,
    # and overwrites a set guard's other bytes.
    'initialize_static_wide_string': ['bb_mutant_does_nothing', 'bb_mutant_static_wide_string_guard_only'],
    # No call (another rax, no recorded free), and the neighbouring class size's replacement.
    'frees_object_through_thread_allocator': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    # No call, and the other event kind's replacement.
    'posts_event_to_delegate_at_0x50': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    # No call; and the delegate called without its adjustment (slot 0x80 given 0).
    'calls_delegate_at_0x18_else_virtual_0x80': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                                 'bb_mutant_delegate_at_0x18_without_adjustment'],
    # No write and no call. (A mutant that gets the vtable or the thread allocator wrong would need
    # the game image, which the mutant library does not see.)
    'deleting_destructor_freeing_0x28_bytes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'posts_text_event_to_delegate_at_0x50': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'posts_record_event_to_delegate_at_0x8': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
