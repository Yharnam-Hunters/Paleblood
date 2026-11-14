#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the pointer arrays' push_back and the resource manager's lookup of an info
(game/loading/list_push_find.cpp), for tools/verify.py run.

usage: list_push_find_cases.py OUT_DIR [transfer_push|request_push|find_info]

push_back (transfer_push 0x02a6b340, request_push 0x02a6c690): each case lays out an engine array
(`array`: begin, end, capacity end, allocator) over distinct pointer values (`items`) and a pointer
to append (`item`, or one of the array's own). The reallocation (0x02a6b420, 0x02a6f3d0) is a
scripted stub that installs a new buffer (`grown`, holding other values than the old one, so a
read through the old pointer shows) or, for a failed allocation, null pointers. Two cases set the
array's words directly to counts near the top of the size range, where the growth rule changes.

find_info (0x024c1630): a manager whose slot array lies at +0xdb8 aligned up to 8 (the manager is
placed at offsets 0..7 into its buffer), over 0x60-byte slots: three words, the info, flags and a
wide name (inline below capacity 8, else on the heap). The request is four words passed on the
stack: a pointer to its name, then three words. The C library's lower-case table (_Getptolower)
is a scripted import returning a synthetic table, once per call the lookup makes (simulated
here); some entries carry a high byte, and one non-zero byte maps to 0.
"""
import json
import os
import struct
import sys

PUSH_ADDRESSES = {'transfer_push': ('0x02a6b340', '0x02a6b420'), 'request_push': ('0x02a6c690', '0x02a6f3d0')}
FIND_INFO = '0x024c1630'
ADDRESSES = [a for a, _ in PUSH_ADDRESSES.values()] + [FIND_INFO]
MASK = 0xffffffffffffffff
MAX_POINTERS = MASK // 8
SLOT = 0x60
SLOT_STORAGE = 0xdb8
STORAGE_ALIGNMENT = 8
NAME = 0x28
INLINE_CAPACITY = 8


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


def grown_capacity(count):
    half = count >> 1
    capacity = count + half if MAX_POINTERS - half >= count else 0
    return max(capacity, count + 1) & MASK


def value_of(name, i):
    return (0x5100000000000000 + (sum(name.encode()) << 32) + 0x1010 * (i + 1)) & MASK


class Case:
    def __init__(self, address, cid):
        self.address, self.cid = address, cid
        self.buffers, self.memory, self.stubs, self.imports = {}, [], [], []

    def pointer(self, at, target):
        self.memory.append({'addr': at, 'pointer': target})

    def json(self, args, returns='void', stack=None):
        c = {'schema': 1, 'address': self.address, 'id': self.cid, 'returns': returns, 'args': args,
             'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.buffers.items()},
             'memory': self.memory, 'stubs': self.stubs, 'imports': self.imports}
        if stack is not None:
            c['stack'] = stack
        return c


# push_back -----------------------------------------------------------------------------------

def pointers(name, count):
    return bytearray(b''.join(struct.pack('<Q', value_of(name, i)) for i in range(count))) or bytearray(8)


def push_case(mode, cid, count, capacity, source=None, grow_fails=False):
    address, reallocate = PUSH_ADDRESSES[mode]
    c = Case(address, cid)
    c.buffers['array'] = bytearray(b'\x77' * 0x28)
    c.buffers['items'] = pointers('items', capacity)
    if count or capacity:
        for field, at in ((8, 0), (16, count), (24, capacity)):
            c.pointer(f'buf:array+{field}', f'items+{8 * at}')
    else:
        for field in (8, 16, 24):
            c.memory.append({'addr': f'buf:array+{field}', 'bytes': u64(0)})
    if source is None:
        c.buffers['item'] = pointers('item', 1)
        item = 'buf:item'
    else:
        item = f'buf:items+{8 * source}'
    if count == capacity:
        new_capacity = grown_capacity(count)
        c.buffers['grown'] = pointers('grown', new_capacity)
        writes = ([{'arg': 0, 'offset': f, 'bytes': u64(0)} for f in (8, 16)] +
                  [{'arg': 0, 'offset': 24, 'bytes': u64(8 * new_capacity)}] if grow_fails else
                  [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                   {'arg': 0, 'offset': 16, 'pointer': f'grown+{8 * count}'},
                   {'arg': 0, 'offset': 24, 'pointer': f'grown+{8 * new_capacity}'}])
        c.stubs.append({'address': reallocate, 'argc': 2, 'writes': writes})
    return c.json({'rdi': 'buf:array', 'rsi': item})


def huge_push_case(mode, cid, begin, count):
    """An array (with no real buffer) whose begin and end words make `count` pointers, full."""
    address, reallocate = PUSH_ADDRESSES[mode]
    c = Case(address, cid)
    c.buffers['array'] = bytearray(b'\x77' * 0x28)
    end = (begin + 8 * count) & MASK
    for field, word in ((8, begin), (16, end), (24, end)):
        c.memory.append({'addr': f'buf:array+{field}', 'bytes': u64(word)})
    c.buffers['item'] = pointers('item', 1)
    c.buffers['grown'] = pointers('grown', 2)
    c.stubs.append({'address': reallocate, 'argc': 2,
                    'writes': [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                               {'arg': 0, 'offset': 16, 'pointer': 'grown+8'},
                               {'arg': 0, 'offset': 24, 'pointer': 'grown+16'}]})
    return c.json({'rdi': 'buf:array', 'rsi': 'buf:item'})


def push_cases(mode):
    return [
        push_case(mode, 'room_outside', 2, 4),
        push_case(mode, 'room_inside_first', 2, 4, source=0),
        push_case(mode, 'room_inside_last', 3, 4, source=2),
        push_case(mode, 'room_at_end_slot', 1, 3, source=1),
        push_case(mode, 'room_after_empty', 0, 2),
        push_case(mode, 'full_outside_one', 1, 1),
        push_case(mode, 'full_outside_two', 2, 2),
        push_case(mode, 'full_outside_five', 5, 5),
        push_case(mode, 'full_outside_eight', 8, 8),
        push_case(mode, 'full_inside_first', 3, 3, source=0),
        push_case(mode, 'full_inside_middle', 5, 5, source=2),
        push_case(mode, 'full_inside_last', 4, 4, source=3),
        push_case(mode, 'empty_no_buffer', 0, 0),
        push_case(mode, 'growth_fails', 0, 0, grow_fails=True),
        push_case(mode, 'growth_fails_inside', 2, 2, source=1, grow_fails=True),
        # Above the item buffers: a count whose half again still fits, and one (a negative byte
        # difference, read as a huge count) where only one more is given.
        huge_push_case(mode, 'huge_count_grows_half', 0x7000000000000000, 0x0800000000000000),
        huge_push_case(mode, 'huge_count_one_more', 0x2000000000000000, 0x1800000000000000),
    ]


# find_info -----------------------------------------------------------------------------------

def lower_table():
    table = [b for b in range(256)]
    for ch in range(ord('A'), ord('Z') + 1):
        table[ch] = ch + 0x20
    table[0xc0] = 0x01e0         # a high byte, dropped by the comparison
    table[0xc1] = 0x7ee1
    table[0x7f] = 0              # a character that folds to the end of a name
    table[0x80] = 0xff80         # negative as a byte
    return table


TABLE = lower_table()


def folded(ch):
    v = TABLE[ch & 0xff] & 0xff
    return v - 0x100 if v & 0x80 else v


def compare_calls(slot_name, request_name):
    """(match, table calls) of the original's comparison."""
    calls = 0
    for i in range(10 ** 6):
        a = slot_name[i] if i < len(slot_name) else 0
        b = request_name[i] if i < len(request_name) else 0
        calls += 2
        if folded(a) != folded(b):
            return False, calls
        if folded(a) == 0:
            return True, calls


def wide(chars):
    return b''.join(struct.pack('<H', ch) for ch in chars) + b'\0\0'


def chars(text):
    return [ord(ch) for ch in text] if isinstance(text, str) else list(text)


def find_case(cid, slots, request, shift=0, no_buffer=False):
    """slots: [(w00, w08, w10, name, heap)]; request: (name, w1, w2, w3)."""
    c = Case(FIND_INFO, cid)
    c.buffers['manager'] = bytearray(b'\x6b' * (SLOT_STORAGE + 0x30 + STORAGE_ALIGNMENT))
    # The array's place in the buffer (whose start is aligned): the storage rounded up to 8.
    storage = SLOT_STORAGE + shift
    array = (storage + STORAGE_ALIGNMENT - 1) // STORAGE_ALIGNMENT * STORAGE_ALIGNMENT
    raw = bytearray()
    for i, (w00, w08, w10, name, heap) in enumerate(slots):
        s = bytearray(b'\x5a' * SLOT)
        s[0:0x20] = struct.pack('<QQQQ', w00, w08, w10, value_of('info', i))
        name = chars(name)
        text = wide(name)
        if heap:
            c.buffers[f'name{i}'] = bytearray(text)
            capacity = max(INLINE_CAPACITY, len(name))
        else:
            s[NAME + 8:NAME + 8 + len(text)] = text
            capacity = INLINE_CAPACITY - 1
        s[NAME + 0x18:NAME + 0x28] = struct.pack('<QQ', len(name), capacity)
        raw += s
    c.buffers['slots'] = raw or bytearray(8)
    for i, slot in enumerate(slots):
        if slot[4]:
            c.pointer(f'buf:slots+{SLOT * i + NAME + 8}', f'name{i}+0')
    if no_buffer:
        for field in (8, 16):
            c.memory.append({'addr': f'buf:manager+{array + field}', 'bytes': u64(0)})
    else:
        c.pointer(f'buf:manager+{array + 8}', 'slots+0')
        c.pointer(f'buf:manager+{array + 16}', f'slots+{SLOT * len(slots)}')
    name, w1, w2, w3 = request
    c.buffers['request_name'] = bytearray(wide(chars(name)))
    c.buffers['table'] = bytearray(b''.join(struct.pack('<H', v) for v in TABLE))
    for i in reversed(range(len(slots))):
        w00, w08, w10, slot_name, _ = slots[i]
        if (w00, w08, w10) != (w3, w1, w2):
            continue
        match, calls = compare_calls(chars(slot_name), chars(name))
        c.imports += [{'name': '_Getptolower', 'argc': 0, 'ret': 'buf:table'}] * calls
        if match:
            break
    return c.json({'rdi': f'buf:manager+{shift}'}, returns='i64',
                  stack=['buf:request_name', w1, w2, w3])


W1, W2, W3 = 0x1111, 0x2222, 0x3333
REQ = ('texture', W1, W2, W3)


def slot(name='texture', heap=False, w00=W3, w08=W1, w10=W2):
    return (w00, w08, w10, name, heap)


def find_cases():
    cases = [
        find_case('no_buffer', [], REQ, no_buffer=True),
        find_case('empty', [], REQ),
        find_case('one_inline_match', [slot('abc')], ('abc', W1, W2, W3)),
        find_case('one_heap_match', [slot(heap=True)], REQ),
        find_case('capacity_seven_inline', [slot('abcdefg')], ('abcdefg', W1, W2, W3)),
        find_case('capacity_eight_heap', [slot('abcdefgh', heap=True)], ('abcdefgh', W1, W2, W3)),
        find_case('case_folded', [slot('TeXtUrE', heap=True)], REQ),
        find_case('request_upper', [slot()], ('TEXTURE', W1, W2, W3)),
        find_case('last_of_two_matches', [slot(), slot()], REQ),
        find_case('first_slot_only', [slot(), slot('other'), slot('another', heap=True)], REQ),
        find_case('word_0x00_differs', [slot(), slot(w00=W3 + 1)], REQ),
        find_case('word_0x08_differs', [slot(), slot(w08=W1 + 1)], REQ),
        find_case('word_0x10_differs', [slot(), slot(w10=W2 + 1)], REQ),
        find_case('words_rotated', [slot(), slot(w00=W1, w08=W2, w10=W3)], REQ),
        find_case('words_in_order', [slot(), slot(w00=W1, w08=W2, w10=W3)], (REQ[0], W2, W3, W1)),
        find_case('name_differs_middle', [slot(), slot('texTure_x', heap=True)], ('texture_y', W1, W2, W3)),
        find_case('slot_name_shorter', [slot(), slot('text')], REQ),
        find_case('request_name_shorter', [slot(), slot()], ('text', W1, W2, W3)),
        find_case('none_matches', [slot('a'), slot(w08=0), slot('b', heap=True)], REQ),
        find_case('empty_names', [slot(''), slot('')], ('', W1, W2, W3)),
        find_case('empty_request_name', [slot('x')], ('', W1, W2, W3)),
        find_case('low_byte_only', [slot([0x141, 0x142, 0x63])], ('abc', W1, W2, W3)),
        find_case('high_table_byte_dropped', [slot([0xc0, 0xc1])], ([0xe0, 0xe1], W1, W2, W3)),
        find_case('negative_byte', [slot([0x80, 0x41])], ([0x80, 0x61], W1, W2, W3)),
        find_case('folds_to_end', [slot([0x61, 0x7f, 0x62])], ([0x61, 0x7f, 0x63, 0x64], W1, W2, W3)),
        find_case('request_folds_to_end', [slot([0x61, 0x62])], ([0x61, 0x7f], W1, W2, W3)),
        find_case('slot_name_high_zero_byte', [slot([0x61, 0x200])], ([0x61], W1, W2, W3)),
    ]
    for shift in range(1, STORAGE_ALIGNMENT):
        cases.append(find_case(f'manager_offset_{shift}', [slot('x'), slot(), slot('y')], REQ, shift=shift))
    return cases


def main():
    modes = tuple(PUSH_ADDRESSES) + ('find_info',)
    if len(sys.argv) != 3 or sys.argv[2] not in modes:
        sys.exit(__doc__)
    out, which = sys.argv[1], sys.argv[2]
    generated = find_cases() if which == 'find_info' else push_cases(which)
    os.makedirs(out, exist_ok=True)
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
