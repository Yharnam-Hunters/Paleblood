#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the narrow and the wide string's assign from counted text
(game/engine/string_assign_text.cpp), for tools/verify.py run.

usage: string_assign_text_cases.py OUT_DIR narrow|wide

The string is laid out at the start of `strings` (0x38 bytes, its first word and the bytes after
the owns byte a pattern) with heap text in a buffer of its own. The characters are a buffer of
their own, a pointer into the string's own characters, null, or a pointer just outside them.
memcpy and memmove (wmemcpy and wmemmove for the wide string) are scripted imports that write the
characters the real ones would; the growth (0x02a2dac0 narrow, 0x02a2a500 wide) is a stub that
installs a new heap buffer the way the real one does. The too-long throw does not return in the
game and is not cased (a stub that returns would run on into code the game never reaches); the
longest count that does not throw is cased with a heap pointer the terminator's write wraps back
into the buffer.
"""
import json
import os
import struct
import sys

NPOS = (1 << 64) - 1


class Kind:
    def __init__(self, address, grow, copy, move, inline, width, max_length):
        self.address, self.grow, self.copy, self.move = address, grow, copy, move
        self.inline, self.width, self.max_length = inline, width, max_length

    def encode(self, text):
        return text.encode('utf-16-le') if self.width == 2 else text.encode('latin-1')


ADDRESSES = {'narrow': '0x02a2d8e0', 'wide': '0x02a2a310'}
KINDS = {
    'narrow': Kind(ADDRESSES['narrow'], '0x02a2dac0', 'memcpy', 'memmove', 16, 1, NPOS - 1),
    'wide': Kind(ADDRESSES['wide'], '0x02a2a500', 'wmemcpy', 'wmemmove', 8, 2, 0x7ffffffffffffffe),
}
STRING = 0x38


def u64(v):
    return struct.pack('<Q', v & NPOS).hex()


class Case:
    def __init__(self, kind, cid):
        self.kind, self.cid = kind, cid
        self.buffers = {'strings': bytearray(b'\x5a' * 0x40)}
        self.memory, self.imports, self.stubs = [], [], []

    def string(self, text, capacity, size=None, heap=None):
        """The string: inline below the inline capacity, else on its own heap buffer (`heap`
        overrides the heap pointer: 0, or a buffer reference)."""
        k, raw = self.kind, self.buffers['strings']
        size = len(text) if size is None else size
        raw[0:8] = struct.pack('<Q', 0x1111)
        if capacity < k.inline:
            raw[8:0x18] = k.encode(text).ljust(16, b'\0')[:16]
        elif heap is not None:
            if heap == 0:
                raw[8:0x10] = bytes(8)
            else:
                self.memory.append({'addr': 'buf:strings+8', 'pointer': heap})
        else:
            room = min(capacity, max(len(text), size, 32))
            self.buffers['heap'] = bytearray(k.encode(text).ljust(k.width * (room + 1), b'\0'))
            self.memory.append({'addr': 'buf:strings+8', 'pointer': 'heap+0'})
        raw[0x18:0x28] = struct.pack('<QQ', size & NPOS, capacity & NPOS)
        raw[0x28:0x30] = struct.pack('<Q', 0x00000000deadbeef)
        raw[0x30] = 1
        raw[0x31:0x38] = b'\x6b' * 7

    def own(self, capacity):
        return ('heap', 0) if capacity >= self.kind.inline else ('strings', 8)

    def scripted(self, name, text, ret):
        self.imports.append({'name': name, 'argc': 3, 'ret': ret,
                             'writes': [{'arg': 0, 'bytes': self.kind.encode(text).hex()}]})

    def json(self, characters, count):
        return {'schema': 1, 'address': self.kind.address, 'id': self.cid, 'returns': 'i64',
                'args': {'rdi': 'buf:strings+0', 'rsi': characters, 'rdx': hex(count & NPOS)},
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.buffers.items()},
                'memory': self.memory, 'imports': self.imports, 'stubs': self.stubs}


def grown(kind, count):
    mask = kind.inline - 1
    return max(count | mask, kind.inline)


def case(kind, cid, target, text, count, inside=None, pointer=None, heap=None):
    """target: (text, capacity[, size]). The characters are `text` in a buffer of their own, or
    start `inside` characters into the target's own text; `pointer` overrides the argument (null,
    or a reference just outside the text)."""
    c = Case(kind, cid)
    t_text, t_cap = target[0], target[1]
    t_size = target[2] if len(target) > 2 else len(t_text)
    c.string(t_text, t_cap, t_size, heap)
    own_buffer, own_offset = c.own(t_cap)
    own = f'buf:{own_buffer}+{own_offset}'
    if inside is not None:
        assert inside < t_size
        length = min(t_size - inside, count)
        if inside != 0 and length:
            c.scripted(kind.move, t_text[inside:inside + length], own)
        characters = f'buf:{own_buffer}+{own_offset + kind.width * inside}'
    else:
        if text:
            c.buffers['text'] = bytearray(kind.encode(text).ljust(kind.width * (len(text) + 1), b'\0'))
        characters = pointer if pointer is not None else 'buf:text'
        destination = own
        if t_cap < count:
            capacity = grown(kind, count)
            c.buffers['grown'] = bytearray(b'\x99' * (kind.width * (count + 1)))
            c.stubs.append({'address': kind.grow, 'argc': 3, 'argmem': [{'arg': 0, 'size': STRING}],
                            'writes': [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                                       {'arg': 0, 'offset': 0x18, 'bytes': u64(t_size)},
                                       {'arg': 0, 'offset': 0x20, 'bytes': u64(capacity)}]})
            destination = 'buf:grown'
        if count:
            c.scripted(kind.copy, text[:count], destination)
    return c.json(characters, count)


def longest_case(kind, cid):
    """The longest count that does not throw, into a string that already has that capacity: the
    terminator at that count wraps around to two characters before the heap pointer."""
    c = Case(kind, cid)
    c.buffers['heap'] = bytearray(b'\x77' * 0x20)
    c.string('', kind.max_length, size=3, heap=f'heap+{2 * kind.width}')
    c.buffers['text'] = bytearray(kind.encode('abcd'))
    c.scripted(kind.copy, 'zz', f'buf:heap+{2 * kind.width}')
    return c.json('buf:text', kind.max_length)


def cases(name):
    kind = KINDS[name]
    full = 'f' * (kind.inline - 1)
    heap_text = 'a long heap text, twenty-four'[:kind.inline + 8]
    heap = (heap_text, kind.inline + 15)
    return [
        case(kind, 'inline_fits', ('xy', kind.inline - 1), 'abc', 3),
        case(kind, 'inline_exactly_full', ('', kind.inline - 1), full, kind.inline - 1),
        case(kind, 'one_past_inline_grows', ('ab', kind.inline - 1), full + 'gg', kind.inline),
        case(kind, 'grows_keeps_size', ('abcde', kind.inline - 1), 'q' * 40, 40),
        case(kind, 'count_shorter_than_text', ('xy', kind.inline - 1), 'abcdef', 2),
        case(kind, 'count_zero_empties', ('xyz', kind.inline - 1), 'abc', 0),
        case(kind, 'count_zero_heap', heap, 'abc', 0),
        case(kind, 'heap_capacity_at_inline_size', ('heap', kind.inline), 'abc', 3),
        case(kind, 'heap_fits', heap, 'h' * (kind.inline + 15), kind.inline + 15),
        case(kind, 'heap_grows', heap, 'g' * (kind.inline + 20), kind.inline + 20),
        case(kind, 'null_count_zero', ('xyz', kind.inline - 1), '', 0, pointer=0),
        case(kind, 'null_count_zero_heap', heap, '', 0, pointer=0),
        case(kind, 'null_text_null_heap_grows', ('', kind.inline, 5), '', 40, pointer=0, heap=0),
        case(kind, 'just_before_own_text', ('abcdef', kind.inline - 1), 'q', 1,
             pointer=f'buf:strings+{8 - kind.width}'),
        case(kind, 'at_own_end', ('abcdef', kind.inline - 1), 'zz', 2,
             pointer=f'buf:strings+{8 + 6 * kind.width}'),
        case(kind, 'at_own_end_count_zero', ('abcdef', kind.inline - 1), '', 0,
             pointer=f'buf:strings+{8 + 6 * kind.width}'),
        case(kind, 'past_size_inside_capacity', ('abcdef', kind.inline - 1, 3), 'dd', 2,
             pointer=f'buf:strings+{8 + 4 * kind.width}'),
        case(kind, 'inside_whole', ('abcdef', kind.inline - 1), '', 6, inside=0),
        case(kind, 'inside_front', ('abcdef', kind.inline - 1), '', 3, inside=0),
        case(kind, 'inside_rest', ('abcdef', kind.inline - 1), '', NPOS, inside=2),
        case(kind, 'inside_exact_rest', ('abcdef', kind.inline - 1), '', 4, inside=2),
        case(kind, 'inside_middle', ('abcdef', kind.inline - 1), '', 2, inside=1),
        case(kind, 'inside_last', ('abcdef', kind.inline - 1), '', 1, inside=5),
        case(kind, 'inside_count_zero', ('abcdef', kind.inline - 1), '', 0, inside=3),
        case(kind, 'inside_front_count_zero', ('abcdef', kind.inline - 1), '', 0, inside=0),
        case(kind, 'inside_heap_middle', heap, '', 3, inside=2),
        case(kind, 'inside_heap_rest', heap, '', NPOS, inside=6),
        case(kind, 'inside_shorter_size', ('abcdefgh'[:kind.inline - 1], kind.inline - 1, 4), '', 9, inside=1),
        longest_case(kind, 'longest_count'),
    ]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in KINDS:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    written = cases(sys.argv[2])
    for c in written:
        with open(os.path.join(out, c['id'] + '.json'), 'w') as f:
            json.dump(c, f, indent=1)
            f.write('\n')
    print(f'{len(written)} {sys.argv[2]} cases written to {out}')


if __name__ == '__main__':
    main()
