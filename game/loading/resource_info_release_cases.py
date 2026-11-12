#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for dropping a reference to a resource info (game/loading/resource_info_release.cpp),
for tools/verify.py run.

usage: resource_info_release_cases.py OUT_DIR

Each case lays out a resource info (`info`: flags at +0x31, stream queue +0x70, pending list head
+0x80, count +0x88, list allocator +0x90) with a pending list of synthetic nodes (`head`, `nodeN`:
next, previous, request). The reference count's drop and read, the resource manager's release
and the stream queue's removal are scripted stubs; the engine's shared lock (0x05aa4578) points at
a fake vtable whose lock and unlock are stubs, and so does the list allocator's free. Not covered:
another thread setting the releasing flag between the first test and the lock (a stub cannot reach
the info from the lock's arguments).
"""
import json
import os
import struct
import sys

ADDRESS = '0x024bd930'
DROP, COUNT, MANAGER_RELEASE, STREAM_REMOVE = '0x02a3a2a0', '0x02a3a1b0', '0x024c1530', '0x02a6d8e0'
LOCK, UNLOCK, NODE_FREE = '0x00500200', '0x00500220', '0x00500240'
SHARED_LOCK = '0x05aa4578'
RELEASING = 0x10
NODE = 0x18


def case(cid, references=0, flags=0, requests=(), count=None):
    """requests: the request of each pending node in list order (None for a null request)."""
    info = bytearray(b'\x6b' * 0x98)
    info[0x31] = flags
    count = len(requests) if count is None else count
    info[0x88:0x90] = struct.pack('<Q', count)
    buffers = {'info': info, 'stream_queue': bytearray(8), 'lock_vtable': bytearray(b'\xe7' * 0x40),
               'node_allocator': bytearray(8), 'node_vtable': bytearray(b'\xe7' * 0x80),
               'head': bytearray(b'\x00' * NODE)}
    memory = [{'addr': 'buf:info+112', 'pointer': 'stream_queue+0'},
              {'addr': 'buf:info+128', 'pointer': 'head+0'},
              {'addr': 'buf:info+144', 'pointer': 'node_allocator+0'},
              {'addr': 'buf:node_allocator+0', 'pointer': 'node_vtable+0'},
              {'addr': 'buf:node_vtable+112', 'guest': NODE_FREE},
              {'addr': SHARED_LOCK, 'pointer': 'lock_vtable+0'},
              {'addr': 'buf:lock_vtable+24', 'guest': LOCK},
              {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]
    ring = ['head'] + [f'node{i}' for i in range(len(requests))]
    for i, request in enumerate(requests):
        buffers[f'node{i}'] = bytearray(NODE)
        if request is not None:
            buffers[f'request{i}'] = bytearray(b'\x52' * 8)
            memory.append({'addr': f'buf:node{i}+16', 'pointer': f'request{i}+0'})
    for j, node in enumerate(ring):
        memory.append({'addr': f'buf:{node}+0', 'pointer': ring[(j + 1) % len(ring)] + '+0'})
        memory.append({'addr': f'buf:{node}+8', 'pointer': ring[j - 1] + '+0'})

    stubs = [{'address': DROP, 'argc': 1}, {'address': COUNT, 'argc': 1, 'ret': references & 0xffffffff}]
    if references == 0 and not flags & RELEASING:
        stubs += [{'address': LOCK, 'argc': 2, 'argi': [0], 'argu32': [1]}, {'address': UNLOCK, 'argc': 1},
                  {'address': MANAGER_RELEASE, 'argc': 2}]
        remaining = count
        for request in requests:
            if not remaining or request is None:
                break
            stubs += [{'address': NODE_FREE, 'argc': 2}, {'address': STREAM_REMOVE, 'argc': 2}]
            remaining -= 1
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:info'},
            'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in buffers.items()},
            'memory': memory, 'stubs': stubs}


def cases():
    return [
        case('references_remain', references=2, requests=('a',)),
        case('negative_count', references=-1),
        case('already_releasing', flags=RELEASING, requests=('a',)),
        case('last_reference_no_pending'),
        case('pending_two', requests=('a', 'b')),
        case('pending_three_other_flags', flags=0xc1, requests=('a', 'b', 'c')),
        case('null_request_first', requests=(None, 'b')),
        case('null_request_second', requests=('a', None, 'c')),
        case('count_below_list', requests=('a', 'b', 'c'), count=2),
        case('cancelled_already_set', flags=0x20, requests=('a',)),
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
