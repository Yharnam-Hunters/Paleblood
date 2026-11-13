#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for a resource info's reference count (game/loading/resource_info_reference.cpp).

usage: resource_info_reference_cases.py OUT_DIR count|drop

Counts of zero, one, many, the largest positive and negative 32-bit values, with garbage around
the count. The engine mutex lock and unlock (0x02484110, 0x02484290) are stubbed; a stub that
writes the count while the lock is held checks the count is read after the lock, and a lock that
fails checks its result is ignored.
"""
import json
import os
import sys

LOCK, UNLOCK = '0x02484110', '0x02484290'
ADDRESSES = {'count': '0x02a3a1b0', 'drop': '0x02a3a2a0'}
MASK = (1 << 64) - 1
MUTEX = 0x1122334455667788


def info_bytes(count, after=0xa5a5a5a5):
    return (MUTEX.to_bytes(8, 'little') + (count & 0xffffffff).to_bytes(4, 'little')
            + after.to_bytes(4, 'little') + bytes(range(0x30, 0x80))).hex()


def case(variant, cid, count, lock_ret=0, count_under_lock=None):
    lock = {'address': LOCK, 'argc': 2, 'ret': hex(lock_ret & MASK), 'argmem': [{'arg': 0, 'size': 16}]}
    if count_under_lock is not None:
        lock['writes'] = [{'arg': 0, 'offset': 8, 'bytes': (count_under_lock & 0xffffffff).to_bytes(4, 'little').hex()}]
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid,
            'returns': 'i32' if variant == 'count' else 'void',
            'args': {'rdi': 'buf:info'}, 'buffers': {'info': {'size': 0x60, 'bytes': info_bytes(count)}},
            'memory': [], 'imports': [],
            'stubs': [lock, {'address': UNLOCK, 'argc': 1, 'ret': '0x0', 'argmem': [{'arg': 0, 'size': 16}]}]}


def cases(variant):
    c = lambda cid, *a, **k: case(variant, cid, *a, **k)
    return [c('zero', 0), c('one', 1), c('two', 2), c('many', 1000), c('max_positive', 0x7fffffff),
            c('negative_one', -1), c('min_negative', -0x80000000), c('lock_fails', 3, lock_ret=0x80020016),
            c('changed_under_lock', 5, count_under_lock=1), c('zeroed_under_lock', 5, count_under_lock=0)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
