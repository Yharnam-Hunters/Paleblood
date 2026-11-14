#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the fast small object allocators' constructors
(game/engine/fast_small_object_allocator_construct.cpp), for tools/verify.py run.

usage: fast_small_object_allocator_cases.py OUT_DIR plain|locked

Each case builds an allocator (`alloc`, filled with a pattern) from a chunk size, the largest and
smallest object sizes, a step and the largest alignment (the 7th argument, on the stack). The
backing allocator's aligned allocation, heap id and free size (through a fake vtable) are scripted
stubs; the aligned allocation returns `classes`, sized for the class count, or 0. Each class's
set-up (0x02a3baa0) and the error reports (their order and argument counts worked out here) are
stubs, and for the locked variant the mutex's query and creation too.
"""
import json
import os
import sys

PLAIN, LOCKED = '0x02fa1640', '0x02fa1950'
FATAL_ERROR, INITIALIZE_CLASS = '0x024b55b0', '0x02a3baa0'
ALLOCATE_ALIGNED, HEAP_ID, FREE_SIZE = '0x02484110', '0x02484600', '0x02484610'  # any game functions, stubbed
MUTEX_IS_INITIALIZED, MUTEX_CREATE = '0x0247ee80', '0x02483fe0'
MASK = (1 << 64) - 1
CLASS_SIZE = 0x60


def plan(largest, smallest, step, alignment):
    """(class count, reports' argument counts before the allocation)."""
    reports = []
    if smallest > largest:
        reports.append(5)
    if step & (step - 1) & MASK:
        reports.append(4)
    if alignment & (alignment - 1) & MASK:
        reports.append(4)
    class_step = max(step, 8)
    align = max(alignment, 8)
    shift = class_step.bit_length() - 1
    top = (largest + align - 1) & -align & MASK
    bottom = 8 if smallest < 8 else smallest & ~7
    count = (((top - bottom) & MASK) >> shift) + 1
    return count & MASK, reports


def case(address, cid, chunk, largest, smallest, step, alignment, array=True, initialized=False, created=0):
    count, reports = plan(largest, smallest, step, alignment)
    assert 1 <= count <= 12, (cid, count)
    stubs = []
    if address == LOCKED:
        stubs.append({'address': MUTEX_IS_INITIALIZED, 'argc': 1, 'ret': int(initialized)})
        if not initialized:
            stubs.append({'address': MUTEX_CREATE, 'argc': 1, 'ret': created})
    stubs += [{'address': FATAL_ERROR, 'argc': n} for n in reports]
    stubs.append({'address': ALLOCATE_ALIGNED, 'argc': 3, 'ret': 'buf:classes' if array else 0})
    if not array:
        stubs += [{'address': HEAP_ID, 'argc': 1, 'ret': 0xfffffff3},
                  {'address': FREE_SIZE, 'argc': 1, 'ret': 0x123456789},
                  {'address': FATAL_ERROR, 'argc': 6}]
    slots = count if array else count - 1   # without an array only the null first slot is skipped
    stubs += [{'address': INITIALIZE_CLASS, 'argc': 5} for _ in range(slots)]
    size = 0x38 if address == PLAIN else 0x48
    array_bytes = CLASS_SIZE * count
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:alloc', 'rsi': 'buf:backing', 'rdx': chunk, 'rcx': largest, 'r8': smallest,
                     'r9': step},
            'stack': [alignment],
            'buffers': {'alloc': {'size': size, 'bytes': '7e' * size}, 'backing': {'size': 8},
                        'vtable': {'size': 0x60},
                        'classes': {'size': array_bytes, 'bytes': '5a' * array_bytes}},
            'memory': [{'addr': 'buf:backing+0', 'pointer': 'vtable'},
                       {'addr': 'buf:vtable+16', 'guest': HEAP_ID},
                       {'addr': 'buf:vtable+48', 'guest': FREE_SIZE},
                       {'addr': 'buf:vtable+88', 'guest': ALLOCATE_ALIGNED}],
            'stubs': stubs, 'imports': []}


def common(address):
    c = lambda cid, *a, **kw: case(address, cid, *a, **kw)  # noqa: E731
    return [
        c('typical', 0x4000, 0x80, 0x10, 0x10, 0x10),
        c('step_8', 0x4000, 0x40, 8, 8, 8),
        c('step_32', 0x4000, 0x100, 0x20, 0x20, 0x10),
        c('step_below_8', 0x4000, 0x40, 8, 4, 8),
        c('step_zero', 0x4000, 0x40, 8, 0, 8),
        c('step_not_power', 0x4000, 0x60, 0x10, 0x18, 0x10),
        c('step_12', 0x4000, 0x40, 0x10, 0x0c, 8),
        c('alignment_16_rounds', 0x4000, 0x48, 0x10, 0x10, 0x10),
        c('alignment_32', 0x4000, 0x70, 0x10, 0x10, 0x20),
        c('alignment_below_8', 0x4000, 0x44, 0x10, 0x10, 4),
        c('alignment_zero', 0x4000, 0x44, 0x10, 0x10, 0),
        c('alignment_not_power', 0x4000, 0x50, 0x10, 0x10, 0x18),
        c('smallest_below_8', 0x4000, 0x30, 3, 8, 8),
        c('smallest_unaligned', 0x4000, 0x40, 0x13, 8, 8),
        c('smallest_above_largest', 0x4000, 0x24, 0x28, 8, 8),
        c('one_class', 0x4000, 0x10, 0x10, 0x10, 0x10),
        c('largest_unaligned', 0x4000, 0x7c, 0x10, 0x10, 0x10),
        c('odd_class_sizes', 0x4000, 0x58, 0x18, 8, 8),
        c('all_reports', 0x4000, 0x30, 0x31, 0x18, 0x0c),
        c('no_array', 0x4000, 0x40, 0x10, 0x10, 0x10, array=False),
        c('no_array_one_class', 0x4000, 0x10, 0x10, 0x10, 0x10, array=False),
    ]


def locked_extra():
    return [
        case(LOCKED, 'mutex_already_set_up', 0x4000, 0x40, 0x10, 0x10, 0x10, initialized=True),
        case(LOCKED, 'mutex_creation_fails', 0x4000, 0x40, 0x10, 0x10, 0x10, created=-0x7ffdffe9),
        case(LOCKED, 'mutex_creation_positive', 0x4000, 0x40, 0x10, 0x10, 0x10, created=1),
    ]


WHICH = {'plain': lambda: common(PLAIN), 'locked': lambda: common(LOCKED) + locked_extra()}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in WHICH:
        sys.exit(__doc__)
    out = sys.argv[1]
    made = WHICH[sys.argv[2]]()
    os.makedirs(out, exist_ok=True)
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
