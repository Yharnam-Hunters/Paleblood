#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the wide string's assign of a repeated character
(game/engine/wide_string_assign_repeated.cpp), for tools/verify.py run.

usage: wide_string_assign_repeated_cases.py OUT_DIR

The string sits in `strings` (0x38 bytes) with its old text inline or in a heap buffer of its own;
both are filled with a pattern past the text, so the terminator's place shows. The growth
(0x02a27b70) is a stub that compares the string and installs a new heap buffer and capacity, as
the real one does; wmemset is a scripted import that writes the characters. Covers counts 0, 1,
2 and up to the capacity, inline and on the heap (a heap capacity of exactly 8 included); growth from inline and from the heap, including
to a single character; characters 0, 0xffff and a lone surrogate; and garbage above the
character's 16 bits. A count too long throws in the game (no return) and is not cased: a stub
that returns would run on into code the game never reaches. Generated fixtures, not game data.
"""
import json
import os
import struct
import sys

ADDRESS, GROW = '0x02a2a650', '0x02a27b70'
INLINE = 8


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def utf16(text):
    return b''.join(struct.pack('<H', ch) for ch in text)


def grown_capacity(length, old):
    rounded = length | 7
    half = old >> 1
    return rounded if half <= rounded // 3 else old + half


def case(cid, count, capacity, old='abc', character=0x41, extra=0):
    """old: the old text (a str); capacity below 8 keeps it inline. extra: bits above the character."""
    old_chars = [ord(ch) for ch in old]
    raw = bytearray(b'\x5a' * 0x38)
    raw[0:8] = struct.pack('<Q', 0x1111)
    raw[0x18:0x28] = struct.pack('<QQ', len(old_chars), capacity)
    raw[0x30] = 1
    buffers, memory, imports, stubs = {}, [], [], []
    if capacity < INLINE:
        raw[8:0x18] = utf16(old_chars).ljust(16, b'\x77')[:16]
        destination = 'buf:strings+8'
    else:
        buffers['heap'] = utf16(old_chars).ljust(2 * capacity + 2, b'\x77')
        memory.append({'addr': 'buf:strings+8', 'pointer': 'heap+0'})
        destination = 'buf:heap'
    buffers['strings'] = bytes(raw)
    if capacity < count:
        grown = grown_capacity(count, capacity)
        buffers['grown'] = utf16(old_chars).ljust(2 * grown + 2, b'\x99')
        stubs.append({'address': GROW, 'argc': 3, 'argmem': [{'arg': 0, 'size': 0x38}],
                      'writes': [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                                 {'arg': 0, 'offset': 0x20, 'bytes': u64(grown)}]})
        destination = 'buf:grown'
    stubs.append({'address': GROW, 'argc': 3})
    if count > 1:
        imports.append({'name': 'wmemset', 'argc': 3, 'ret': destination,
                        'writes': [{'arg': 0, 'bytes': utf16([character] * count).hex()}]})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:strings+0', 'rsi': hex(count), 'rdx': hex(character | extra)},
            'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in buffers.items()},
            'memory': memory, 'imports': imports, 'stubs': stubs}


def cases():
    return [
        case('inline_zero', 0, 7), case('inline_zero_empty', 0, 7, old=''),
        case('inline_one', 1, 7), case('inline_two', 2, 7), case('inline_full', 7, 7, old='abcdefg'),
        case('inline_shorter_than_old', 2, 7, old='abcdef'),
        case('heap_zero', 0, 15, old='abcdefghij'), case('heap_one', 1, 15, old='abcdefghij'),
        case('heap_capacity_8_zero', 0, 8, old='abcdefgh'), case('heap_capacity_8_one', 1, 8),
        case('heap_capacity_8_full', 8, 8), case('heap_two', 2, 15), case('heap_full', 15, 15), case('heap_longer_than_old', 12, 23, old='hello'),
        case('grow_from_inline', 8, 7), case('grow_from_inline_long', 30, 7, old='abcdefg'),
        case('grow_from_heap', 20, 15, old='abcdefghijklmno'), case('grow_from_heap_by_half', 16, 15),
        case('grow_to_one', 1, 0, old=''),
        case('character_zero', 3, 7, character=0), case('character_ffff', 4, 15, character=0xffff),
        case('character_surrogate', 5, 7, character=0xd800),
        case('character_one_ffff', 1, 7, character=0xffff),
        case('character_upper_garbage', 3, 7, extra=0xdead0000),
        case('character_one_upper_garbage', 1, 15, extra=0xbeef0000),
    ]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
