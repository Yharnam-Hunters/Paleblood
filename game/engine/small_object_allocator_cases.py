#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the small object allocators' constructors
(game/engine/small_object_allocator_construct.cpp), for tools/verify.py run.

usage: small_object_allocator_cases.py OUT_DIR plain|locked

Each case builds an allocator (`alloc`, filled with a pattern) from a chunk size, the largest and
smallest object sizes and a granularity. The backing allocator's aligned allocation (through a
fake vtable) is a scripted stub that returns `classes`, sized for the class count, or 0; the error
reports (their order and arguments worked out here from the checks) and, for the locked variant,
the mutex's query and creation are stubs too.
"""
import json
import os
import sys

PLAIN, LOCKED = '0x02fa1e20', '0x02fa22a0'
FATAL_ERROR, ALLOCATE_ALIGNED = '0x024b55b0', '0x02484110'
MUTEX_IS_INITIALIZED, MUTEX_CREATE = '0x0247ee80', '0x02483fe0'
MASK = (1 << 64) - 1
ALLOCATOR_SIZE = 0x60


def classes(chunk, largest, smallest, granularity):
    """(class sizes, report argument counts in order)."""
    out = [5] if chunk <= (largest * 2) & MASK else []
    step = granularity if granularity & (granularity - 1) == 0 else 16
    step = max(step, 8)
    shift = step.bit_length() - 1
    first = (smallest + step - 1) & -step & MASK
    last = (largest + 7) & ~7 & MASK
    stepped = ((last - 1 + step - first) & MASK) >> shift
    assert stepped < 16, 'keep the class array small'
    sizes = [first + i * step for i in range(stepped)] + [last]
    for size in sizes:
        low = size & 0xffffffff
        a = low & (-low) & 0xffffffff
        if a & 0x80000000:
            a |= MASK ^ 0xffffffff
        if a & (a - 1):
            out.append(4)
        stride = (max(size, 8) + a - 1) & -a & MASK
        assert stride, 'a class alignment of 0 divides by zero'
        if (stride * 2) & MASK > chunk:
            out.append(5)
    return sizes, out


def case(address, cid, chunk, largest, smallest, granularity, array=True, initialized=False, created=0):
    sizes, reports = classes(chunk, largest, smallest, granularity)
    assert len(sizes) <= 16, len(sizes)
    stubs = []
    if address == LOCKED:
        stubs.append({'address': MUTEX_IS_INITIALIZED, 'argc': 1, 'ret': int(initialized)})
        if not initialized:
            stubs.append({'address': MUTEX_CREATE, 'argc': 1, 'ret': created})
    stubs.append({'address': ALLOCATE_ALIGNED, 'argc': 3, 'ret': 'buf:classes' if array else 0})
    stubs += [{'address': FATAL_ERROR, 'argc': n} for n in reports[:1] if chunk <= (largest * 2) & MASK]
    class_reports = reports[1:] if chunk <= (largest * 2) & MASK else reports
    if array:
        stubs += [{'address': FATAL_ERROR, 'argc': n} for n in class_reports]
    # (The first report comes before the allocation: stubs are consumed per address, so the order
    # across addresses does not matter.)
    size = 0x50 if address == PLAIN else 0x60
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:alloc', 'rsi': 'buf:backing', 'rdx': chunk, 'rcx': largest, 'r8': smallest,
                     'r9': granularity},
            'buffers': {'alloc': {'size': size, 'bytes': '7e' * size}, 'backing': {'size': 8},
                        'vtable': {'size': 0x60},
                        'classes': {'size': ALLOCATOR_SIZE * len(sizes), 'bytes': '5a' * (ALLOCATOR_SIZE * len(sizes))}},
            'memory': [{'addr': 'buf:backing+0', 'pointer': 'vtable'}, {'addr': 'buf:vtable+88', 'guest': ALLOCATE_ALIGNED}],
            'stubs': stubs, 'imports': []}


def common(address):
    c = lambda cid, *a, **kw: case(address, cid, *a, **kw)  # noqa: E731
    return [
        c('typical', 0x1000, 0x80, 0x10, 0x10),
        c('granularity_8', 0x1000, 0x40, 8, 8),
        c('granularity_32', 0x2000, 0x100, 0x20, 0x20),
        c('granularity_not_power', 0x1000, 0x60, 0x10, 0x18),
        c('granularity_0', 0x1000, 0x40, 0x10, 0),
        c('granularity_1', 0x1000, 0x40, 0x10, 1),
        c('granularity_4', 0x1000, 0x40, 0x10, 4),
        c('smallest_rounded_up', 0x1000, 0x80, 0x11, 0x10),
        c('largest_rounded_up', 0x1000, 0x7c, 0x10, 0x10),
        c('largest_not_on_step', 0x1000, 0x48, 0x10, 0x10),
        c('one_class', 0x1000, 0x10, 0x10, 0x10),
        c('odd_class_sizes', 0x1000, 0x58, 0x18, 0x10),
        c('chunk_exactly_twice_largest', 0x100, 0x80, 0x40, 0x40),
        c('chunk_small_for_classes', 0x50, 0x40, 0x10, 0x10),
        c('chunk_zero', 0, 0x20, 0x10, 0x10),
        c('no_array_one_class', 0x1000, 0x10, 0x10, 0x10, array=False),
    ]


def plain_cases():
    return common(PLAIN)


def locked_cases():
    return common(LOCKED) + [
        case(LOCKED, 'mutex_already_set_up', 0x1000, 0x40, 0x10, 0x10, initialized=True),
        case(LOCKED, 'mutex_creation_fails', 0x1000, 0x40, 0x10, 0x10, created=-0x7ffdffe9),
        case(LOCKED, 'mutex_creation_positive', 0x1000, 0x40, 0x10, 0x10, created=1),
    ]


WHICH = {'plain': plain_cases, 'locked': locked_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in WHICH:
        sys.exit(__doc__)
    out = sys.argv[1]
    cases = WHICH[sys.argv[2]]()
    os.makedirs(out, exist_ok=True)
    for c in cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main()
