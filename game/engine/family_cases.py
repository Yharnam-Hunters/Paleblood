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


def sized_delete_cases(address: str, value: int | None) -> list[dict]:
    """The thread allocator as in thread_free_case: two objects (the free gets the size K, whatever
    the object says), and a null pointer (no call)."""
    out = []
    for case_id, seed in (('object', 3), ('other_object', 5), ('null', 7)):
        case = thread_free_case(address, case_id, SIZE_DEFAULT << 16 | seed, seed)
        case['returns'] = 'void'
        if case_id == 'null':
            case['args']['rdi'] = '0x0'
        out.append(case)
    return out


ALLOCATE = '0x00400020'          # a stand-in for the allocator's slot 0x10, stubbed


def allocate_cases(address: str, value: int | None) -> list[dict]:
    """The thread allocator as in thread_free_case, its slot 0x10 a stub comparing the allocator
    and the size and returning the allocation."""
    out = []
    for case_id, seed in (('allocate', 3), ('allocate_other', 5)):
        case = thread_free_case(address, case_id, 0, seed)
        case['memory'].append({'addr': 'buf:allocator_vt+0x10', 'guest': ALLOCATE})
        case['stubs'] = [{'address': ALLOCATE, 'argc': 2, 'ret': 0x7f00_0000_0000 | seed << 12}]
        case['args']['rdi'] = hex(0x0101010101010101 * seed)
        out.append(case)
    return out






SHARED_DESTRUCTOR = '0x024b5a60'

OPERATOR_DELETE = '0x02fbe588'

VIRTUAL_DESTRUCTOR = '0x00400240'
   # a stand-in for the object's slot 0x28, stubbed
DESTRUCTOR_SLOT = 0x28



def object_buffer(seed: int, size: int = 0x40) -> dict:
    return {'size': size, 'bytes': bytes((seed * 11 + i) & 0xff for i in range(size)).hex()}



OBJECT_ALLOCATOR = '0x057e7320'
    # the global that points at the allocator
ALLOCATOR_FREE = '0x00400280'
      # a stand-in for the allocator's slot 0x68, stubbed


def destroy_then_delete_cases(destructor: str | None, freed: bool = False):
    """The destructor, then operator delete (or, FREED, slot 0x68 of the allocator the global
    points at, comparing the allocator and the object), all stubs comparing the object; two
    seeds (the object's contents must come through untouched). DESTRUCTOR None: an empty
    function, run unstubbed."""
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for seed in (3, 5):
            case = {'schema': 1, 'address': address, 'id': f'object_{seed}', 'returns': 'i64' if freed else 'void',
                    'buffers': {'object': object_buffer(seed)}, 'memory': [], 'imports': [],
                    'stubs': [{'address': destructor, 'argc': 1, 'ret': seed}] if destructor else [],
                    'args': {'rdi': 'buf:object', 'rsi': hex(0x1111111111111111 * (seed & 0xf)),
                             'rax': '0x5aa5c33c0ff0e11e'}}
            if freed:
                case['buffers'].update({'allocator': object_buffer(seed + 1, 0x20), 'allocator_table': {'size': 0x80}})
                case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                                   {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                                   {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
                case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
            else:
                case['stubs'].append({'address': OPERATOR_DELETE, 'argc': 1, 'ret': 0})
            out.append(case)
        return out
    return cases



destroys_then_deletes_cases = destroy_then_delete_cases(SHARED_DESTRUCTOR)



def virtual_delete_case(case_id: str, address: str, flag: int, seed: int) -> dict:
    """An object whose table's slot 0x28 is a stub comparing the object; FLAG is the whole second
    argument (only its low byte decides operator delete, a stub comparing the object)."""
    stubs = [{'address': VIRTUAL_DESTRUCTOR, 'argc': 1, 'ret': seed << 8}]
    if flag & 0xff:
        stubs.append({'address': OPERATOR_DELETE, 'argc': 1, 'ret': 0})
    return {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'object': object_buffer(seed), 'table': {'size': 0x40}},
            'memory': [{'addr': 'buf:object+0x0', 'pointer': 'table'},
                       {'addr': f'buf:table+0x{DESTRUCTOR_SLOT:x}', 'guest': VIRTUAL_DESTRUCTOR}],
            'imports': [], 'stubs': stubs,
            'args': {'rdi': 'buf:object', 'rsi': hex(flag), 'rdx': '0x2222222222222222',
                     'rax': '0x5aa5c33c0ff0e11e'}}



def virtual_delete_cases(address: str, value: int | None) -> list[dict]:
    """The flag 1, 0, 0x100 (a set bit above the low byte: no delete) and 0xffffff80."""
    return [virtual_delete_case('delete', address, 1, 3),
            virtual_delete_case('keep', address, 0, 5),
            virtual_delete_case('keep_high_bits', address, 0x100, 7),
            virtual_delete_case('delete_high_bits', address, 0xffffff80, 11)]





RELEASE_COUNTED = '0x027ee1e0'

DESTROY_BASE = '0x006f92f0'
      # a stand-in for the allocator's slot 0x68, stubbed


def member_release_case(case_id: str, address: str, member: str | int, count: int | None, seed: int,
                        freed: bool = False) -> dict:
    """A holder whose +0xc0 is MEMBER: 0, a tagged value, or 'counted' (a buffer whose dword at
    +0x20 is COUNT). The release function (when the masked count is not 0) and the base
    destructor are stubs comparing their object and the bytes behind it (the vtable already
    stored, the count already decremented)."""
    holder = bytearray((seed * 13 + i) & 0xff for i in range(0xd0))
    holder[0xc0:0xc8] = (0 if member == 'counted' else member).to_bytes(8, 'little')
    case = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void',
            'buffers': {'holder': {'size': 0xd0, 'bytes': holder.hex()}},
            'memory': [], 'imports': [], 'stubs': [],
            'args': {'rdi': 'buf:holder', 'rsi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
    if member == 'counted':
        counted = bytearray((seed * 7 + i) & 0xff for i in range(0x28))
        counted[0x20:0x24] = count.to_bytes(4, 'little')
        case['buffers']['counted'] = {'size': 0x28, 'bytes': counted.hex()}
        case['memory'].append({'addr': 'buf:holder+0xc0', 'pointer': 'counted'})
        if count & 0x3fffff:
            case['stubs'].append({'address': RELEASE_COUNTED, 'argc': 1, 'ret': seed,
                                  'argmem': [{'arg': 0, 'size': 0x28}]})
    case['stubs'].append({'address': DESTROY_BASE, 'argc': 1, 'ret': 0,
                          'argmem': [{'arg': 0, 'offset': 0xc0, 'size': 8}]})
    if freed:
        # The deleting twin: then the allocator's slot 0x68, comparing the allocator and the holder.
        case['returns'] = 'i64'
        case['buffers'].update({'allocator': {'size': 0x20, 'bytes': bytes((seed * 3 + i) & 0xff for i in range(0x20)).hex()},
                                'allocator_table': {'size': 0x80}})
        case['memory'] += [{'addr': OBJECT_ALLOCATOR, 'pointer': 'allocator'},
                           {'addr': 'buf:allocator+0x0', 'pointer': 'allocator_table'},
                           {'addr': 'buf:allocator_table+0x68', 'guest': ALLOCATOR_FREE}]
        case['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2, 'ret': seed << 40 | 0xf3ee})
    return case



def member_release_cases(address: str, value: int | None, freed: bool = False) -> list[dict]:
    """No member; a tagged one; a counted one with the count 5, 0x400000 (masked 0: no release)
    and 0xffc00001 (masked 1). FREED: the deleting twin's cases."""
    return [member_release_case('none', address, 0, None, 3, freed),
            member_release_case('tagged', address, 0x123457, None, 5, freed),
            member_release_case('counted', address, 'counted', 5, 7, freed),
            member_release_case('counted_masked_zero', address, 'counted', 0x400000, 11, freed),
            member_release_case('counted_high_bits', address, 'counted', 0xffc00001, 13, freed)]



def member_release_free_cases(address: str, value: int | None) -> list[dict]:
    return member_release_cases(address, value, freed=True)





# (object, guard) of each guarded vtable setter; the vtable is whatever the original stores.
STATIC_VTABLE_SETTERS = {'sets_static_vtable_at_0x05ac59b0': (0x05ac59b0, 0x05ac59b8), 'sets_static_vtable_at_0x05ac59c0': (0x05ac59c0, 0x05ac59c8), 'sets_static_vtable_at_0x05ac59d0': (0x05ac59d0, 0x05ac59d8)}



def static_vtable_cases(family: str):
    """The guard 0 (the vtable is stored and the guard set), 1 and 0xff (nothing happens) and
    0x100 (only the low byte is tested: stored); the object starts as a pattern."""
    obj, guard = STATIC_VTABLE_SETTERS[family]

    def cases(address: str, value: int | None) -> list[dict]:
        return [{'schema': 1, 'address': address, 'id': f'guard_{start:x}', 'returns': 'void',
                 'buffers': {},
                 'memory': [{'addr': hex(obj), 'bytes': struct.pack('<Q', 0x5a5a5a5a5a5a5a5a ^ start).hex()},
                            {'addr': hex(guard), 'bytes': struct.pack('<Q', start).hex()}],
                 'imports': [], 'stubs': [],
                 'args': {'rdi': '0x1111111111111111', 'rax': '0x5aa5c33c0ff0e11e'}}
                for start in (0, 1, 0xff, 0x100)]
    return cases



RELEASE_FUNCTION = '0x00400240'
   # a stand-in for slot 0x18 of the member's table, stubbed
RELEASE_SLOT = 0x18



def release_member_cases(address: str, value: int | None) -> list[dict]:
    """No member at +8 (only the vtable is stored), and a member whose table's slot 0x18 is a stub
    comparing the member; the object starts all ones and as a pattern."""
    out = []
    for seed, fill in ((3, 0xff), (5, None)):
        body = bytes(fill if fill is not None else (seed * 11 + i) & 0xff for i in range(0x20))
        for member in (False, True):
            c = {'schema': 1, 'address': address, 'id': f'{"member" if member else "no_member"}_{seed}',
                 'returns': 'void', 'buffers': {'object': {'size': 0x20, 'bytes': body.hex()}},
                 'memory': [{'addr': 'buf:object+0x8', 'bytes': bytes(8).hex()}], 'imports': [], 'stubs': [],
                 'args': {'rdi': 'buf:object', 'rsi': '0x2222222222222222', 'rax': '0x5aa5c33c0ff0e11e'}}
            if member:
                c['buffers']['member'] = {'size': 0x10, 'bytes': bytes((seed + i) & 0xff for i in range(0x10)).hex()}
                c['buffers']['table'] = {'size': 0x20}
                c['memory'] = [{'addr': 'buf:object+0x8', 'pointer': 'member'},
                               {'addr': 'buf:member+0x0', 'pointer': 'table'},
                               {'addr': f'buf:table+0x{RELEASE_SLOT:x}', 'guest': RELEASE_FUNCTION}]
                c['stubs'] = [{'address': RELEASE_FUNCTION, 'argc': 1, 'ret': seed}]
            out.append(c)
    return out

CASES = {'initialize_static_wide_string': static_wide_string_cases,
         'deletes_sized_through_thread_allocator': sized_delete_cases,
         'allocates_from_thread_allocator': allocate_cases,
         'posts_record_event_to_delegate_at_0x8': record_event_cases,
         'deleting_destructor_freeing_0x28_bytes': destructor_cases,
         'posts_text_event_to_delegate_at_0x50': text_event_cases,
         'calls_delegate_at_0x18_else_virtual_0x80': delegate_forward_cases,
         'posts_event_to_delegate_at_0x50': delegate_event_cases,
         'frees_object_through_thread_allocator': thread_free_cases,
         'destroys_then_deletes': destroys_then_deletes_cases,
         'calls_virtual_0x28_then_deletes_if_asked': virtual_delete_cases,
         'destroys_with_0x01402180_then_deletes': destroy_then_delete_cases('0x01402180'),
         'destroys_with_0x010e7ca0_then_deletes': destroy_then_delete_cases('0x010e7ca0'),
         'destroys_with_0x01401dc0_then_deletes': destroy_then_delete_cases('0x01401dc0'),
         'destroys_with_0x01640e70_then_deletes': destroy_then_delete_cases('0x01640e70'),
         'destroys_with_0x0139c4f0_then_deletes': destroy_then_delete_cases('0x0139c4f0'),
         'destroys_with_0x007164e0_then_frees': destroy_then_delete_cases('0x007164e0', freed=True),
         'destroys_with_0x005696e0_then_frees': destroy_then_delete_cases('0x005696e0', freed=True),
         'destroys_with_0x006fa840_then_frees': destroy_then_delete_cases('0x006fa840', freed=True),
         'destroys_with_0x0045a480_then_frees': destroy_then_delete_cases('0x0045a480', freed=True),
         'calls_empty_destructor_then_deletes': destroy_then_delete_cases(None),
         'destroys_with_0x010ee390_then_deletes': destroy_then_delete_cases('0x010ee390'),
         'destroys_with_0x016ea820_then_deletes': destroy_then_delete_cases('0x016ea820'),
         'destroys_with_0x02afd720_then_deletes': destroy_then_delete_cases('0x02afd720'),
         'destroys_with_0x0274a7f0_then_deletes': destroy_then_delete_cases('0x0274a7f0'),
         'destroys_with_0x006fbd50_then_frees': destroy_then_delete_cases('0x006fbd50', freed=True),

         'releases_member_at_0xc0_then_destroys_base': member_release_cases,
         'releases_member_at_0xc0_then_destroys_and_frees': member_release_free_cases,
         **{name: static_vtable_cases(name) for name in STATIC_VTABLE_SETTERS},
         'stores_vtable_0x05541b90_then_releases_member_at_8': release_member_cases}

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
    # No call, and the neighbouring size's replacement.
    'deletes_sized_through_thread_allocator': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'allocates_from_thread_allocator': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    # No call; and (virtual 0x28) the destructor called but the object never deleted.
    'destroys_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_virtual_0x28_then_deletes_if_asked': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero',
                                                 'bb_mutant_virtual_0x28_never_deletes'],
    'destroys_with_0x01402180_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x010e7ca0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x01401dc0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x01640e70_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0139c4f0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x007164e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x005696e0_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x006fa840_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0045a480_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'calls_empty_destructor_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x010ee390_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x016ea820_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x02afd720_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x0274a7f0_then_deletes': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'destroys_with_0x006fbd50_then_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    # No call and no write. (A mutant reaching the vtable or the base destructor would need the
    # game image, which the mutant library does not see.)
    'releases_member_at_0xc0_then_destroys_base': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],
    'releases_member_at_0xc0_then_destroys_and_frees': ['bb_mutant_does_nothing', 'bb_mutant_returns_zero'],

    # Nothing stored. (A mutant storing the wrong vtable would need the game image.)
    'sets_static_vtable_at_0x05ac59b0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59c0': ['bb_mutant_does_nothing'],
    'sets_static_vtable_at_0x05ac59d0': ['bb_mutant_does_nothing'],
    # Nothing stored or called; or the member released but no vtable stored.
    'stores_vtable_0x05541b90_then_releases_member_at_8': ['bb_mutant_does_nothing', 'bb_mutant_releases_member_at_8_only'],
}


def cases(family: str, address: str, value: int | None) -> list[dict]:
    return CASES[family](address, value)
