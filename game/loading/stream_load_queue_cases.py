#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the stream load queue's request, the stream handle's constructor and the
finished-entry queue (game/loading/stream_load_queue.cpp), for tools/verify.py run.

usage: stream_load_queue_cases.py OUT_DIR request|handle|front|pop

request (0x02a6cd90): a stream queue (its first vtable slot a scripted report stub, the manager
at +0x10, the lock at +0xa8 with scripted lock and unlock slots; it may sit at an address that
is not 8-aligned, so the events array's address is aligned up), a stream request (0x150 bytes of
a pattern, its validity slot scripted, the extra byte at +0x148) and a result handle. The stream
handle constructor runs as the original (so the info's +0x70 is written); the manager's info
lookup (0x024c17e0), the resource handle's constructor (0x024bd470, storing its info the way the
real one does) and destructor (0x024bd4c0), the request copy (0x024be3b0, leaving the local
copy's strings and data inline or on a heap so the destruction frees them) and the stream event
push_back (0x02a6d010, compared with the event's kind, info and extra byte) are scripted stubs.

handle (0x02a70a90): a handle, an info and a queue (each of the last two may be null); the
resource handle's constructor is a scripted stub.

front (0x02a69fb0) and pop (0x02a6a020): an object with a list of nodes (head node, count and
allocator stored at +0x30 aligned up to 8) and a lock at +0x58 (scripted lock and unlock). The
entry copy (0x02aa4b90), the entries' destroy slot and the allocator's free are scripted stubs.
"""
import json
import os
import struct
import sys

ADDRESSES = {'request': '0x02a6cd90', 'handle': '0x02a70a90', 'front': '0x02a69fb0', 'pop': '0x02a6a020'}
LOCK, UNLOCK, REPORT, VALID, FREE, DESTROY = ('0x00500200', '0x00500240', '0x00500280', '0x005002c0',
                                             '0x00500300', '0x00500340')
INFO_FOR, HANDLE_NEW, HANDLE_DROP = '0x024c17e0', '0x024bd470', '0x024bd4c0'
COPY, PUSH_BACK, ENTRY_COPY = '0x024be3b0', '0x02a6d010', '0x02aa4b90'
STRINGS = (0x48, 0xa0, 0xd8)
DATA = 0x110
MASK = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


class Case:
    def __init__(self, mode, cid):
        self.mode, self.cid = mode, cid
        self.raw, self.memory, self.stubs, self.args = {}, [], [], {}

    def lock_at(self, buffer, offset):
        self.raw['lock_vtable'] = bytearray(0x30)
        self.memory += [{'addr': f'buf:{buffer}+{offset}', 'pointer': 'lock_vtable+0'},
                        {'addr': 'buf:lock_vtable+24', 'guest': LOCK},
                        {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]

    def json(self, returns):
        return {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': returns,
                'args': self.args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
                'memory': self.memory, 'imports': [], 'stubs': self.stubs}


def handle_new(c, info):
    write = {'arg': 0, 'offset': 8, **({'pointer': f'{info}+0'} if info else {'bytes': u64(0)})}
    c.stubs.append({'address': HANDLE_NEW, 'argc': 2, 'writes': [write]})


def request_case(cid, valid=True, info=True, heap_strings=(), data=False, skew=0, fill=0x2e, extra=0x5a):
    c = Case('request', cid)
    q = f'buf:queue+{skew}' if skew else 'buf:queue'
    c.raw['queue'] = bytearray([0x4d]) * 0xd8
    c.lock_at('queue', skew + 0xa8)
    request = bytearray([fill]) * 0x150
    request[0x148] = extra
    struct.pack_into('<QQ', request, 0x20, 0x2020202020202020, 0x2828282828282828)   # the reported word, a decoy
    c.raw.update({'result': bytearray([0x11]) * 0x18, 'request': request, 'request_vtable': bytearray(0x18),
                  'queue_vtable': bytearray(8), 'manager': bytearray(8), 'info': bytearray([0x69]) * 0x98})
    c.memory += [{'addr': 'buf:request+0', 'pointer': 'request_vtable+0'},
                 {'addr': 'buf:request_vtable+16', 'guest': VALID},
                 {'addr': q, 'pointer': 'queue_vtable+0'},
                 {'addr': 'buf:queue_vtable+0', 'guest': REPORT},
                 {'addr': f'buf:queue+{skew + 16}', 'pointer': 'manager+0'}]
    c.args = {'rdi': 'buf:result', 'rsi': q, 'rdx': 'buf:request'}
    c.stubs.append({'address': VALID, 'argc': 1, 'ret': '0x1' if valid else '0x0'})
    if not valid:
        handle_new(c, None)
        return c.json('i64')
    c.stubs.append({'address': INFO_FOR, 'argc': 2, 'ret': 'buf:info' if info else '0x0'})
    if not info:
        c.stubs.append({'address': REPORT, 'argc': 4, 'argi': [0, 1, 3], 'argu32': [2]})
        handle_new(c, None)
        return c.json('i64')
    handle_new(c, 'info')
    c.stubs.append({'address': LOCK, 'argc': 1, 'argu32': [1]})
    writes, frees = [], 0
    c.raw['heap'] = bytearray(8)
    c.raw['heap_vtable'] = bytearray(0x78)
    c.memory += [{'addr': 'buf:heap+0', 'pointer': 'heap_vtable+0'},
                 {'addr': 'buf:heap_vtable+112', 'guest': FREE}]
    for base in STRINGS:
        long = base in heap_strings
        writes += [{'arg': 0, 'offset': base + 0x20, 'bytes': u64(15 if long else 7)},
                   {'arg': 0, 'offset': base + 0x28, 'pointer': 'heap+0'}]
        if long:
            c.raw[f'text_{base:x}'] = bytearray(0x20)
            writes.append({'arg': 0, 'offset': base + 8, 'pointer': f'text_{base:x}+0'})
            frees += 1
    if data:
        c.raw['data'] = bytearray(0x10)
        writes += [{'arg': 0, 'offset': DATA + 8, 'pointer': 'data+0'},
                   {'arg': 0, 'offset': DATA + 0x20, 'pointer': 'heap+0'}]
        frees += 1
    else:
        writes.append({'arg': 0, 'offset': DATA + 8, 'bytes': u64(0)})
    c.stubs.append({'address': COPY, 'argc': 2, 'writes': writes})
    c.stubs.append({'address': PUSH_BACK, 'argc': 2,
                    'argmem': [{'arg': 1, 'offset': 0, 'size': 4}, {'arg': 1, 'offset': 8, 'size': 16},
                               {'arg': 1, 'offset': 0x158, 'size': 1}]})
    c.stubs += [{'address': FREE, 'argc': 2} for _ in range(frees)]
    c.stubs.append({'address': UNLOCK, 'argc': 1})
    handle_new(c, 'info')
    c.stubs.append({'address': HANDLE_DROP, 'argc': 1, 'argmem': [{'arg': 0, 'offset': 8, 'size': 16}]})
    return c.json('i64')


def handle_case(cid, info, queue):
    c = Case('handle', cid)
    c.raw.update({'handle': bytearray([0x33]) * 0x18, 'info': bytearray([0x69]) * 0x98, 'queue': bytearray(0x10)})
    c.args = {'rdi': 'buf:handle', 'rsi': 'buf:info' if info else '0x0', 'rdx': 'buf:queue' if queue else '0x0'}
    handle_new(c, 'info' if info else None)
    return c.json('void')


def list_case(mode, cid, nodes, count=None, skew=0):
    """nodes: how many nodes the list links; count: the stored count (default: nodes)."""
    c = Case(mode, cid)
    c.raw['object'] = bytearray([0x0b]) * 0x68
    c.lock_at('object', skew + 0x58)
    base = (skew + 0x30 + 7) & ~7
    names = ['head'] + [f'node{k}' for k in range(nodes)]
    c.raw.update({n: bytearray([0x4e]) * 0x60 for n in names})
    c.raw.update({'entry_vtable': bytearray(8), 'alloc': bytearray(8), 'alloc_vtable': bytearray(0x78),
                  'out': bytearray([0x0f]) * 0x60})
    c.memory += [{'addr': f'buf:object+{base + 8}', 'pointer': 'head+0'},
                 {'addr': f'buf:object+{base + 16}', 'bytes': u64(nodes if count is None else count)},
                 {'addr': f'buf:object+{base + 24}', 'pointer': 'alloc+0'},
                 {'addr': 'buf:alloc+0', 'pointer': 'alloc_vtable+0'},
                 {'addr': 'buf:alloc_vtable+112', 'guest': FREE},
                 {'addr': 'buf:entry_vtable+0', 'guest': DESTROY}]
    for k, n in enumerate(names):
        c.memory += [{'addr': f'buf:{n}+0', 'pointer': f'{names[(k + 1) % len(names)]}+0'},
                     {'addr': f'buf:{n}+8', 'pointer': f'{names[k - 1]}+0'}]
        if k:
            c.memory.append({'addr': f'buf:{n}+16', 'pointer': 'entry_vtable+0'})
    c.args = {'rdi': f'buf:object+{skew}' if skew else 'buf:object'}
    c.stubs.append({'address': LOCK, 'argc': 1, 'argu32': [1]})
    stored = nodes if count is None else count
    if mode == 'front':
        c.args['rsi'] = 'buf:out'
        if stored:
            c.stubs.append({'address': ENTRY_COPY, 'argc': 2, 'ret': 'buf:out'})
    elif nodes:
        c.stubs.append({'address': DESTROY, 'argc': 1})
        c.stubs.append({'address': FREE, 'argc': 2})
    c.stubs.append({'address': UNLOCK, 'argc': 1})
    return c.json('i8' if mode == 'front' else 'void')


def cases(mode):
    if mode == 'request':
        return [request_case('queued'), request_case('not_valid', valid=False), request_case('no_info', info=False),
                request_case('heap_strings', heap_strings=STRINGS), request_case('name_on_heap', heap_strings=(0x48,)),
                request_case('data_buffer', data=True),
                request_case('everything_on_heap', heap_strings=STRINGS, data=True),
                request_case('queue_skewed_4', skew=4), request_case('queue_skewed_1', skew=1, data=True),
                request_case('zero_request', fill=0, extra=0), request_case('extra_ff', extra=0xff)]
    if mode == 'handle':
        return [handle_case('both', True, True), handle_case('info_only', True, False),
                handle_case('queue_only', False, True), handle_case('neither', False, False)]
    return [list_case(mode, 'empty', 0), list_case(mode, 'one', 1), list_case(mode, 'three', 3),
            list_case(mode, 'empty_count_2', 0, count=2), list_case(mode, 'two_count_0', 2, count=0),
            list_case(mode, 'skewed_3', 2, skew=3), list_case(mode, 'skewed_5_empty', 0, skew=5),
            list_case(mode, 'skewed_7', 1, skew=7)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        sys.exit(__doc__)
    out = sys.argv[1]
    generated = cases(sys.argv[2])
    os.makedirs(out, exist_ok=True)
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
