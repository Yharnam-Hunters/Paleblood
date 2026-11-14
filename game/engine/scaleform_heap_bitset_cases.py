#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the Scaleform heap page bitset (game/engine/scaleform_heap_bitset_mark.cpp,
scaleform_heap_bitset_units.cpp), for tools/verify.py run. Each replacement's own generator calls
this one with its function.

usage: scaleform_heap_bitset_cases.py OUT_DIR mark|units

The bitset is a 0x50-byte buffer (its 0x40 bytes and the words a long block's length can reach past
them). Marking runs on bitsets of all zero, all one and mixed bits, for every length class, at units
where the codes cross a word boundary. Reading runs on bitsets holding each length class's code
(written as the marking does) with neighbouring codes set.
"""
import json
import os
import struct
import sys

MARK, UNITS = '0x027ec640', '0x027ec890'
SIZE = 0x50
PATTERNS = {'zero': b'\x00' * SIZE, 'ones': b'\xff' * SIZE,
            'mixed': bytes((i * 0x9d + 0x5a) & 0xff for i in range(SIZE))}


def put(words, unit, code):
    shift = (2 * unit) & 0x1e
    words[unit >> 4] = (words[unit >> 4] & ~(3 << shift) & 0xffffffff) | ((code << shift) & 0xffffffff)


def encode(words, unit, units):
    if 3 <= units <= 5:
        put(words, unit, 3)
        put(words, unit + 1, units - 3)
    elif units == 2:
        put(words, unit, 2)
    elif units <= 1:
        put(words, unit, 1)
        return
    elif units <= 0x25:
        for i, code in enumerate((3, 3, (units - 6) >> 4, ((units - 6) >> 2) & 3, (units - 6) & 3)):
            put(words, unit + i, code)
    else:
        for i in range(3):
            put(words, unit + i, 3)
        words[(2 * unit + 0x25) >> 5] = units & 0xffffffff
    put(words, unit + units - 1, 1)


def case(address, cid, args, data, returns):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns,
            'args': dict({'rdi': 'buf:bitset'}, **args),
            'buffers': {'bitset': {'size': SIZE, 'bytes': data.hex()}}, 'memory': [], 'stubs': [], 'imports': []}


def mark_cases():
    out = []
    lengths = (0, 1, 2, 3, 4, 5, 6, 7, 0x15, 0x25, 0x26, 0x40, 0xa0)
    for pattern, data in PATTERNS.items():
        for units in lengths:
            for unit in (0, 14, 15, 33):
                if (2 * unit + 0x25) >> 5 >= SIZE // 4 or unit + units > SIZE * 4:
                    continue
                out.append(case(MARK, f'{pattern}_{units:#x}_at_{unit}', {'rsi': unit, 'rdx': units}, data, 'void'))
    return out


def units_cases():
    out = []
    lengths = (1, 2, 3, 4, 5, 6, 7, 0x10, 0x15, 0x25, 0x26, 0x40, 0xa0, 0x12345)
    for pattern in ('zero', 'ones', 'mixed'):
        for units in lengths:
            for unit in (0, 14, 15, 33):
                words = list(struct.unpack(f'<{SIZE // 4}I', PATTERNS[pattern]))
                if (2 * unit + 0x25) >> 5 >= len(words):
                    continue
                # Only the codes the reader looks at are written; the last unit may lie outside.
                end = unit + units - 1
                if end >= SIZE * 4:
                    put_end = False
                else:
                    put_end = True
                saved = list(words)
                encode(words, unit, min(units, SIZE * 4 - unit) if not put_end else units)
                if not put_end and units > 0x25:
                    words[(2 * unit + 0x25) >> 5] = units
                data = struct.pack(f'<{len(words)}I', *words)
                out.append(case(UNITS, f'{pattern}_{units:#x}_at_{unit}', {'rsi': unit}, data, 'i32'))
                words = saved
    # A free unit (code 0) reads as 0 units.
    out.append(case(UNITS, 'free_unit', {'rsi': 5}, PATTERNS['zero'], 'i32'))
    return out


WHICH = {'mark': mark_cases, 'units': units_cases}


def main(argv):
    if len(argv) != 2 or argv[1] not in WHICH:
        sys.exit(__doc__)
    out, which = argv
    os.makedirs(out, exist_ok=True)
    cases = WHICH[which]()
    for c in cases:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {out}')


if __name__ == '__main__':
    main(sys.argv[1:])
