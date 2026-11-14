#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine array push_back copies for 4-byte elements
(game/engine/array32_push_back.cpp), for tools/verify.py run.

usage: array32_push_back_cases.py OUT_DIR ADDRESS   (one of the copies in REALLOCATE)

Each case lays out an engine array (`array`: begin, end, capacity end, allocator) over distinct
32-bit values (`values`) and a value to append (`value`, or one of the array's own). The copy's
reallocation (REALLOCATE) is a scripted stub that installs a new buffer (`grown`, holding other
values than the old one, so a read through the old pointer shows) or, for a failed allocation,
null pointers; it has a spare scripted call. One value lies just past a full array (not inside
it). Two cases set the array's words directly to counts near the top of the size range, where the
growth rule changes. The cases are those of param/id_array_push_back_cases.py.
Generated fixtures, not game data.
"""
import json
import os
import struct
import sys

# Each copy and the reallocation it calls.
REALLOCATE = {
    '0x016e85a0': '0x016e8680',
    '0x01b0d510': '0x02d75ce0',
    '0x01c15110': '0x01c151f0',
    '0x01efddb0': '0x01effa90',
    '0x022a89a0': '0x022ae660',
    '0x02b9e1b0': '0x02b9fa90',
    '0x02bda260': '0x02bda340',
    '0x02bda520': '0x02bda600',
    '0x02bdc9b0': '0x02bda600',
    '0x02c044f0': '0x02c045d0',
    '0x02c048e0': '0x02bdef60',
    '0x02c249a0': '0x02c26a50',
    '0x02c2abc0': '0x02c2b200',
    '0x02d75be0': '0x02d75ce0',
}
MASK = 0xffffffffffffffff
MAX_VALUES = MASK // 4


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


def grown_capacity(count):
    half = count >> 1
    capacity = count + half if MAX_VALUES - half >= count else 0
    return max(capacity, count + 1) & MASK


def values(name, count):
    base = sum(name.encode()) << 16
    return b''.join(struct.pack('<I', (base + 0x101 * (i + 1)) & 0xffffffff) for i in range(count)) or bytes(4)


class Case:
    def __init__(self, address, cid):
        self.address, self.reallocate = address, REALLOCATE[address]
        self.cid = cid
        self.buffers, self.memory, self.stubs = {'array': b'\x77' * 0x28}, [], []

    def json(self, item):
        self.stubs.append({'address': self.reallocate, 'argc': 2})
        return {'schema': 1, 'address': self.address, 'id': self.cid, 'returns': 'void',
                'args': {'rdi': 'buf:array', 'rsi': item},
                'buffers': {k: {'size': len(v), 'bytes': v.hex()} for k, v in self.buffers.items()},
                'memory': self.memory, 'stubs': self.stubs, 'imports': []}


def push_case(address, cid, count, capacity, source=None, grow_fails=False):
    c = Case(address, cid)
    c.buffers['values'] = values('values', max(capacity, 0 if source is None else source + 1))
    if count or capacity:
        for field, at in ((8, 0), (16, count), (24, capacity)):
            c.memory.append({'addr': f'buf:array+{field}', 'pointer': f'values+{4 * at}'})
    else:
        for field in (8, 16, 24):
            c.memory.append({'addr': f'buf:array+{field}', 'bytes': u64(0)})
    if source is None:
        c.buffers['value'] = values('value', 1) + b'\x99' * 4
        item = 'buf:value'
    else:
        item = f'buf:values+{4 * source}'
    if count == capacity:
        new_capacity = grown_capacity(count)
        c.buffers['grown'] = values('grown', new_capacity)
        writes = ([{'arg': 0, 'offset': f, 'bytes': u64(0)} for f in (8, 16)] +
                  [{'arg': 0, 'offset': 24, 'bytes': u64(4 * new_capacity)}] if grow_fails else
                  [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                   {'arg': 0, 'offset': 16, 'pointer': f'grown+{4 * count}'},
                   {'arg': 0, 'offset': 24, 'pointer': f'grown+{4 * new_capacity}'}])
        c.stubs.append({'address': c.reallocate, 'argc': 2, 'writes': writes})
    return c.json(item)


def huge_push_case(address, cid, begin, count):
    """An array (with no real buffer) whose begin and end words make `count` values, full."""
    c = Case(address, cid)
    end = (begin + 4 * count) & MASK
    for field, word in ((8, begin), (16, end), (24, end)):
        c.memory.append({'addr': f'buf:array+{field}', 'bytes': u64(word)})
    c.buffers['value'] = values('value', 1)
    c.buffers['grown'] = values('grown', 2)
    c.stubs.append({'address': c.reallocate, 'argc': 2,
                    'writes': [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                               {'arg': 0, 'offset': 16, 'pointer': 'grown+4'},
                               {'arg': 0, 'offset': 24, 'pointer': 'grown+8'}]})
    return c.json('buf:value')


def push_cases(address):
    a = address
    return [
        push_case(a, 'room_outside', 2, 4),
        push_case(a, 'room_inside_first', 2, 4, source=0),
        push_case(a, 'room_inside_last', 3, 4, source=2),
        push_case(a, 'room_at_end_slot', 1, 3, source=1),
        push_case(a, 'room_after_empty', 0, 2),
        push_case(a, 'full_outside_one', 1, 1),
        push_case(a, 'full_outside_two', 2, 2),
        push_case(a, 'full_outside_five', 5, 5),
        push_case(a, 'full_outside_eight', 8, 8),
        push_case(a, 'full_inside_first', 3, 3, source=0),
        push_case(a, 'full_inside_middle', 5, 5, source=2),
        push_case(a, 'full_inside_last', 4, 4, source=3),
        # The value just past a full array's end (not inside it: read from the old buffer).
        push_case(a, 'full_value_at_end', 3, 3, source=3),
        push_case(a, 'empty_no_buffer', 0, 0),
        push_case(a, 'growth_fails', 0, 0, grow_fails=True),
        push_case(a, 'growth_fails_inside', 2, 2, source=1, grow_fails=True),
        # Above the value buffers: a count whose half again still fits, and one (a negative byte
        # difference, read as a huge count) where only one more is given.
        huge_push_case(a, 'huge_count_grows_half', 0x7000000000000000, 0x0800000000000000),
        huge_push_case(a, 'huge_count_one_more', 0x7000000000000000, 0x3000000000000000),
    ]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in REALLOCATE:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = push_cases(sys.argv[2])
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
