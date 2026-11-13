#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the wide string's assign from part of another, its assign from counted text and
its growth (game/engine/wide_string_assign.cpp), for tools/verify.py run.

usage: wide_string_assign_cases.py OUT_DIR [assign|grow|counted]

Strings are laid out in `strings` (the target at 0x00, the source at 0x40, 0x38 bytes each) with
heap text in buffers of their own. wmemcpy and wmemmove are scripted imports that write the
characters the real ones would. For assign the growth (0x02a27b70) is a stub that installs a new
heap buffer the way the real one does (as for the counted assign, whose text is a buffer of its
own, the target's own characters, or a pointer just outside them); for grow the allocator is a fake object whose allocate
and free slots point at scripted stubs. The out-of-range and too-long throws, and a failed
allocation (whose report returns into a null buffer in a stubbed run), do not return in the game
and are not cased: a stub that returns would run on into code the game never reaches.
"""
import json
import os
import struct
import sys

ASSIGN, GROW, COUNTED = '0x02a279a0', '0x02a27b70', '0x02a2a720'
ALLOCATE, FREE = '0x00500200', '0x00500220'
ALLOCATE_SLOT, FREE_SLOT = 0x58, 0x70
TARGET, SOURCE = 0x00, 0x40
NPOS = (1 << 64) - 1
MAX_CAPACITY = 0x7ffffffffffffffe
INLINE = 8


def u64(v):
    return struct.pack('<Q', v & NPOS).hex()


def utf16(text):
    return text.encode('utf-16-le')


def grown_capacity(length, old):
    rounded = length | 7
    if rounded > MAX_CAPACITY:
        return length
    half = old >> 1
    if half <= rounded // 3:
        return rounded
    return old + half if old <= MAX_CAPACITY - half else MAX_CAPACITY


class Case:
    def __init__(self, address, cid):
        self.address, self.cid = address, cid
        self.buffers = {'strings': bytearray(b'\x5a' * 0x80), 'allocator': bytearray(b'\x3c' * 16),
                        'vtable': bytearray(b'\xe7' * 0x80)}
        self.memory = [{'addr': 'buf:allocator+0', 'pointer': 'vtable+0'},
                       {'addr': f'buf:vtable+{ALLOCATE_SLOT}', 'guest': ALLOCATE},
                       {'addr': f'buf:vtable+{FREE_SLOT}', 'guest': FREE}]
        self.imports, self.stubs = [], []

    def string(self, at, name, text, capacity, size=None):
        """A string at `at` in `strings`: inline below capacity 8, else on its own heap buffer."""
        raw = self.buffers['strings']
        size = len(text) if size is None else size
        raw[at:at + 8] = struct.pack('<Q', 0x1111 + at)
        if capacity < INLINE:
            raw[at + 8:at + 0x18] = utf16(text).ljust(16, b'\0')[:16]
        else:
            # Backing memory for the text and its terminator, not the whole (possibly huge) capacity.
            room = min(capacity, max(len(text), size, 32))
            self.buffers[name] = bytearray(utf16(text).ljust(2 * room + 2, b'\0'))
            self.memory.append({'addr': f'buf:strings+{at + 8}', 'pointer': f'{name}+0'})
        raw[at + 0x18:at + 0x28] = struct.pack('<QQ', size, capacity)
        self.memory.append({'addr': f'buf:strings+{at + 0x28}', 'pointer': 'allocator+0'})
        raw[at + 0x30] = 1
        raw[at + 0x31:at + 0x38] = b'\x6b' * 7

    def copy(self, text, ret):
        """A scripted wmemcpy or wmemmove writing `text` through its destination."""
        return {'argc': 3, 'ret': ret, 'writes': [{'arg': 0, 'bytes': utf16(text).hex()}]}

    def json(self, returns, args):
        return {'schema': 1, 'address': self.address, 'id': self.cid, 'returns': returns, 'args': args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.buffers.items()},
                'memory': self.memory, 'imports': self.imports, 'stubs': self.stubs}


def text_ref(capacity, at, name):
    return f'buf:{name}' if capacity >= INLINE else f'buf:strings+{at + 8}'


def assign_case(cid, target, source, position, count, self_assign=False):
    """target, source: (text, capacity). The source must reach `position` (no throw)."""
    c = Case(ASSIGN, cid)
    t_text, t_cap = target
    s_text, s_cap = source
    c.string(TARGET, 'target_heap', t_text, t_cap)
    if not self_assign:
        c.string(SOURCE, 'source_heap', s_text, s_cap)
    else:
        s_text, s_cap = t_text, t_cap
    assert position <= len(s_text)
    length = min(len(s_text) - position, count)
    part = s_text[position:position + length]
    if self_assign:
        end = position + length
        if end > position and position != 0:
            c.imports.append({'name': 'wmemmove', **c.copy(part, text_ref(t_cap, TARGET, 'target_heap'))})
        source_arg = 'buf:strings+0'
    else:
        source_arg = f'buf:strings+{SOURCE}'
        destination = text_ref(t_cap, TARGET, 'target_heap')
        if t_cap < length:
            capacity = grown_capacity(length, t_cap)
            c.buffers['grown'] = bytearray(b'\x99' * (2 * capacity + 2))
            keep = t_text[:len(t_text)]
            c.stubs.append({'address': GROW, 'argc': 3, 'argmem': [{'arg': 0, 'size': 0x30}],
                            'writes': [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                                       {'arg': 0, 'offset': 0x18, 'bytes': u64(len(keep))},
                                       {'arg': 0, 'offset': 0x20, 'bytes': u64(capacity)}]})
            destination = 'buf:grown'
        if length:
            c.imports.append({'name': 'wmemcpy', **c.copy(part, destination)})
    return c.json('i64', {'rdi': 'buf:strings+0', 'rsi': source_arg, 'rdx': hex(position), 'rcx': hex(count & NPOS)})


def counted_case(cid, target, text, count, inside=None, pointer=None):
    """target: (text, capacity). The characters are `text` in a buffer of their own, or start
    `inside` characters into the target's own text; `pointer` overrides the argument (null, or
    just outside the text)."""
    c = Case(COUNTED, cid)
    t_text, t_cap = target
    c.string(TARGET, 'target_heap', t_text, t_cap)
    own_buffer, own_offset = ('target_heap', 0) if t_cap >= INLINE else ('strings', TARGET + 8)
    own = f'buf:{own_buffer}+{own_offset}'
    if inside is not None:
        assert inside < len(t_text)
        length = min(len(t_text) - inside, count)
        if inside != 0 and length:
            c.imports.append({'name': 'wmemmove', **c.copy(t_text[inside:inside + length], own)})
        characters = f'buf:{own_buffer}+{own_offset + 2 * inside}'
    else:
        if text:
            c.buffers['text'] = bytearray(utf16(text).ljust(2 * len(text) + 2, b'\0'))
        characters = pointer if pointer is not None else 'buf:text'
        destination = own
        if t_cap < count:
            capacity = grown_capacity(count, t_cap)
            c.buffers['grown'] = bytearray(b'\x99' * (2 * capacity + 2))
            c.stubs.append({'address': GROW, 'argc': 3, 'argmem': [{'arg': 0, 'size': 0x30}],
                            'writes': [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                                       {'arg': 0, 'offset': 0x18, 'bytes': u64(len(t_text))},
                                       {'arg': 0, 'offset': 0x20, 'bytes': u64(capacity)}]})
            destination = 'buf:grown'
        if count:
            c.imports.append({'name': 'wmemcpy', **c.copy(text[:count], destination)})
    return c.json('i64', {'rdi': 'buf:strings+0', 'rsi': characters, 'rdx': hex(count & NPOS)})


def counted_cases():
    heap = ('mount/param0', 15)
    return [
        counted_case('inline_fits', ('xy', 7), 'abc', 3),
        counted_case('inline_exactly_full', ('', 7), 'seven77', 7),
        counted_case('one_past_inline_grows', ('ab', 7), 'eight888', 8),
        counted_case('count_shorter_than_text', ('xy', 7), 'abcdef', 2),
        counted_case('count_zero_empties', ('xyz', 7), 'abc', 0),
        counted_case('count_zero_heap', heap, 'abc', 0),
        counted_case('heap_fits', heap, 'a long heap str', 15),
        counted_case('heap_grows', ('heap target', 11), 'a source of nineteen', 20),
        counted_case('null_count_zero', ('xyz', 7), '', 0, pointer=0),
        counted_case('just_before_own_text', ('abcdef', 7), 'q', 1, pointer='buf:strings+6'),
        counted_case('at_own_end', ('abcdef', 7), '\0', 1, pointer='buf:strings+20'),
        counted_case('at_own_end_count_zero', ('abcdef', 7), '', 0, pointer='buf:strings+20'),
        counted_case('inside_whole', ('abcdef', 7), '', 6, inside=0),
        counted_case('inside_front', ('abcdef', 7), '', 3, inside=0),
        counted_case('inside_tail', ('abcdef', 7), '', 100, inside=2),
        counted_case('inside_last', ('abcdef', 7), '', 1, inside=5),
        counted_case('inside_count_zero', ('abcdef', 7), '', 0, inside=3),
        counted_case('inside_heap_middle', heap, '', 3, inside=2),
        counted_case('inside_heap_rest', heap, '', NPOS, inside=6),
    ]


def grow_case(cid, text, capacity, length, keep, size=None, fresh_capacity=None):
    c = Case(GROW, cid)
    c.string(TARGET, 'old_heap', text, capacity, size)
    new_capacity = grown_capacity(length, capacity)
    fresh = fresh_capacity if fresh_capacity is not None else new_capacity
    c.buffers['fresh'] = bytearray(b'\x99' * (2 * fresh + 2))
    c.stubs.append({'address': ALLOCATE, 'argc': 3, 'ret': 'buf:fresh'})
    if keep:
        c.imports.append({'name': 'wmemcpy', **c.copy(text[:keep], 'buf:fresh')})
    if capacity >= INLINE:
        c.stubs.append({'address': FREE, 'argc': 2})
    return c.json('void', {'rdi': 'buf:strings+0', 'rsi': hex(length), 'rdx': hex(keep)})


def assign_cases():
    abc, long12 = ('abc', 7), ('mount/param0', 15)
    return [
        assign_case('inline_all', ('xy', 7), abc, 0, NPOS),
        assign_case('inline_middle_one', ('xy', 7), abc, 1, 1),
        assign_case('count_zero_empties', ('xyz', 7), abc, 0, 0),
        assign_case('position_at_end_empties', ('xyz', 7), abc, 3, NPOS),
        assign_case('heap_target_empties', ('a long heap str', 15), abc, 3, 5),
        assign_case('count_past_end', ('', 7), abc, 1, 100),
        assign_case('exactly_inline_capacity', ('', 7), ('seven77', 7), 0, NPOS),
        assign_case('one_past_inline_grows', ('ab', 7), ('eight888', 15), 0, NPOS),
        assign_case('heap_source_grows', ('ab', 7), long12, 2, NPOS),
        assign_case('heap_source_part_fits', ('ab', 7), long12, 6, 5),
        assign_case('heap_target_fits', ('a long heap str', 15), long12, 0, NPOS),
        assign_case('heap_target_exact', ('', 12), long12, 0, NPOS),
        assign_case('heap_target_grows', ('heap target', 11), ('a source of nineteen', 31), 1, NPOS),
        assign_case('heap_to_heap_middle', ('a long heap str', 15), long12, 3, 4),
        assign_case('self_whole', ('abcdef', 7), ('', 7), 0, NPOS, self_assign=True),
        assign_case('self_front', ('abcdef', 7), ('', 7), 0, 3, self_assign=True),
        assign_case('self_tail', ('abcdef', 7), ('', 7), 2, NPOS, self_assign=True),
        assign_case('self_middle_heap', ('mount/param0', 15), ('', 15), 2, 3, self_assign=True),
        assign_case('self_at_end', ('abcdef', 7), ('', 7), 6, NPOS, self_assign=True),
        assign_case('self_count_zero', ('mount/param0', 15), ('', 15), 4, 0, self_assign=True),
        assign_case('self_empty', ('', 7), ('', 7), 0, NPOS, self_assign=True),
    ]


def grow_cases():
    return [
        grow_case('inline_to_heap', 'abc', 7, 8, 3),
        grow_case('inline_keep_none', 'abc', 7, 8, 0),
        grow_case('inline_keep_all', 'seven77', 7, 9, 7),
        grow_case('heap_rounds_up', 'mount/param0', 15, 16, 10),
        grow_case('heap_half_again', 'h' * 20, 100, 101, 20),
        grow_case('heap_rounded_wins', 'h' * 20, 20, 64, 20),
        grow_case('heap_keep_none_frees', 'mount/param0', 15, 40, 0),
        grow_case('heap_capacity_eight', 'eight888', 8, 9, 8),
        grow_case('keep_more_than_size', 'abc', 7, 12, 5, size=3),
        grow_case('near_max_capacity', 'abc', 0x7ffffffffffffff0, 0x7ffffffffffffff1, 3, fresh_capacity=4),
        grow_case('too_long_to_round', 'abc', 7, 0x7ffffffffffffffa, 3, fresh_capacity=4),
        grow_case('half_exceeds_max', 'abc', 0x7ffffffffffffff9, 0x7ffffffffffffffa, 3, fresh_capacity=4),
    ]


VARIANTS = {'assign': assign_cases, 'grow': grow_cases, 'counted': counted_cases}


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] not in VARIANTS):
        sys.exit(__doc__)
    which = sys.argv[2] if len(sys.argv) == 3 else 'assign'
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    cases = VARIANTS[which]()
    for c in cases:
        with open(os.path.join(out, c['id'] + '.json'), 'w') as f:
            json.dump(c, f, indent=1)
            f.write('\n')
    print(f'{len(cases)} {which} cases written to {out}')


if __name__ == '__main__':
    main()
