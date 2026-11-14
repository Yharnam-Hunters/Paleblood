#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the stream load queue's removal of a request
(game/loading/stream_queue_remove.cpp), for tools/verify.py run.

usage: stream_queue_remove_cases.py OUT_DIR

Each case gives a queue (`queue`, possibly at an address that is not 8-aligned, so both arrays'
addresses are aligned up) with a request pool (`pool`) and an in-use array of synthetic request
pointers. memmove is a scripted import; the error report and the give-back array's push_back
(0x02a6c690) are scripted stubs.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02a6d8e0'
FATAL_ERROR, PUSH_BACK = '0x024b55b0', '0x02a6c690'
QUEUE, POOL, POOL_OFFSET, POOL_SIZE_OFFSET, RELEASED, IN_USE = 0x148, 0x100, 0xc8, 0xd0, 0xd8, 0x108


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def align8(v):
    return (v + 7) & ~7


def case(cid, entries, request, skew=0, pool_size=POOL):
    """entries: offsets into the pool of the in-use requests; request: the request's offset (may
    lie outside the pool)."""
    buffers = {'queue': {'size': QUEUE + 8, 'bytes': (b'\x3a' * (QUEUE + 8)).hex()},
               'pool': {'size': POOL + 0x20},
               'in_use': {'size': 8 * max(len(entries), 1)}}
    in_use = align8(IN_USE + skew) - skew
    released = align8(RELEASED + skew) - skew
    memory = [{'addr': f'buf:queue+{skew + POOL_OFFSET}', 'pointer': 'pool+0'},
              {'addr': f'buf:queue+{skew + POOL_SIZE_OFFSET}', 'bytes': u64(pool_size)},
              {'addr': f'buf:queue+{skew + in_use + 8}', 'pointer': 'in_use+0'},
              {'addr': f'buf:queue+{skew + in_use + 16}', 'pointer': f'in_use+{8 * len(entries)}'},
              {'addr': f'buf:queue+{skew + released + 8}', 'bytes': u64(0x5151)}]
    for i, e in enumerate(entries):
        memory.append({'addr': f'buf:in_use+{8 * i}', 'pointer': f'pool+{e}'})
    stubs, imports = [], []
    if request < 0 or request > pool_size:
        stubs.append({'address': FATAL_ERROR, 'argc': 3})
    if request in entries:
        imports.append({'name': 'memmove', 'argc': 3})
    stubs.append({'address': PUSH_BACK, 'argc': 2, 'argmem': [{'arg': 1, 'size': 8}]})
    if request < 0:
        buffers['before'] = {'size': 0x10}
        target = 'buf:before+8'
    else:
        target = f'buf:pool+{request}'
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': f'buf:queue+{skew}' if skew else 'buf:queue', 'rsi': target},
            'buffers': buffers, 'memory': memory, 'imports': imports, 'stubs': stubs}


def cases():
    return [
        case('first_in_use', [0x00, 0x40, 0x80], 0x00),
        case('middle_in_use', [0x00, 0x40, 0x80], 0x40),
        case('last_in_use', [0x00, 0x40, 0x80], 0x80),
        case('not_in_use', [0x00, 0x40], 0x80),
        case('empty_in_use', [], 0x20),
        case('first_of_duplicates', [0x40, 0x40, 0x10], 0x40),
        case('pool_end_inside', [0x00, POOL], POOL),
        case('past_pool_end', [0x00], POOL + 8),
        case('pool_start', [0x08], 0x00),
        case('queue_skewed_4', [0x00, 0x40], 0x40, skew=4),
        case('queue_skewed_1', [0x40], 0x40, skew=1),
        case('empty_pool', [], 0x00, pool_size=0),
    ]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases()
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
