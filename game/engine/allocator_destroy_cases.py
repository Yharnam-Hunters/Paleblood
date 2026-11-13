#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the small object allocators' destructors
(game/engine/fast_small_object_allocator_destroy.cpp, game/engine/small_object_allocator_destroy.cpp),
for tools/verify.py run.

usage: allocator_destroy_cases.py OUT_DIR KIND
  KIND: fast_destroy, fast_delete, fast_destroy_locked, fast_delete_locked (fast small object
        allocators), destroy, delete, delete_locked (small object allocators)

Each case builds an allocator (`alloc`, filled with a pattern) with a class array (`classes`) of
0 to 3 size classes and a backing allocator whose free (through a fake vtable) is a scripted stub.
A fast allocator's classes are released by a stub (0x02a3bc40); a small object allocator's classes
hold a chunk tree or not, and their chunk release (0x02484ae0) is a stub. A locked allocator's
mutex has a fake vtable whose initialization query is a stub, a flag, and scripted
scePthreadMutexDestroy results; the error reports are stubs. Deleting forms end in operator delete.
"""
import json
import os
import struct
import sys

FATAL_ERROR, FREE = '0x024b55b0', '0x02484110'   # FREE: any game function, stubbed
MUTEX_IS_INITIALIZED = '0x0247ee80'
FAST_RELEASE_CLASS, RELEASE_CHUNKS = '0x02a3bc40', '0x02484ae0'
EBUSY, EINVAL = 0x80020010, 0x80020016
CLASS_SIZE = 0x60

# kind: (address, fast, locked, deleting, allocator size, class count offset, backing offset, lock offset)
KINDS = {
    'fast_destroy': ('0x02484760', True, False, False, 0x30, 0x10, 0x28, None),
    'fast_delete': ('0x024846f0', True, False, True, 0x30, 0x10, 0x28, None),
    'fast_destroy_locked': ('0x02484910', True, True, False, 0x48, 0x10, 0x28, 0x30),
    'fast_delete_locked': ('0x024847d0', True, True, True, 0x48, 0x10, 0x28, 0x30),
    'destroy': ('0x0248aa90', False, False, False, 0x50, 0x38, 0x48, None),
    'delete': ('0x0248a9f0', False, False, True, 0x50, 0x38, 0x48, None),
    'delete_locked': ('0x0248ab30', False, True, True, 0x60, 0x48, 0x58, 0x10),
}


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def reports_for(result):
    """The error reports' argument counts after a destroy result (none when busy or 0)."""
    low = result & 0xffffffff
    if low in (0, EBUSY):
        return []
    return [3, 3] if low == EINVAL else [3]


def destroys(queried, flag, results):
    """How many destroys the mutex teardown makes: the query's, then the base's when still set."""
    used = []
    results = list(results)
    if queried:
        r = results.pop(0)
        used.append(r)
        if r & 0xffffffff != EBUSY:
            flag = False
    if flag:
        used.append(results.pop(0))
    assert not results, 'unused destroy results'
    return used


def case(kind, cid, classes=2, trees=None, queried=False, flag=False, results=()):
    address, fast, locked, deleting, size, count_at, backing_at, lock_at = KINDS[kind]
    memory = [{'addr': f'buf:alloc+{count_at}', 'bytes': u64(classes)},
              {'addr': 'buf:alloc+8', 'pointer': 'classes'},
              {'addr': f'buf:alloc+{backing_at}', 'pointer': 'backing'},
              {'addr': 'buf:backing+0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+112', 'guest': FREE}]
    stubs, imports = [], []
    if fast:
        stubs += [{'address': FAST_RELEASE_CLASS, 'argc': 1} for _ in range(classes)]
    else:
        trees = trees if trees is not None else [True] * classes
        for index, tree in enumerate(trees):
            at = index * CLASS_SIZE + 0x58
            if tree:
                memory.append({'addr': f'buf:classes+{at}', 'pointer': f'classes+{index * CLASS_SIZE + 0x10}'})
                stubs.append({'address': RELEASE_CHUNKS, 'argc': 2})
            else:
                memory.append({'addr': f'buf:classes+{at}', 'bytes': u64(0)})
    stubs.append({'address': FREE, 'argc': 2})
    if locked:
        memory += [{'addr': f'buf:alloc+{lock_at}', 'pointer': 'mutex_vtable'},
                   {'addr': f'buf:alloc+{lock_at + 8}', 'bytes': u64(0x1122334455667788)},
                   {'addr': f'buf:alloc+{lock_at + 16}', 'bytes': '01' if flag else '00'},
                   {'addr': 'buf:mutex_vtable+16', 'guest': MUTEX_IS_INITIALIZED}]
        stubs.append({'address': MUTEX_IS_INITIALIZED, 'argc': 1, 'ret': int(queried)})
        for r in destroys(queried, flag, results):
            imports.append({'name': 'scePthreadMutexDestroy', 'argc': 1, 'ret': hex(r)})
            stubs += [{'address': FATAL_ERROR, 'argc': n} for n in reports_for(r)]
    else:
        assert not results
    if deleting:
        imports.append({'name': '_ZdlPv', 'argc': 1})
    array = max(classes, 1) * CLASS_SIZE
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:alloc'},
            'buffers': {'alloc': {'size': size, 'bytes': '7e' * size}, 'backing': {'size': 8},
                        'vtable': {'size': 0x78}, 'mutex_vtable': {'size': 0x30},
                        'classes': {'size': array, 'bytes': '5a' * array}},
            'memory': memory, 'stubs': stubs, 'imports': imports}


def common(kind):
    out = [case(kind, 'no_classes', classes=0), case(kind, 'one_class', classes=1),
           case(kind, 'three_classes', classes=3)]
    if not KINDS[kind][1]:
        out += [case(kind, 'no_trees', classes=2, trees=[False, False]),
                case(kind, 'second_tree_only', classes=3, trees=[False, True, False])]
    return out


def locked(kind):
    c = lambda cid, **kw: case(kind, cid, **kw)  # noqa: E731
    return [
        c('mutex_not_initialized'),
        c('mutex_destroyed', queried=True, flag=True, results=[0]),
        c('mutex_einval', queried=True, flag=True, results=[EINVAL]),
        c('mutex_other_error', queried=True, flag=True, results=[0x80020001]),
        c('mutex_positive_result', queried=True, flag=True, results=[5]),
        c('mutex_busy_then_destroyed', queried=True, flag=True, results=[EBUSY, 0]),
        c('mutex_busy_twice', queried=True, flag=True, results=[EBUSY, EBUSY]),
        c('mutex_busy_then_einval', queried=True, flag=True, results=[EBUSY, EINVAL]),
        c('mutex_busy_then_other_error', queried=True, flag=True, results=[EBUSY, 0x80020002]),
        c('mutex_query_false_flag_set', queried=False, flag=True, results=[0]),
        c('mutex_query_false_flag_set_einval', queried=False, flag=True, results=[EINVAL]),
        c('mutex_query_true_flag_clear_busy', queried=True, flag=False, results=[EBUSY]),
        c('mutex_query_true_flag_clear', queried=True, flag=False, results=[0]),
        c('mutex_busy_upper_bits', queried=True, flag=True, results=[0x1234567880020010, 0xffffffff00000000]),
        c('mutex_einval_upper_bits', queried=True, flag=True, results=[0xabcdef0080020016]),
        c('mutex_no_classes_destroyed', classes=0, queried=True, flag=True, results=[0]),
    ]


def cases(kind):
    return common(kind) + (locked(kind) if KINDS[kind][2] else [])


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in KINDS:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    made = cases(sys.argv[2])
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
