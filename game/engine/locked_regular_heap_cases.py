#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the heap manager's locked regular heap (game/engine/locked_regular_heap.cpp), for
tools/verify.py run.

usage: locked_regular_heap_cases.py OUT_DIR construct|initialize

The object is a buffer filled with a pattern so that every field written shows: the regular heap's
state, the light mutex (its vtable a buffer whose third slot names a stub, the handle and the flag)
and the allocator interface. The memory to manage lies in `arena` at a given offset. The C
library's memset is scripted (it writes nothing) and the PthreadMutex initialize (0x02483fe0) is
stubbed; the stub in the vtable answers whether the mutex is initialized, sometimes disagreeing
with the flag (or with garbage above the bool's byte), so the virtual call is what counts.
"""
import json
import os
import sys

ADDRESSES = {'construct': '0x0247c590', 'initialize': '0x0247c5e0'}
PTHREAD_INIT = '0x02483fe0'
IS_INITIALIZED = '0x00500200'  # a stub in the light mutex vtable's third slot
OBJECT = 0x468
MUTEX, HANDLE, FLAG = 0x440, 0x448, 0x450
MASK = (1 << 64) - 1
HIGH = 0xdead_beef_0000_0000  # garbage above the 32-bit result


def object_bytes(flag):
    b = bytearray(b'\xc3' * OBJECT)
    b[HANDLE:HANDLE + 8] = (0x1122334455667788).to_bytes(8, 'little')
    b[FLAG] = flag
    return b.hex()


def case(variant, cid, args, flag=0, virtual=None, stubs=(), memset=False):
    """virtual: what the vtable's is-initialized slot answers (None: not called)."""
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid,
         'returns': 'void' if variant == 'construct' else 'i8', 'args': {'rdi': 'buf:object', **args},
         'buffers': {'object': {'size': OBJECT, 'bytes': object_bytes(flag)},
                     'vtable': {'size': 0x20, 'bytes': 'c3' * 0x20}, 'arena': {'size': 0x2000}},
         'memory': [{'addr': f'buf:object+{MUTEX:#x}', 'pointer': 'vtable'},
                    {'addr': 'buf:vtable+0x10', 'guest': IS_INITIALIZED}],
         'imports': [{'name': 'memset', 'argc': 3, 'ret': 'buf:object'}] if memset else [], 'stubs': []}
    if virtual is not None:
        c['stubs'].append({'address': IS_INITIALIZED, 'argc': 1, 'ret': hex(virtual)})
    c['stubs'] += list(stubs)
    return c


def mutex_init(ret):
    return {'address': PTHREAD_INIT, 'argc': 1, 'ret': hex(ret & MASK), 'argmem': [{'arg': 0, 'size': 8}]}


def construct_cases():
    return [case('construct', 'clear', {}, memset=True), case('construct', 'flag_was_set', {}, flag=1, memset=True),
            case('construct', 'flag_nonboolean', {}, flag=0x80, memset=True)]


def initialize_cases():
    def heap(offset, size):
        return {'rsi': f'buf:arena+{offset}', 'rdx': size}

    def ready(cid, offset, size, refused=False):
        return case('initialize', cid, heap(offset, size), 1, 1, memset=refused)

    out = [
        case('initialize', 'no_memory', {'rsi': 0, 'rdx': 0x400}),
        case('initialize', 'no_memory_mutex_not_initialized', {'rsi': 0, 'rdx': 0x400}, flag=0),
        case('initialize', 'no_memory_size_zero', {'rsi': 0, 'rdx': 0}, flag=1),
        # The mutex is ready: the regular heap's set-up decides.
        ready('aligned_memory', 0, 0x400), ready('offset_1', 1, 0x400), ready('offset_8', 8, 0x400),
        ready('offset_9', 9, 0x400), ready('offset_15', 15, 0x400), ready('size_unaligned_end', 0, 0x40f),
        ready('range_exactly_0x40', 8, 0x58), ready('range_0x30_refused', 8, 0x38, refused=True),
        ready('end_rounds_down_refused', 0, 0x57, refused=True), ready('end_rounds_down_passes', 0, 0x5f),
        ready('large_range', 0, 0x2000), ready('range_0x80_list_7', 0, 0x98),
        # The mutex is made here first.
        case('initialize', 'mutex_made', heap(0, 0x400), 0, 0, [mutex_init(0)]),
        case('initialize', 'mutex_made_high_bits', heap(8, 0x400), 0, 0, [mutex_init(HIGH)]),
        case('initialize', 'mutex_made_then_refused', heap(8, 0x38), 0, 0, [mutex_init(0)], memset=True),
        case('initialize', 'mutex_fails', heap(0, 0x400), 0, 0, [mutex_init(-1)]),
        case('initialize', 'mutex_fails_positive', heap(0, 0x400), 0, 0, [mutex_init(5)]),
        case('initialize', 'mutex_fails_low_bits_only', heap(0, 0x400), 0, 0, [mutex_init(HIGH | 0x80020016)]),
        case('initialize', 'flag_set_virtual_no', heap(0, 0x400), 1, 0, [mutex_init(0)]),
        case('initialize', 'virtual_upper_bits', heap(0, 0x400), 0, 0x100, [mutex_init(0)]),
        case('initialize', 'virtual_yes_flag_clear', heap(0, 0x400), 0, 1),
    ]
    return out


VARIANTS = {'construct': construct_cases, 'initialize': initialize_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in VARIANTS:
        print(__doc__, file=sys.stderr)
        return 2
    cases = VARIANTS[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
