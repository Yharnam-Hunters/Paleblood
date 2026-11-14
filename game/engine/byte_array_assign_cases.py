#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine byte array's copy assignment (game/engine/byte_array_assign.cpp), for
tools/verify.py run.

usage: byte_array_assign_cases.py OUT_DIR

Each case lays out the array (`array`) and the source (`source`, or the array itself), engine
arrays of 0x28 bytes (a word, begin, end, capacity end, allocator), over byte buffers of their own
(`old`, `data`; null when absent). The two allocators are fake objects with distinct stubs in slot
0x10 (so the order of the queries shows) and scripted allocate and free slots. memmove is a
scripted import writing what the real one would. A failed allocation runs the report path with the
thread's transaction flag clear (the report, 0x024b55b0, is a stub that returns): a one-byte
source then leaves the array at null with its end at 1. Longer sources would write from address 1
on, and a set transaction flag jumps away; neither returns in a stubbed run and they are not cased.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02b4d100'
FATAL_ERROR = '0x024b55b0'
ARRAY_QUERY, SOURCE_QUERY, ALLOCATE, FREE = '0x00500200', '0x00500220', '0x00500240', '0x00500260'
QUERY_SLOT, ALLOCATE_SLOT, FREE_SLOT = 0x10, 0x58, 0x70
BEGIN, END, CAPACITY_END, ALLOCATOR = 8, 0x10, 0x18, 0x20
TRANSACTION_FLAG_OFFSET = 0x057e4318
THREAD_BLOCK = 0x40


def u64(v):
    return struct.pack('<Q', v).hex()


def pattern(n, seed):
    return bytes((seed + 7 * i) & 0xff for i in range(n))


class Case:
    def __init__(self, cid):
        self.cid = cid
        self.buffers = {'array': bytearray(b'\x77' * 0x28), 'source': bytearray(b'\x55' * 0x28)}
        self.memory, self.stubs, self.imports = [], [], []
        self.gs = None

    def allocator(self, name, query):
        self.buffers[name] = bytearray(b'\x3c' * 16)
        self.buffers[name + '_vtable'] = bytearray(b'\xe7' * 0x80)
        self.memory += [{'addr': f'buf:{name}+0', 'pointer': f'{name}_vtable+0'},
                        {'addr': f'buf:{name}_vtable+{QUERY_SLOT}', 'guest': query},
                        {'addr': f'buf:{name}_vtable+{ALLOCATE_SLOT}', 'guest': ALLOCATE},
                        {'addr': f'buf:{name}_vtable+{FREE_SLOT}', 'guest': FREE}]

    def array(self, which, data, size, capacity, allocator):
        """which: the 0x28-byte object; data: its byte buffer's name, or None for a null buffer."""
        if data is None:
            for field in (BEGIN, END, CAPACITY_END):
                self.memory.append({'addr': f'buf:{which}+{field}', 'bytes': u64(0)})
        else:
            for field, at in ((BEGIN, 0), (END, size), (CAPACITY_END, capacity)):
                self.memory.append({'addr': f'buf:{which}+{field}', 'pointer': f'{data}+{at}'})
        self.memory.append({'addr': f'buf:{which}+{ALLOCATOR}', 'pointer': f'{allocator}+0'})

    def json(self, source_arg):
        c = {'schema': 1, 'address': ADDRESS, 'id': self.cid, 'returns': 'i64',
             'args': {'rdi': 'buf:array', 'rsi': source_arg},
             'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.buffers.items()},
             'memory': self.memory, 'imports': self.imports, 'stubs': self.stubs}
        if self.gs:
            c['gs'] = self.gs
        return c


def assign_case(cid, size, capacity, source_size, has_buffer=True, source_buffer=True, shared_allocator=False,
                allocation_fails=False):
    c = Case(cid)
    c.allocator('array_allocator', ARRAY_QUERY)
    source_allocator = 'array_allocator' if shared_allocator else 'source_allocator'
    if not shared_allocator:
        c.allocator('source_allocator', SOURCE_QUERY)
    old = pattern(max(capacity, 1), 0x10)
    data = pattern(max(source_size, 1), 0x80)
    c.buffers['old'] = bytearray(old + b'\xee' * 8)
    c.buffers['data'] = bytearray(data + b'\xdd' * 8)
    c.array('array', 'old' if has_buffer else None, size, capacity, 'array_allocator')
    c.array('source', 'data' if source_buffer else None, source_size, source_size + 3, source_allocator)

    query = SOURCE_QUERY if not shared_allocator else ARRAY_QUERY
    c.stubs += [{'address': ARRAY_QUERY, 'argc': 1}, {'address': query, 'argc': 1}]
    if source_size == 0:
        pass
    elif source_size <= size:
        c.imports.append({'name': 'memmove', 'argc': 3, 'ret': 'buf:old',
                          'writes': [{'arg': 0, 'bytes': data[:source_size].hex()}]})
    elif source_size <= capacity:
        entry = {'name': 'memmove', 'argc': 3, 'ret': 'buf:old' if has_buffer else 0}
        if size:
            entry['writes'] = [{'arg': 0, 'bytes': data[:size].hex()}]
        c.imports.append(entry)
    else:
        if has_buffer:
            c.stubs.append({'address': FREE, 'argc': 2})
        c.buffers['fresh'] = bytearray(b'\x99' * (source_size + 8))
        c.stubs.append({'address': ALLOCATE, 'argc': 3, 'ret': 0 if allocation_fails else 'buf:fresh'})
        if allocation_fails:
            c.buffers['tls'] = bytearray(THREAD_BLOCK + 8)
            c.memory.append({'addr': f'buf:tls+{THREAD_BLOCK}', 'pointer': f'tls+{THREAD_BLOCK}'})
            c.memory.append({'addr': f'0x{TRANSACTION_FLAG_OFFSET:08x}', 'bytes': struct.pack('<q', -0x20).hex()})
            c.stubs.append({'address': FATAL_ERROR, 'argc': 3})
            c.gs = f'buf:tls+{THREAD_BLOCK}'
    return c.json('buf:source')


def self_case(cid, size, capacity):
    c = Case(cid)
    c.allocator('array_allocator', ARRAY_QUERY)
    c.buffers['old'] = bytearray(pattern(capacity, 0x10) + b'\xee' * 8)
    c.array('array', 'old', size, capacity, 'array_allocator')
    return c.json('buf:array')


CASES = [
    self_case('self_assign', 3, 5),
    self_case('self_assign_empty', 0, 4),
    assign_case('empty_source_keeps_buffer', 4, 6, 0),
    assign_case('empty_source_null_buffers', 0, 0, 0, has_buffer=False, source_buffer=False),
    assign_case('empty_source_null_array', 0, 0, 0, has_buffer=False),
    assign_case('shorter_moved', 6, 8, 3),
    assign_case('same_size_moved', 5, 5, 5),
    assign_case('one_byte_over_one', 1, 1, 1),
    assign_case('longer_within_capacity', 3, 9, 7),
    assign_case('longer_up_to_capacity', 3, 9, 9),
    assign_case('empty_array_within_capacity', 0, 6, 4),
    assign_case('one_past_capacity', 4, 6, 7),
    assign_case('much_larger', 2, 3, 40),
    assign_case('full_array_grows', 4, 4, 5),
    assign_case('null_buffer_allocates', 0, 0, 6, has_buffer=False),
    assign_case('null_buffer_allocates_one', 0, 0, 1, has_buffer=False),
    assign_case('shared_allocator_moved', 4, 6, 2, shared_allocator=True),
    assign_case('shared_allocator_grows', 2, 2, 9, shared_allocator=True),
    assign_case('allocation_fails_one_byte', 0, 0, 1, has_buffer=False, allocation_fails=True),
    assign_case('allocation_fails_after_free', 0, 0, 1, allocation_fails=True),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
