#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine array push_back copies for 8-byte elements
(game/engine/array64_push_back.cpp), for tools/verify.py run.

usage: array64_push_back_cases.py OUT_DIR ADDRESS   (one of the copies in REALLOCATE)

Each case lays out an engine array (`array`: begin, end, capacity end, allocator) over distinct
64-bit values (`values`) and a value to append (`value`, or one of the array's own). The copy's
reallocation (REALLOCATE) is a scripted stub that installs a new buffer (`grown`, holding other
values than the old one, so a read through the old pointer shows) or, for a failed allocation,
null pointers; it has a spare scripted call. One value lies just past a full array (not inside
it). Two cases set the array's words directly to counts near the top of the size range, where the
growth rule changes. The cases are those of loading/list_push_find_cases.py's push_back modes.
Generated fixtures, not game data.
"""
import json
import os
import struct
import sys

# Each copy and the reallocation it calls.
REALLOCATE = {
    '0x016a5050': '0x016a5130',
    '0x016b0c80': '0x016b1290',
    '0x016d1ee0': '0x016d1fc0',
    '0x016d9e90': '0x016d9f70',
    '0x016e0880': '0x016e1490',
    '0x0179c2d0': '0x0179c3b0',
    '0x0179fcb0': '0x0179fd90',
    '0x01860dc0': '0x018615f0',
    '0x0187e030': '0x0187df30',
    '0x0189a450': '0x0189a350',
    '0x0189a530': '0x0189a350',
    '0x0189e890': '0x0189e790',
    '0x018bf3c0': '0x018bf2c0',
    '0x019048a0': '0x01904bb0',
    '0x0193ec80': '0x0193ed60',
    '0x01b3bbb0': '0x01b3bf60',
    '0x01b3bc90': '0x01b3c060',
    '0x01c42880': '0x01c42780',
    '0x01c62780': '0x01c62b70',
    '0x01ceb3b0': '0x01ceb6a0',
    '0x01e8d1a0': '0x01e8d280',
    '0x0215ea10': '0x0215f2f0',
    '0x0223b420': '0x0223b500',
    '0x02255a70': '0x02255b50',
    '0x02266a70': '0x02266eb0',
    '0x02266b50': '0x02266fb0',
    '0x02273a10': '0x02273af0',
    '0x022ab310': '0x022b03e0',
    '0x022ac430': '0x022b0720',
    '0x02463460': '0x02463540',
    '0x024708f0': '0x02471310',
    '0x024709d0': '0x02471410',
    '0x024fa490': '0x024fa650',
    '0x024fa570': '0x024fa750',
    '0x0254d640': '0x0254d870',
    '0x025593a0': '0x02559480',
    '0x025c4550': '0x025c4630',
    '0x0260f940': '0x0260fdf0',
    '0x0265c0f0': '0x0265c6a0',
    '0x0265c860': '0x0265e210',
    '0x02682310': '0x02682520',
    '0x02686b10': '0x02686cd0',
    '0x02686bf0': '0x02686dd0',
    '0x026bc560': '0x026c07c0',
    '0x026bc640': '0x026c08c0',
    '0x026f47c0': '0x026f4c50',
    '0x02705560': '0x02706ac0',
    '0x02705dd0': '0x02707280',
    '0x027071a0': '0x027077a0',
    '0x02a451a0': '0x02a46410',
    '0x02a6c360': '0x02a6f3d0',
    '0x02a6e880': '0x02a6e960',
    '0x02a76110': '0x02a76a30',
    '0x02a77a80': '0x02a78620',
    '0x02a8c9c0': '0x02a8e840',
    '0x02a92fd0': '0x02a930b0',
    '0x02aa2e10': '0x02aa3430',
    '0x02b15b40': '0x02b15c20',
    '0x02b17680': '0x02b17760',
    '0x02b18970': '0x02b18a60',
    '0x02b1c050': '0x02b1c130',
    '0x02b22010': '0x02b220f0',
    '0x02b267d0': '0x02b26b10',
    '0x02b29eb0': '0x02b2a280',
    '0x02b2dce0': '0x02b2e010',
    '0x02b2e110': '0x02b2e1f0',
    '0x02b2e390': '0x02b2e590',
    '0x02b30330': '0x02b30c50',
    '0x02b32310': '0x02b32400',
    '0x02b32500': '0x02b325f0',
    '0x02b33ed0': '0x02b33fc0',
    '0x02b34780': '0x02b34ac0',
    '0x02b38c10': '0x02b3ae50',
    '0x02b3a3c0': '0x02b3ad50',
    '0x02b3b980': '0x02b3ba60',
    '0x02b55040': '0x02b5c400',
    '0x02b56440': '0x02b5ae80',
    '0x02b9e290': '0x02b9f980',
    '0x02b9e370': '0x02b9f870',
    '0x02ba2c00': '0x02ba3260',
    '0x02ba8070': '0x02ba85a0',
    '0x02bba920': '0x02bbb730',
    '0x02bc0ed0': '0x02bc2580',
    '0x02bc0fb0': '0x02bc2480',
    '0x02bc1940': '0x02bc2280',
    '0x02bc6090': '0x02bc6170',
    '0x02bc6770': '0x02bc6850',
    '0x02bc6e90': '0x02bc73d0',
    '0x02bc6f70': '0x02bc72c0',
    '0x02bca980': '0x02bcaa60',
    '0x02bcac20': '0x02bcad00',
    '0x02bcb0f0': '0x02bcb1d0',
    '0x02bda440': '0x02bda710',
    '0x02bdf5f0': '0x02bdfa20',
    '0x02be1770': '0x02be1af0',
    '0x02be1850': '0x02be19f0',
    '0x02bf2ee0': '0x02c04cb0',
    '0x02bf2fc0': '0x02c03ca0',
    '0x02bfd230': '0x02c03680',
    '0x02c015d0': '0x02c01ea0',
    '0x02c025d0': '0x02c026b0',
    '0x02c03bb0': '0x02c03ca0',
    '0x02c046d0': '0x02bdfa20',
    '0x02c049c0': '0x02bdee60',
    '0x02c08dc0': '0x02c0a100',
    '0x02c12250': '0x02c12de0',
    '0x02c16840': '0x02c17760',
    '0x02c19850': '0x02c19a90',
    '0x02c1a250': '0x02c17760',
    '0x02c2b300': '0x02c2b3f0',
    '0x02c52f30': '0x02c54a90',
    '0x02c69460': '0x02c69d80',
    '0x02cf9870': '0x02cf9ac0',
    '0x02d063d0': '0x02d064b0',
    '0x02d18230': '0x02d27cd0',
    '0x02d18310': '0x02d1ad80',
    '0x02d2a210': '0x02d2a2f0',
    '0x02d2ccf0': '0x02d2dcb0',
    '0x02d3ff30': '0x02d413b0',
    '0x02d416c0': '0x02d27cd0',
    '0x02d43ed0': '0x02d43fb0',
    '0x02d440b0': '0x02d44190',
    '0x02d44c20': '0x02d44d00',
    '0x02d48c40': '0x02d48ea0',
    '0x02d49370': '0x02d49460',
    '0x02d4ac10': '0x02d4acf0',
    '0x02d4b000': '0x02d4c0b0',
    '0x02d55f20': '0x02d56420',
    '0x02d68440': '0x02d691f0',
    '0x02d68520': '0x02d691f0',
    '0x02d68600': '0x02d690f0',
    '0x02d686f0': '0x02d689b0',
    '0x02d687d0': '0x02d688b0',
    '0x02d6a870': '0x02d6ade0',
    '0x02d6a950': '0x02d6ace0',
    '0x02d6aa30': '0x02d6abe0',
    '0x02d92bb0': '0x02d94450',
    '0x02dab230': '0x02dab5f0',
    '0x02dae530': '0x02dae610',
    '0x02daee50': '0x02daf010',
    '0x02db1be0': '0x02db1cc0',
    '0x02db5020': '0x02db5140',
    '0x02db7c60': '0x02db8230',
    '0x02dba280': '0x02dba990',
    '0x02dc1f10': '0x02dc2010',
    '0x02dce210': '0x02dce2f0',
    '0x02dcf190': '0x02dd07f0',
    '0x02dd1cf0': '0x02dd2370',
    '0x02dd1dd0': '0x02dd2270',
    '0x02dd1eb0': '0x02dd2170',
    '0x02dd1f90': '0x02dd2070',
    '0x02dd2470': '0x02dd2590',
    '0x02dd6d30': '0x02dd6e20',
    '0x02dd7cb0': '0x02dd8270',
    '0x02dd9440': '0x02dd9a30',
    '0x02dd9520': '0x02dd9930',
    '0x02ddbb60': '0x02ddbc40',
    '0x02ddd0f0': '0x02ddf5c0',
    '0x02ddd1d0': '0x02ddf4c0',
    '0x02ddd310': '0x02ddf4c0',
    '0x02ddd630': '0x02ddef60',
    '0x02ddd710': '0x02dddf20',
    '0x02ddf860': '0x02ddf990',
    '0x02de1420': '0x02de1510',
    '0x02de46a0': '0x02de55c0',
    '0x02de4780': '0x02de54c0',
    '0x02e60b70': '0x02e60d80',
    '0x02e70980': '0x02e73830',
    '0x02e70f90': '0x02e73730',
    '0x02e73930': '0x02e73ca0',
    '0x02ec88e0': '0x02ec98d0',
    '0x02ec89c0': '0x02ec98d0',
    '0x02ecc5c0': '0x02ecc6a0',
    '0x02ed5480': '0x02ed5560',
    '0x02edc700': '0x02edc7e0',
    '0x02ee6e20': '0x02ee6f00',
    '0x02f6e0e0': '0x02f6e1c0',
    '0x02f8eda0': '0x02f90730',
}
MASK = 0xffffffffffffffff
MAX_VALUES = MASK // 8


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


def grown_capacity(count):
    half = count >> 1
    capacity = count + half if MAX_VALUES - half >= count else 0
    return max(capacity, count + 1) & MASK


def values(name, count):
    base = sum(name.encode()) << 40
    return b''.join(struct.pack('<Q', (base + 0x10101 * (i + 1)) & MASK) for i in range(count)) or bytes(8)


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
            c.memory.append({'addr': f'buf:array+{field}', 'pointer': f'values+{8 * at}'})
    else:
        for field in (8, 16, 24):
            c.memory.append({'addr': f'buf:array+{field}', 'bytes': u64(0)})
    if source is None:
        c.buffers['value'] = values('value', 1) + b'\x99' * 8
        item = 'buf:value'
    else:
        item = f'buf:values+{8 * source}'
    if count == capacity:
        new_capacity = grown_capacity(count)
        c.buffers['grown'] = values('grown', new_capacity)
        writes = ([{'arg': 0, 'offset': f, 'bytes': u64(0)} for f in (8, 16)] +
                  [{'arg': 0, 'offset': 24, 'bytes': u64(8 * new_capacity)}] if grow_fails else
                  [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                   {'arg': 0, 'offset': 16, 'pointer': f'grown+{8 * count}'},
                   {'arg': 0, 'offset': 24, 'pointer': f'grown+{8 * new_capacity}'}])
        c.stubs.append({'address': c.reallocate, 'argc': 2, 'writes': writes})
    return c.json(item)


def huge_push_case(address, cid, begin, count):
    """An array (with no real buffer) whose begin and end words make `count` values, full."""
    c = Case(address, cid)
    end = (begin + 8 * count) & MASK
    for field, word in ((8, begin), (16, end), (24, end)):
        c.memory.append({'addr': f'buf:array+{field}', 'bytes': u64(word)})
    c.buffers['value'] = values('value', 1)
    c.buffers['grown'] = values('grown', 2)
    c.stubs.append({'address': c.reallocate, 'argc': 2,
                    'writes': [{'arg': 0, 'offset': 8, 'pointer': 'grown+0'},
                               {'arg': 0, 'offset': 16, 'pointer': 'grown+8'},
                               {'arg': 0, 'offset': 24, 'pointer': 'grown+16'}]})
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
        huge_push_case(a, 'huge_count_grows_half', 0x7000000000000000, 0x0400000000000000),
        huge_push_case(a, 'huge_count_one_more', 0x7000000000000000, 0x1800000000000000),
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
