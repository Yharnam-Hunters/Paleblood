#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the resource load event array's push_back and reallocation
(game/loading/load_event_array.cpp), for tools/verify.py run.

usage: load_event_array_cases.py OUT_DIR [push_back|reallocate]

Each case lays out an engine array (`array`: begin, end, capacity end, allocator) over synthetic
load events (`events`, 0x158 bytes: two words and a request with a vtable, three wide strings at
+0x58/+0xb0/+0xe8 of the event and a data buffer at +0x128 with its allocator), and an event to
append (`event`, or one of the array's own). The request's copy constructor (0x024be3b0) is a
scripted stub. For push_back the reallocation (0x02a6bc50) is a stub too, which installs the new
buffer (`grown`) the way the real one does. For reallocation the allocators are fake objects whose
allocate and free slots point at scripted stubs (array, strings and data buffers apart); a failed
allocation runs the report path with the thread's transaction flag clear.
"""
import json
import os
import struct
import sys

PUSH_BACK, REALLOCATE = '0x02a69b70', '0x02a6bc50'
COPY, FATAL_ERROR = '0x024be3b0', '0x024b55b0'
ARRAY_ALLOCATE, ARRAY_FREE, STRING_FREE, DATA_FREE = '0x00500200', '0x00500220', '0x00500240', '0x00500260'
ALLOCATE_SLOT, FREE_SLOT = 0x58, 0x70
EVENT = 0x158
STRINGS = (0x58, 0xb0, 0xe8)       # in declaration order; destroyed last first
DATA = 0x128
MASK = 0xffffffffffffffff
MAX_EVENTS = MASK // EVENT
TRANSACTION_FLAG_OFFSET = 0x057e4318
THREAD_BLOCK = 0x40


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


def grown_capacity(count):
    half = count >> 1
    capacity = count + half if MAX_EVENTS - half >= count else 0
    return max(capacity, count + 1)


class Case:
    def __init__(self, address, cid):
        self.address = address
        self.cid = cid
        self.buffers = {'array': bytearray(b'\x77' * 0x28)}
        self.memory = []
        self.stubs = []

    def pointer(self, at, target):
        self.memory.append({'addr': at, 'pointer': target})

    def allocator(self, name, free, allocate=None):
        self.buffers[name] = bytearray(b'\x3c' * 16)
        self.buffers[name + '_vtable'] = bytearray(b'\xe7' * 0x80)
        self.pointer(f'buf:{name}+0', f'{name}_vtable+0')
        self.memory.append({'addr': f'buf:{name}_vtable+{FREE_SLOT}', 'guest': free})
        if allocate:
            self.memory.append({'addr': f'buf:{name}_vtable+{ALLOCATE_SLOT}', 'guest': allocate})

    def events(self, name, count, layout=None):
        """layout: {index: (heap string offsets, has data)}."""
        raw = bytearray()
        for i in range(count):
            heaps, data = (layout or {}).get(i, ((), False))
            e = bytearray(b'\xa5' * EVENT)
            e[0:16] = struct.pack('<QQ', 0x1000 + i, 0x2000 + i)
            for base in STRINGS:
                heap = base in heaps
                if not heap:
                    e[base + 8:base + 0x18] = f'e{i}s{base:x}'.encode('utf-16-le').ljust(16, b'\0')
                e[base + 0x18:base + 0x28] = struct.pack('<QQ', 9 if heap else 3, 12 if heap else 7)
            e[DATA:DATA + 0x18] = bytes(0x18)
            raw += e
            for base in STRINGS:
                if base in heaps:
                    self.buffers[f'h_{name}_{i}_{base:x}'] = bytearray(b'\x41\0' * 16)
                    self.pointer(f'buf:{name}+{EVENT * i + base + 8}', f'h_{name}_{i}_{base:x}+0')
                self.pointer(f'buf:{name}+{EVENT * i + base + 0x28}', 'string_allocator+0')
            if data:
                # True: a whole buffer; 'end_only': a null start with the other two set.
                self.buffers[f'd_{name}_{i}'] = bytearray(b'\xdd' * 32)
                for k, off in enumerate((0, 16, 32)):
                    if data == 'end_only' and k == 0:
                        continue
                    self.pointer(f'buf:{name}+{EVENT * i + DATA + 8 * k}', f'd_{name}_{i}+{off}')
            self.pointer(f'buf:{name}+{EVENT * i + DATA + 0x18}', 'data_allocator+0')
        self.buffers[name] = raw or bytearray(8)

    def construct(self):
        self.stubs.append({'address': COPY, 'argc': 2, 'argmem': [{'arg': 1, 'size': 0x40}]})

    def array_fields(self, buf, begin, end, capacity):
        for field, at in ((8, begin), (16, end), (24, capacity)):
            if at is None:
                self.memory.append({'addr': f'buf:array+{field}', 'bytes': u64(0)})
            else:
                self.pointer(f'buf:array+{field}', f'{buf}+{EVENT * at}')

    def json(self, args=None, gs=None):
        c = {'schema': 1, 'address': self.address, 'id': self.cid, 'returns': 'void',
             'args': args or {'rdi': 'buf:array'},
             'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.buffers.items()},
             'memory': self.memory, 'stubs': self.stubs}
        if gs:
            c['gs'] = gs
        return c


def push_back_case(cid, count, capacity, source=None, grow_fails=False):
    c = Case(PUSH_BACK, cid)
    c.allocator('string_allocator', STRING_FREE)
    c.allocator('data_allocator', DATA_FREE)
    c.events('events', capacity)
    if count or capacity:
        c.array_fields('events', 0, count, capacity)
    else:
        c.array_fields('events', None, None, None)
    if source is None:
        c.events('event', 1, {0: ((0xb0,), True)})
        event = 'buf:event'
    else:
        event = f'buf:events+{EVENT * source}'
    if count == capacity:
        new_capacity = grown_capacity(count)
        c.events('grown', new_capacity)
        writes = ([{'arg': 0, 'offset': 8, 'bytes': u64(0)}, {'arg': 0, 'offset': 16, 'bytes': u64(0)},
                   {'arg': 0, 'offset': 24, 'bytes': u64(EVENT * new_capacity)}] if grow_fails else
                  [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                   {'arg': 0, 'offset': 16, 'pointer': f'grown+{EVENT * count}'},
                   {'arg': 0, 'offset': 24, 'pointer': f'grown+{EVENT * new_capacity}'}])
        c.stubs.append({'address': REALLOCATE, 'argc': 2, 'writes': writes})
        if not grow_fails:
            c.construct()
    else:
        c.construct()
    return c.json(args={'rdi': 'buf:array', 'rsi': event})


def reallocate_case(cid, count, capacity, new_capacity, layout=None, allocation_fails=False, null_array=False):
    c = Case(REALLOCATE, cid)
    c.allocator('array_allocator', ARRAY_FREE, ARRAY_ALLOCATE)
    c.allocator('string_allocator', STRING_FREE)
    c.allocator('data_allocator', DATA_FREE)
    c.pointer('buf:array+32', 'array_allocator+0')
    c.events('events', capacity, layout)
    if null_array:
        c.array_fields('events', None, None, None)
    else:
        c.array_fields('events', 0, count, capacity)
    c.buffers['fresh'] = bytearray(b'\x99' * max(EVENT * new_capacity, 8))
    c.stubs.append({'address': ARRAY_ALLOCATE, 'argc': 3, 'ret': 0 if allocation_fails else 'buf:fresh'})
    gs = None
    if allocation_fails:
        c.buffers['tls'] = bytearray(THREAD_BLOCK + 8)
        c.pointer(f'buf:tls+{THREAD_BLOCK}', f'tls+{THREAD_BLOCK}')
        c.memory.append({'addr': f'0x{TRANSACTION_FLAG_OFFSET:08x}', 'bytes': struct.pack('<q', -0x20).hex()})
        c.stubs.append({'address': FATAL_ERROR, 'argc': 3})
        gs = f'buf:tls+{THREAD_BLOCK}'
    else:
        for _ in range(count):
            c.construct()
    if not null_array:
        for i in range(count):
            heaps, data = (layout or {}).get(i, ((), False))
            if data is True:
                c.stubs.append({'address': DATA_FREE, 'argc': 2})
            for base in reversed(STRINGS):
                if base in heaps:
                    c.stubs.append({'address': STRING_FREE, 'argc': 2})
        c.stubs.append({'address': ARRAY_FREE, 'argc': 2})
    return c.json(gs=gs)


def push_back_cases():
    return [
        push_back_case('room_outside', 2, 4),
        push_back_case('room_inside_first', 2, 4, source=0),
        push_back_case('room_inside_last', 3, 4, source=2),
        push_back_case('full_outside_one', 1, 1),
        push_back_case('full_outside_two', 2, 2),
        push_back_case('full_outside_five', 5, 5),
        push_back_case('full_inside_first', 3, 3, source=0),
        push_back_case('full_inside_last', 4, 4, source=3),
        push_back_case('empty_no_buffer', 0, 0),
        push_back_case('growth_fails', 0, 0, grow_fails=True),
        push_back_case('room_after_empty', 0, 2),
    ]


def reallocate_cases():
    return [
        reallocate_case('inline_no_data', 3, 3, 4),
        reallocate_case('heap_strings_and_data', 3, 3, 5,
                        {0: ((0x58,), False), 1: ((0xb0, 0xe8), True), 2: ((0x58, 0xb0, 0xe8), True)}),
        reallocate_case('data_only', 2, 2, 3, {1: ((), True)}),
        reallocate_case('data_start_null', 2, 2, 3, {0: ((), 'end_only'), 1: ((0x58,), True)}),
        reallocate_case('single_heap_last', 1, 1, 2, {0: ((0xe8,), False)}),
        reallocate_case('empty_with_buffer', 0, 2, 3),
        reallocate_case('no_buffer', 0, 0, 1, null_array=True),
        reallocate_case('partly_full', 2, 5, 8, {0: ((0xb0,), True)}),
        reallocate_case('shrink_to_count', 3, 6, 3),
        reallocate_case('fails_empty', 0, 2, 3, allocation_fails=True),
        reallocate_case('fails_no_buffer', 0, 0, 1, allocation_fails=True, null_array=True),
    ]


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] not in ('push_back', 'reallocate')):
        sys.exit(__doc__)
    out = sys.argv[1]
    which = sys.argv[2] if len(sys.argv) == 3 else 'push_back'
    generated = push_back_cases() if which == 'push_back' else reallocate_cases()
    os.makedirs(out, exist_ok=True)
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
