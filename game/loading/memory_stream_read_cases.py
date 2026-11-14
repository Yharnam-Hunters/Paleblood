#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for reading from the input stream over memory
(game/loading/memory_stream_read.cpp), for tools/verify.py run.

usage: memory_stream_read_cases.py OUT_DIR

Each case gives a stream (vtable whose open slot 0x48 is a scripted stub; size, data pointer, the
word at +0x20 and the position) and a count. The engine's memory copy (0x02a1a1b0) is a scripted
stub, compared with its arguments (the length as it is passed, sign-extended).
"""
import json
import os
import struct
import sys

ADDRESS = '0x024de230'
IS_OPEN, MEMCPY = '0x00500200', '0x02a1a1b0'
MASK = 0xffffffffffffffff


def case(cid, size, position, count, open_=True, counter=0x1234):
    stream = struct.pack('<QQQQQQ', 0, 0x77, size & MASK, 0, counter & MASK, position & MASK)
    copied = min(count, (size - position) & MASK) & 0xffffffff
    if copied >= 0x80000000:
        copied = 0
    stubs = [{'address': IS_OPEN, 'argc': 1, 'ret': '0x1' if open_ else '0x0'}]
    if open_ and count and copied:
        stubs.append({'address': MEMCPY, 'argc': 3})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
            'args': {'rdi': 'buf:stream', 'rsi': 'buf:out', 'rdx': hex(count & MASK)},
            'buffers': {'stream': {'size': 0x30, 'bytes': stream.hex()}, 'vtable': {'size': 0x50},
                        'data': {'size': 0x40}, 'out': {'size': 0x40}},
            'memory': [{'addr': 'buf:stream+0', 'pointer': 'vtable+0'},
                       {'addr': 'buf:vtable+72', 'guest': IS_OPEN},
                       {'addr': 'buf:stream+24', 'pointer': 'data+0'}],
            'imports': [], 'stubs': stubs}


def cases():
    return [
        case('count_below_left', 0x40, 0x10, 8),
        case('count_above_left', 0x40, 0x30, 0x20),
        case('count_equals_left', 0x40, 0x20, 0x20),
        case('nothing_left', 0x40, 0x40, 8),
        case('zero_count', 0x40, 0, 0),
        case('not_open', 0x40, 0, 8, open_=False),
        case('not_open_zero_count', 0x40, 0, 0, open_=False),
        case('left_2gb', 0x80000000, 0, 0x80000000),
        case('count_wraps_to_5', 0x100000010, 0, 0x100000005),
        case('count_wraps_to_0', 0x200000000, 0, 0x100000000),
        case('position_past_size', 0x10, 0x20, 8),
        case('huge_count', 0x40, 0x3f, MASK, counter=MASK),
    ]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases()
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
