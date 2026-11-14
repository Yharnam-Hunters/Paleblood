#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's thread object's constructors (game/engine/thread_construct.cpp), for
tools/verify.py run.

usage: thread_construct_cases.py OUT_DIR default|sized|with_affinity|create

Each case constructs a thread object over a filled buffer (`thread`), so the bytes the constructor
leaves alone are compared too. The default allocator exists or is made by its (stubbed) creation;
its info slot is a stub on a fake vtable that writes the flags under test. The start mutex's
initialization query runs as original code through the real vtable: it reads the flag the
constructor has just cleared, so the mutex is always created (the query's function is shorter than
a stub's 14-byte jump); mutex creation, the condition signal's creation (which writes its
created flag or not), the mutex lock, the thread creation, the name assignment and the reports are
stubs, and strlen is a scripted import.

create (engine_thread_create_02a3b4d0) passes its arguments to the stubbed thread creation with
affinity (with a spare scripted call), with every core and 0 in place of its fifth argument.
"""
import json
import os
import struct
import sys

ADDRESSES = {'default': '0x0248adc0', 'sized': '0x0248b200', 'with_affinity': '0x0248afe0',
             'create': '0x02a3b4d0'}
LINES = {'default': (0x60, 0x61, 0x62), 'sized': (0xb2, 0xb3, 0xb4), 'with_affinity': (0x8d, 0x8e, 0x8f)}
FATAL_ERROR, DEFAULT_ALLOCATOR, DEFAULT_ALLOCATOR_CREATE = '0x024b55b0', '0x05940670', '0x02fa2b10'
INFO = '0x02484110'                 # any game function: the fake vtable points at it, stubbed
MUTEX_CREATE, MUTEX_LOCK = '0x02483fe0', '0x0247ee90'
SIGNAL_CREATE, STRING_ASSIGN = '0x0247e690', '0x02a2d8e0'
CREATE, CREATE_WITH_AFFINITY = '0x02a3b4d0', '0x02a3b310'
VALID = 0x20
SIGNAL_FLAGS = 0x50 + 0x18


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(kind, cid, name='Worker', allocator=True, info=0x21, mutex_result=0,
         signal_flags=1, handle=0x5550000000000042, stack_size=0x20000, second=0x3f, procedure=0x1234,
         argument=0xfeedface12345678):
    buffers = {'thread': {'size': 0xd0, 'bytes': '5a' * 0xd0}, 'allocator': {'size': 8},
               'allocator_vtable': {'size': 0x80}}
    memory = [{'addr': 'buf:allocator+0', 'pointer': 'allocator_vtable'},
              {'addr': 'buf:allocator_vtable+32', 'guest': INFO}]
    memory.append({'addr': DEFAULT_ALLOCATOR, 'pointer': 'allocator'} if allocator
                  else {'addr': DEFAULT_ALLOCATOR, 'bytes': u64(0)})
    name_arg = 0
    if name is not None:
        buffers['name'] = {'size': len(name) + 1, 'bytes': (name.encode() + b'\0').hex()}
        name_arg = 'buf:name'
    stubs, imports = [], []
    if not allocator:
        stubs.append({'address': DEFAULT_ALLOCATOR_CREATE, 'argc': 0, 'ret': 'buf:allocator'})
    stubs.append({'address': INFO, 'argc': 3, 'writes': [{'arg': 0, 'offset': 0, 'bytes': '%02x' % info}]})
    reports = 0 if info & VALID else 1
    stubs.append({'address': MUTEX_CREATE, 'argc': 1, 'ret': hex(mutex_result)})
    signal = {'address': SIGNAL_CREATE, 'argc': 2}
    if signal_flags is not None:    # None: the creation leaves the flags as the constructor set them
        signal['writes'] = [{'arg': 0, 'offset': 0x18, 'bytes': struct.pack('<I', signal_flags).hex()}]
    stubs.append(signal)
    stubs.append({'address': MUTEX_LOCK, 'argc': 1})
    if kind == 'with_affinity':
        stubs.append({'address': CREATE_WITH_AFFINITY, 'argc': 7, 'ret': hex(handle)})
        args = {'rdi': 'buf:thread', 'rsi': procedure, 'rdx': stack_size, 'rcx': second, 'r8': name_arg,
                'r9': argument}
    else:
        # The fifth argument is a 32-bit value the caller loads with a 32-bit move.
        stubs.append({'address': CREATE, 'argc': 6, 'argi': [0, 1, 2, 3, 5], 'argu32': [4], 'ret': hex(handle)})
        if kind == 'default':
            args = {'rdi': 'buf:thread', 'rsi': procedure, 'rdx': second, 'rcx': name_arg, 'r8': argument}
        else:
            args = {'rdi': 'buf:thread', 'rsi': procedure, 'rdx': stack_size, 'rcx': second, 'r8': name_arg,
                    'r9': argument}
    text = 'unnamed' if name is None else name
    if text:
        imports.append({'name': 'strlen', 'argc': 1, 'ret': len(text)})
    stubs.append({'address': STRING_ASSIGN, 'argc': 3})
    created = (mutex_result & 0xffffffff) == 0
    reports += (handle == 0) + (not created) + ((signal_flags or 0) & 1 == 0)
    stubs += [{'address': FATAL_ERROR, 'argc': 3} for _ in range(reports)]
    return {'schema': 1, 'address': ADDRESSES[kind], 'id': cid, 'returns': 'void', 'args': args,
            'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': imports}


def create_case(cid, dropped=0x7, name='Worker', handle=0x5550000000000042, unused=0x3f, stack_size=0x20000):
    buffers = {'system_thread': {'size': 0x10, 'bytes': '5a' * 0x10}, 'thread': {'size': 0x10}}
    if name is not None:
        buffers['name'] = {'size': len(name) + 1, 'bytes': (name.encode() + b'\0').hex()}
    args = {'rdi': 'buf:system_thread', 'rsi': 'buf:thread', 'rdx': unused, 'rcx': stack_size, 'r8': dropped,
            'r9': 'buf:name' if name is not None else 0}
    stubs = [{'address': CREATE_WITH_AFFINITY, 'argc': 7, 'ret': hex(handle)},
             {'address': CREATE_WITH_AFFINITY, 'argc': 7, 'ret': 0}]
    return {'schema': 1, 'address': ADDRESSES['create'], 'id': cid, 'returns': 'i64', 'args': args,
            'buffers': buffers, 'memory': [], 'stubs': stubs, 'imports': []}


def make(kind):
    if kind == 'create':
        return [create_case('created'), create_case('not_created', handle=0),
                create_case('dropped_upper_bits', dropped=0xffffffff12345678),
                create_case('dropped_all_bits', dropped=0xffffffffffffffff), create_case('null_name', name=None),
                create_case('stack_size_zero', stack_size=0),
                create_case('unused_upper_bits', unused=0xffffffff00000007)]
    out = [
        case(kind, 'named'),
        case(kind, 'null_name', name=None),
        case(kind, 'empty_name', name=''),
        case(kind, 'long_name', name='DLThread worker with a long name'),
        case(kind, 'no_default_allocator', allocator=False),
        case(kind, 'invalid_heap', info=0x1f),
        case(kind, 'only_valid_flag', info=0x20),
        case(kind, 'all_flags', info=0xff),
        case(kind, 'mutex_create_fails', mutex_result=0xffffffff),
        case(kind, 'mutex_create_positive', mutex_result=1),
        case(kind, 'mutex_create_upper_bits', mutex_result=0x1234567800000000),
        case(kind, 'signal_not_created', signal_flags=0),
        case(kind, 'signal_other_flags', signal_flags=0xfffffffe),
        case(kind, 'signal_flags_set', signal_flags=7),
        case(kind, 'signal_flags_untouched', signal_flags=None),
        case(kind, 'thread_not_created', handle=0),
        case(kind, 'thread_handle_one', handle=1),
        case(kind, 'everything_fails', name=None, allocator=False, info=0, mutex_result=0x80020016,
             signal_flags=0, handle=0),
        case(kind, 'zero_procedure_argument', procedure=0, argument=0),
        case(kind, 'second_argument_upper_bits', second=0xffffffff00000007),
        case(kind, 'second_argument_zero', second=0),
    ]
    if kind != 'default':
        out += [case(kind, 'stack_size_zero', stack_size=0),
                case(kind, 'stack_size_huge', stack_size=0xffffffffffff0000)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    made = make(sys.argv[2])
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
