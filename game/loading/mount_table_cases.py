#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the disk file device's mount table push_back and reallocation
(game/loading/mount_table.cpp), for tools/verify.py run.

usage: mount_table_cases.py OUT_DIR [push_back|reallocate]

Each case lays out a mount table (`table`: begin, end, capacity end, allocator) over synthetic
mount entries (`entries`, 0x70 bytes: name and target wide strings, each with its size, capacity,
allocator and owns byte), and a mount to append (`mount`, or one of the table's own entries).
The wide string assign (0x02a279a0) is a scripted stub. For push_back the reallocation
(0x024716d0) is a stub too, which installs the new buffer (`grown`) the way the real one does.
For reallocation the allocators are fake objects whose allocate and free slots point at scripted
stubs (the table's allocator and the strings' allocator apart); a failed allocation runs the
report path with the thread's transaction flag clear (the jump does not return in the game).
"""
import json
import os
import struct
import sys

PUSH_BACK, REALLOCATE = '0x02470d50', '0x024716d0'
ASSIGN, FATAL_ERROR = '0x02a279a0', '0x024b55b0'
TABLE_ALLOCATE, TABLE_FREE, STRING_FREE = '0x00500200', '0x00500220', '0x00500240'
ALLOCATE_SLOT, FREE_SLOT = 0x58, 0x70
ENTRY = 0x70
TARGET = 0x38
NPOS = 0xffffffffffffffff
MAX_ENTRIES = 0x249249249249249
TRANSACTION_FLAG_OFFSET = 0x057e4318
THREAD_BLOCK = 0x40


def u64(v):
    return struct.pack('<Q', v & NPOS).hex()


def grown_capacity(count):
    half = count >> 1
    capacity = count + half if MAX_ENTRIES - half >= count else 0
    return max(capacity, count + 1)


def entry_bytes(i, heap_name=False, heap_target=False):
    """A mount's raw bytes: inline or heap strings (heap pointers are filled in as presets)."""
    raw = bytearray(b'\xa5' * ENTRY)
    for base, heap, owns in ((0, heap_name, 1), (TARGET, heap_target, i & 1)):
        raw[base:base + 8] = struct.pack('<Q', 0x1111 * (i + 1))
        if not heap:
            raw[base + 8:base + 0x18] = f'n{i}{base:x}'.encode('utf-16-le').ljust(16, b'\0')
        raw[base + 0x18:base + 0x28] = struct.pack('<QQ', 12 + i if heap else 3, 15 + i if heap else 7)
        raw[base + 0x30] = owns
        raw[base + 0x31:base + 0x38] = b'\x5c' * 7
    return bytes(raw)


class Case:
    def __init__(self, address, cid):
        self.address = address
        self.cid = cid
        self.buffers = {'table': bytearray(b'\x77' * 0x28)}
        self.memory = []
        self.stubs = []
        self.imports = []
        self.extra = {}

    def pointer(self, at, target):
        self.memory.append({'addr': at, 'pointer': target})

    def entries(self, name, count, heaps=()):
        raw = bytearray()
        for i in range(count):
            hn, ht = (i in heaps, i in heaps) if not isinstance(heaps, dict) else heaps.get(i, (False, False))
            raw += entry_bytes(i, hn, ht)
            for base, heap in ((0, hn), (TARGET, ht)):
                if heap:
                    self.buffers[f'heap_{name}_{i}_{base:x}'] = bytearray(b'\x41\0' * 16)
                    self.pointer(f'buf:{name}+{ENTRY * i + base + 8}', f'heap_{name}_{i}_{base:x}+0')
                self.pointer(f'buf:{name}+{ENTRY * i + base + 0x28}', 'string_allocator+0')
        self.buffers[name] = raw or bytearray(8)

    def assign(self, destination, source):
        self.stubs.append({'address': ASSIGN, 'argc': 4, 'ret': f'buf:{destination}',
                           'argmem': [{'arg': 0, 'size': 0x30}, {'arg': 1, 'size': 0x30}]})

    def construct(self, destination, source):
        self.assign(destination, source)
        self.assign(destination, source)

    def json(self, returns='void', args=None, gs=None):
        c = {'schema': 1, 'address': self.address, 'id': self.cid, 'returns': returns,
             'args': args or {'rdi': 'buf:table'},
             'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.buffers.items()},
             'memory': self.memory, 'stubs': self.stubs, 'imports': self.imports}
        if gs:
            c['gs'] = gs
        return c


def table_fields(c, buf, begin, end, capacity):
    for field, at in ((8, begin), (16, end), (24, capacity)):
        if at is None:
            c.memory.append({'addr': f'buf:table+{field}', 'bytes': u64(0)})
        else:
            c.pointer(f'buf:table+{field}', f'{buf}+{ENTRY * at}')


def push_back_case(cid, count, capacity, source=None, grow_fails=False):
    """source: None for an outside mount, or the index of the table's own entry."""
    c = Case(PUSH_BACK, cid)
    c.buffers['string_allocator'] = bytearray(b'\x3c' * 16)
    c.entries('entries', capacity)
    if count or capacity:
        table_fields(c, 'entries', 0, count, capacity)
    else:
        table_fields(c, 'entries', None, None, None)
    if source is None:
        c.buffers['mount'] = bytearray(entry_bytes(9, True, False))
        mount = 'buf:mount'
    else:
        mount = f'buf:entries+{ENTRY * source}'
    if count == capacity:
        new_capacity = grown_capacity(count)
        c.entries('grown', new_capacity)
        writes = ([{'arg': 0, 'offset': 8, 'bytes': u64(0)}, {'arg': 0, 'offset': 16, 'bytes': u64(0)},
                   {'arg': 0, 'offset': 24, 'bytes': u64(ENTRY * new_capacity)}] if grow_fails else
                  [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                   {'arg': 0, 'offset': 16, 'pointer': f'grown+{ENTRY * count}'},
                   {'arg': 0, 'offset': 24, 'pointer': f'grown+{ENTRY * new_capacity}'}])
        c.stubs.append({'address': REALLOCATE, 'argc': 2, 'writes': writes})
        if not grow_fails:
            c.construct(f'grown+{ENTRY * count}', mount)
    else:
        c.construct(f'entries+{ENTRY * count}', mount)
    return c.json(args={'rdi': 'buf:table', 'rsi': mount})


def reallocate_case(cid, count, capacity, new_capacity, heaps=(), allocation_fails=False, null_table=False):
    c = Case(REALLOCATE, cid)
    for name, allocate, free in (('table_allocator', TABLE_ALLOCATE, TABLE_FREE),
                                 ('string_allocator', None, STRING_FREE)):
        c.buffers[name] = bytearray(b'\x3c' * 16)
        c.buffers[name + '_vtable'] = bytearray(b'\xe7' * 0x80)
        c.pointer(f'buf:{name}+0', f'{name}_vtable+0')
        c.memory.append({'addr': f'buf:{name}_vtable+{FREE_SLOT}', 'guest': free})
        if allocate:
            c.memory.append({'addr': f'buf:{name}_vtable+{ALLOCATE_SLOT}', 'guest': allocate})
    c.pointer('buf:table+32', 'table_allocator+0')
    c.entries('entries', capacity, heaps)
    if null_table:
        table_fields(c, 'entries', None, None, None)
    else:
        table_fields(c, 'entries', 0, count, capacity)
    c.buffers['fresh'] = bytearray(b'\x99' * max(ENTRY * new_capacity, 8))
    c.stubs.append({'address': TABLE_ALLOCATE, 'argc': 3, 'ret': 0 if allocation_fails else 'buf:fresh'})
    gs = None
    if allocation_fails:
        c.buffers['tls'] = bytearray(THREAD_BLOCK + 8)
        c.pointer(f'buf:tls+{THREAD_BLOCK}', f'tls+{THREAD_BLOCK}')
        c.memory.append({'addr': f'0x{TRANSACTION_FLAG_OFFSET:08x}', 'bytes': struct.pack('<q', -0x20).hex()})
        c.stubs.append({'address': FATAL_ERROR, 'argc': 3})
        gs = f'buf:tls+{THREAD_BLOCK}'
    else:
        for i in range(count):
            c.construct(f'fresh+{ENTRY * i}', f'entries+{ENTRY * i}')
    free_order = []
    if not null_table:
        for i in range(count):
            hn, ht = (i in heaps, i in heaps) if not isinstance(heaps, dict) else heaps.get(i, (False, False))
            if ht:
                free_order.append(STRING_FREE)
            if hn:
                free_order.append(STRING_FREE)
        free_order.append(TABLE_FREE)
    for address in free_order:
        c.stubs.append({'address': address, 'argc': 2})
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
        reallocate_case('inline_strings', 3, 3, 4),
        reallocate_case('heap_strings', 3, 3, 5, heaps={0: (True, False), 1: (False, True), 2: (True, True)}),
        reallocate_case('single', 1, 1, 2, heaps=(0,)),
        reallocate_case('empty_with_buffer', 0, 2, 3),
        reallocate_case('no_buffer', 0, 0, 1, null_table=True),
        reallocate_case('partly_full', 2, 6, 9, heaps={1: (True, True)}),
        reallocate_case('shrink_to_count', 4, 7, 4),
        reallocate_case('fails_empty', 0, 2, 3, allocation_fails=True),
        reallocate_case('fails_no_buffer', 0, 0, 1, allocation_fails=True, null_table=True),
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
