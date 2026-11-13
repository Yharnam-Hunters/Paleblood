#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for queuing a resource load request and for the file load queue's removal
(game/loading/load_event_queue.cpp), for tools/verify.py run.

usage: load_event_queue_cases.py OUT_DIR request|request_kind_4|remove

request (0x02a698e0, kind 0) and request_kind_4 (0x02a69cd0): a queue (`queue`, its vtable's first
slot a scripted report stub, its lock at +0xa0 with scripted lock and unlock slots), a request
(0x148 bytes of a pattern, its vtable's slot 0x10 a scripted validity stub) and a result handle.
The manager getter runs as the original; the manager's info lookup (0x024c17e0), the handle's constructor
(0x024bd470, which stores its info the way the real one does) and destructor (0x024bd4c0), the
request copy (0x024be3b0, which leaves the local copy's strings and data as the case says: inline
and empty, or on a heap so the destruction frees them) and the event push_back (0x02a69b70) are
scripted stubs. A case can pass the queue at an address that is not 8-aligned, so the events
array's address is aligned up.

remove (0x02a6af30): a file load queue whose transfer tasks (+0x28) hold six lists of synthetic
tasks (+0x10: the info, or a decoy). The read-write lock's read and unlock (0x0247f640,
0x0247f710) and the task cancel (0x024dd8a0) are scripted stubs.
"""
import json
import os
import struct
import sys

ADDRESSES = {'request': '0x02a698e0', 'request_kind_4': '0x02a69cd0', 'remove': '0x02a6af30'}
KINDS = {'request': 0, 'request_kind_4': 4}
LOCK, UNLOCK, REPORT, VALID, FREE = '0x00500200', '0x00500240', '0x00500280', '0x005002c0', '0x00500300'
INFO_FOR, HANDLE_NEW, HANDLE_DROP = '0x024c17e0', '0x024bd470', '0x024bd4c0'
COPY, PUSH_BACK = '0x024be3b0', '0x02a69b70'
RW_READ, RW_UNLOCK, CANCEL = '0x0247f640', '0x0247f710', '0x024dd8a0'
QUEUE, LOCK_OFFSET, REQUEST = 0xb0, 0xa0, 0x148
STRINGS = (0x48, 0xa0, 0xd8)
DATA = 0x110
LISTS, LIST_STRIDE, TASKS = 0x18, 0x28, 0x118


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


class Case:
    def __init__(self, mode, cid, queue_skew=0):
        self.mode = mode
        self.cid = cid
        self.skew = queue_skew
        self.raw = {'queue': bytearray([0x4d]) * (QUEUE + 8), 'lock_vtable': bytearray(0x30)}
        self.memory = [{'addr': f'buf:queue+{queue_skew + LOCK_OFFSET}', 'pointer': 'lock_vtable+0'},
                       {'addr': 'buf:lock_vtable+24', 'guest': LOCK},
                       {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]
        self.stubs = []
        self.args = {}

    def json(self, returns):
        return {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': returns,
                'args': self.args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
                'memory': self.memory, 'imports': [], 'stubs': self.stubs}


def handle_new(c, info):
    write = {'arg': 0, 'offset': 8, **({'pointer': f'{info}+0'} if info else {'bytes': u64(0)})}
    c.stubs.append({'address': HANDLE_NEW, 'argc': 2, 'writes': [write]})


def request_case(mode, cid, valid=True, info=True, heap_strings=(), data=False, skew=0, fill=0x2e):
    c = Case(mode, cid, skew)
    q = f'buf:queue+{skew}' if skew else 'buf:queue'
    c.raw['result'] = bytearray([0x11]) * 0x10
    request = bytearray([fill]) * REQUEST
    c.raw['request'] = request
    c.raw['request_vtable'] = bytearray(0x18)
    c.raw['queue_vtable'] = bytearray(0x8)
    c.raw['manager'] = bytearray(8)
    c.raw['info'] = bytearray(0x98)
    c.memory += [{'addr': 'buf:request+0', 'pointer': 'request_vtable+0'},
                 {'addr': 'buf:request_vtable+16', 'guest': VALID},
                 {'addr': f'buf:queue+{skew}', 'pointer': 'queue_vtable+0'},
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
    # The copy leaves the local request's strings and data as the case says.
    writes = []
    frees = []
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
    if data:
        c.raw['data'] = bytearray(0x10)
        writes += [{'arg': 0, 'offset': DATA + 8, 'pointer': 'data+0'},
                   {'arg': 0, 'offset': DATA + 0x20, 'pointer': 'heap+0'}]
    else:
        writes.append({'arg': 0, 'offset': DATA + 8, 'bytes': u64(0)})
    c.stubs.append({'address': COPY, 'argc': 2, 'writes': writes})
    c.stubs.append({'address': PUSH_BACK, 'argc': 2,
                    'argmem': [{'arg': 1, 'offset': 0, 'size': 4}, {'arg': 1, 'offset': 8, 'size': 16}]})
    if data:
        frees.append(1)
    for base in reversed(STRINGS):
        if base in heap_strings:
            frees.append(1)
    c.stubs += [{'address': FREE, 'argc': 2} for _ in frees]
    c.stubs.append({'address': UNLOCK, 'argc': 1})
    handle_new(c, 'info')
    c.stubs.append({'address': HANDLE_DROP, 'argc': 1, 'argmem': [{'arg': 0, 'offset': 8, 'size': 8}]})
    return c.json('i64')


def request_cases(mode):
    return [
        request_case(mode, 'queued'),
        request_case(mode, 'not_valid', valid=False),
        request_case(mode, 'no_info', info=False),
        request_case(mode, 'heap_strings', heap_strings=STRINGS),
        request_case(mode, 'name_on_heap', heap_strings=(0x48,)),
        request_case(mode, 'last_string_on_heap', heap_strings=(0xd8,)),
        request_case(mode, 'data_buffer', data=True),
        request_case(mode, 'everything_on_heap', heap_strings=STRINGS, data=True),
        request_case(mode, 'queue_skewed_4', skew=4),
        request_case(mode, 'queue_skewed_1', skew=1, data=True),
        request_case(mode, 'zero_request', fill=0),
    ]


def remove_case(cid, lists, target=(None, None), tasks_null=False):
    """lists: per list (6), how many tasks; target: (list, index) of the task loading the info,
    or None. Other tasks load a decoy info."""
    c = Case('remove', cid)
    c.raw['info'] = bytearray(8)
    c.raw['decoy'] = bytearray(8)
    c.raw['tasks'] = bytearray([0x5e]) * TASKS
    c.memory.append({'addr': 'buf:queue+40', 'pointer': 'tasks+0'})
    c.args = {'rdi': 'buf:queue', 'rsi': 'buf:info'}
    found = None
    for k, count in enumerate(lists):
        name = f'list{k}'
        c.raw[name] = bytearray(8 * max(count, 1))
        base = LISTS + LIST_STRIDE * k
        c.memory += [{'addr': f'buf:tasks+{base + 8}', 'pointer': f'{name}+0'},
                     {'addr': f'buf:tasks+{base + 16}', 'pointer': f'{name}+{8 * count}'}]
        for j in range(count):
            task = f'task{k}_{j}'
            c.raw[task] = bytearray(0x18)
            hit = target == (k, j) or (target[0] == k and target[1] == 'all')
            c.memory += [{'addr': f'buf:{name}+{8 * j}', 'pointer': f'{task}+0'},
                         {'addr': f'buf:{task}+16', 'pointer': 'info+0' if hit else 'decoy+0'}]
            if hit and found is None:
                found = task
    c.stubs.append({'address': LOCK, 'argc': 1, 'argu32': [1]})
    c.stubs.append({'address': RW_READ, 'argc': 1, 'argu32': [1]})
    c.stubs.append({'address': RW_UNLOCK, 'argc': 1})
    if found:
        c.stubs.append({'address': CANCEL, 'argc': 1, 'argu8': [1]})
    c.stubs.append({'address': UNLOCK, 'argc': 1})
    return c.json('void')


def remove_cases():
    return [
        remove_case('all_empty', [0] * 6),
        remove_case('first_list_first', [3, 0, 0, 0, 0, 0], (0, 0)),
        remove_case('first_list_last', [3, 2, 0, 0, 0, 0], (0, 2)),
        remove_case('second_list', [2, 2, 2, 2, 2, 2], (1, 1)),
        remove_case('third_list', [2, 2, 2, 2, 2, 2], (2, 0)),
        remove_case('fourth_list', [1, 1, 1, 1, 1, 1], (3, 0)),
        remove_case('fifth_list', [0, 0, 0, 0, 3, 0], (4, 2)),
        remove_case('last_list', [1, 1, 1, 1, 1, 2], (5, 1)),
        remove_case('not_found', [2, 2, 2, 2, 2, 2]),
        remove_case('first_of_several', [0, 3, 0, 0, 0, 0], (1, 'all')),
        remove_case('third_list_second', [0, 0, 2, 0, 0, 2], (2, 1)),
    ]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        sys.exit(__doc__)
    out = sys.argv[1]
    mode = sys.argv[2]
    generated = remove_cases() if mode == 'remove' else request_cases(mode)
    os.makedirs(out, exist_ok=True)
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
