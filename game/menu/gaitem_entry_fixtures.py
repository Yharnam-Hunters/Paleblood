#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the item dialog entry copies in game/menu/gaitem_dialog_entries.cpp,
for tools/verify.py run. Each function has its own generator, <name without menu_>_cases.py,
which calls write() here.

The engine's wide string assign (0x02a279a0) and the pool's out-of-range report are scripted
stubs: each assign is compared with its arguments and with the target string as it stands then
(begun empty on the source's allocator). The allocator pointers are only copied. All entries and
text are invented.
"""
import json
import os
import struct
import sys

FATAL_ERROR, ASSIGN = '0x024b55b0', '0x02a279a0'
SELECT_SIZE, STATUS_SIZE, STRING_SIZE = 0x88, 0x90, 0x38
POOL_COUNT = 0x888
ALLOCATOR = 0x00007a0000001000  # never called

FUNCTIONS = {
    'menu_gaitem_select_entry_copy_into_pool': ('0x01edf560', 'select'),
    'menu_gaitem_status_entry_copy': ('0x01f1bca0', 'status'),
}


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff)


def string_bytes(text, owns, allocator=ALLOCATOR, heap_capacity=None):
    """A wide string object; with heap_capacity, its text is in buffer 'heap' (pointer patched by
    the caller through the returned memory entry offset)."""
    data = bytearray(b'\x77' * STRING_SIZE)
    data[0:8] = u64(0x5151515151515151)
    encoded = text.encode('utf-16-le') + b'\0\0'
    if heap_capacity is None:
        data[8:24] = encoded.ljust(16, b'\0')
        capacity = 7
    else:
        capacity = heap_capacity
    data[0x18:0x20] = u64(len(text))
    data[0x20:0x28] = u64(capacity)
    data[0x28:0x30] = u64(allocator)
    data[0x30] = owns
    return bytes(data), encoded


def select_entry(key, words, texts, owns=(1, 0), heap=(None, None)):
    data = bytearray(b'\x44' * SELECT_SIZE)
    data[0:4] = struct.pack('<i', key)
    data[8:16] = u64(words[0])
    data[0x48:0x50] = u64(words[1])
    heaps = []
    for k, at in enumerate((0x10, 0x50)):
        s, encoded = string_bytes(texts[k], owns[k], ALLOCATOR + 0x100 * k, heap[k])
        data[at:at + STRING_SIZE] = s
        heaps.append((at + 8, encoded) if heap[k] is not None else None)
    return bytes(data), heaps


def status_entry(word0, word40, texts, shared, byte, owns=(0, 1), heap=(None, None)):
    data = bytearray(b'\x55' * STATUS_SIZE)
    data[0:8] = u64(word0)
    data[0x40:0x48] = u64(word40)
    heaps = []
    for k, at in enumerate((0x08, 0x48)):
        s, encoded = string_bytes(texts[k], owns[k], ALLOCATOR + 0x100 * k, heap[k])
        data[at:at + STRING_SIZE] = s
        heaps.append((at + 8, encoded) if heap[k] is not None else None)
    data[0x88] = byte
    return bytes(data), heaps, shared


ASSIGN_STUB = {'address': ASSIGN, 'ret': 0, 'argc': 4, 'argmem': [{'arg': 0, 'size': STRING_SIZE}]}


def base_case(address, cid, returns, args, buffers, memory, stubs=()):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': buffers, 'memory': memory, 'stubs': list(stubs), 'imports': []}


def heap_memory(buffer, base, heaps, memory, buffers, prefix):
    for n, h in enumerate(heaps):
        if h is None:
            continue
        at, encoded = h
        name = f'{prefix}_heap{n}'
        buffers[name] = {'size': 0x40, 'bytes': encoded.hex()}
        memory.append({'addr': f'buf:{buffer}+{base + at}', 'pointer': name})


def select_cases(address):
    def c(cid, slot_index, count, entry=None, pool_shift=0, slot=None, reported=None):
        entry_bytes, heaps = entry if entry is not None else select_entry(7, (0x1122334455667788, 9), ('abc', 'xyz12'))
        # The pool's bytes (and so the slots') start as garbage, so every copied field shows.
        pool = bytearray(b'\x99' * (POOL_COUNT + 8 + 16))
        pool[pool_shift + POOL_COUNT:pool_shift + POOL_COUNT + 8] = u64(count)
        buffers = {'pool': {'size': len(pool), 'bytes': pool.hex()},
                   'source': {'size': SELECT_SIZE, 'bytes': entry_bytes.hex()}}
        memory = []
        heap_memory('source', 0, heaps, memory, buffers, 'source')
        aligned = (pool_shift + 7) & ~7
        if slot is None:
            slot = f'buf:pool+{aligned + SELECT_SIZE * slot_index}'
        if reported is None:
            reported = slot_index >= count
        stubs = [{'address': FATAL_ERROR, 'ret': 0, 'argc': 3}] if reported else []
        if slot != 0:
            stubs += [dict(ASSIGN_STUB), dict(ASSIGN_STUB)]
        return base_case(address, cid, 'void', {'rdi': f'buf:pool+{pool_shift}', 'rsi': slot, 'rdx': 'buf:source'},
                         buffers, memory, stubs)

    return [
        c('first_slot', 0, 1), c('second_slot', 1, 2), c('last_of_full_pool', 14, 15),
        c('slot_at_count', 1, 1), c('slot_past_count', 3, 1), c('empty_pool', 0, 0),
        c('pool_misaligned_3', 0, 1, pool_shift=3), c('pool_misaligned_7_past', 1, 1, pool_shift=7),
        c('pool_misaligned_1_in', 1, 2, pool_shift=1),
        # The slots start at the rounded-up address, so this slot (one entry past the pool's own
        # address) is still in range.
        c('slot_between_ends', 1, 1, pool_shift=7, slot=f'buf:pool+{7 + SELECT_SIZE}', reported=False),
        # The end wraps around below the slot: reported.
        c('count_wraps', 2, 0x7fffffffffffffff, reported=True),
        c('empty_strings', 0, 1, select_entry(-1, (0, 0xffffffffffffffff), ('', ''), owns=(0, 0))),
        c('seven_characters', 0, 1, select_entry(0x7fffffff, (1, 2), ('abcdefg', 'z'), owns=(1, 1))),
        c('heap_sources', 0, 1, select_entry(-0x80000000, (3, 4), ('hello', 'abcdefg'), heap=(32, 8))),
        c('owns_other_values', 0, 1, select_entry(5, (5, 6), ('q', 'rs'), owns=(0x80, 0xff))),
        # A null slot is below the end (not reported) and is not copied into.
        c('null_slot', 0, 1, slot=0, reported=False),
        c('null_slot_empty_pool', 0, 0, slot=0, reported=False),
    ]


def status_cases(address):
    def c(cid, index, entries, base_index=0, shared_count=5):
        buffers = {'target': {'size': STATUS_SIZE, 'bytes': ('cd' * STATUS_SIZE)}}
        data = b''.join(e[0] for e in entries)
        buffers['entries'] = {'size': len(data), 'bytes': data.hex()}
        memory = []
        for k, (entry, heaps, shared) in enumerate(entries):
            heap_memory('entries', STATUS_SIZE * k, heaps, memory, buffers, f'e{k}')
            if shared:
                name = f'shared{k}'
                buffers[name] = {'size': 0x10, 'bytes': (b'\x11' * 8 + struct.pack('<i', shared_count)).hex()}
                memory.append({'addr': f'buf:entries+{STATUS_SIZE * k + 0x80}', 'pointer': name})
            else:
                memory.append({'addr': f'buf:entries+{STATUS_SIZE * k + 0x80}', 'bytes': u64(0).hex()})
        args = {'rdi': 'buf:target', 'rsi': f'buf:entries+{STATUS_SIZE * base_index}',
                'rdx': 0xaaaaaaaa00000000 | (index & 0xffffffff)}
        return base_case(address, cid, 'i64', args, buffers, memory, [dict(ASSIGN_STUB), dict(ASSIGN_STUB)])

    e = [status_entry(0x1000 + k, 0x2000 + k, (t, u), k != 1, 0x30 + k)
         for k, (t, u) in enumerate((('one', 'a'), ('two', 'bb'), ('three', 'ccc')))]
    heap = status_entry(9, 10, ('abcdef', 'xy'), True, 0xff, owns=(1, 0x7f), heap=(16, 100))
    empty = status_entry(0, 0xffffffffffffffff, ('', ''), False, 0, owns=(0, 0))
    full = status_entry(1, 2, ('abcdefg', 'gfedcba'), True, 1, owns=(1, 1))
    return [
        c('index_0', 0, e), c('index_1_no_shared', 1, e), c('index_2', 2, e),
        c('index_minus_1', -1, e, base_index=1), c('index_minus_2', -2, e, base_index=2),
        c('heap_sources', 0, [heap]), c('empty_strings', 0, [empty]), c('seven_characters', 0, [full]),
        c('shared_count_negative', 0, [full], shared_count=-1),
        c('shared_count_largest', 0, [full], shared_count=0x7fffffff),
    ]


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind = FUNCTIONS[name]
    cases = select_cases(address) if kind == 'select' else status_cases(address)
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
