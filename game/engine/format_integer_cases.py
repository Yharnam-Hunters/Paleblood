#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for DLFormat's integer conversions (game/engine/format_integer.cpp), for
tools/verify.py run.

usage: format_integer_cases.py OUT_DIR text|write|integer

text: the number's text (out buffer prefilled) for values at the 32/64-bit edges in every base
edge (including invalid ones) and flag combination. write: the formatter's output state (`state`,
0x30 bytes, its cursor into `out`) and a synthesized number text (`digits`, followed by a stop
character so the '#' prefix scan ends), over widths, precisions, rooms, flags and count-only.
integer: the whole conversion, with a va_list (`va`) whose register save area (`regs`) and stack
area (`stack`) hold different values so the gp_offset decides which is read. The digit tables are
the executable's own.
"""
import json
import os
import struct
import sys

TEXT, WRITE, INTEGER = '0x024e6c70', '0x024e6f00', '0x024e70e0'
MAX64 = 0xffffffffffffffff

ZERO, SIGNED, PLUS, SPACE, LEFT, ALT, CAPITAL, KEEP_ZERO = 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x100


def state_bytes(room, flags, width, precision, length, count_only):
    data = bytearray(b'\x3c' * 0x30)
    data[0x10:0x14] = struct.pack('<i', room)
    data[0x18:0x1c] = struct.pack('<I', flags & 0xffffffff)
    data[0x1c:0x20] = struct.pack('<i', width)
    data[0x20:0x24] = struct.pack('<I', precision & 0xffffffff)
    data[0x26:0x28] = struct.pack('<h', length)
    data[0x28] = 1 if count_only else 0
    return data.hex()


def state_case(address, cid, room, flags, width, precision, length, count_only):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i8',
            'args': {'rdi': 'buf:state'},
            'buffers': {'state': {'size': 0x30,
                                  'bytes': state_bytes(room, flags, width, precision, length, count_only)},
                        'out': {'size': 0x100, 'bytes': 'ab' * 0x100}},
            'memory': [{'addr': 'buf:state+8', 'pointer': 'out'}], 'imports': [], 'stubs': []}


def text_case(cid, value, base, flags):
    return {'schema': 1, 'address': TEXT, 'id': cid, 'returns': 'i32',
            'args': {'rdi': 'buf:out', 'rsi': hex(value & MAX64), 'rdx': hex(base & 0xffffffff),
                     'rcx': hex(flags)},
            'buffers': {'out': {'size': 0xa0, 'bytes': 'cd' * 0xa0}},
            'memory': [], 'imports': [], 'stubs': []}


def write_case(cid, text, length=None, room=40, flags=0, width=0, precision=-1, count_only=False):
    case = state_case(WRITE, cid, room, flags, width, precision, 0, count_only)
    data = (text + 'Q').encode('utf-16-le')
    case['args']['rsi'] = 'buf:digits'
    case['args']['rdx'] = hex((len(text) if length is None else length) & 0xffffffff)
    case['buffers']['digits'] = {'size': 0x80, 'bytes': (data + b'\x51\x00' * 0x40)[:0x80].hex()}
    return case


def integer_case(cid, conversion, value, flags=0, length=0, width=0, precision=-1, room=40,
                 count_only=False, gp_offset=8):
    case = state_case(INTEGER, cid, room, flags, width, precision, length, count_only)
    case['args']['rsi'] = hex(ord(conversion))
    case['args']['rdx'] = 'buf:va'
    regs = bytearray(64)
    for slot in range(8):
        struct.pack_into('<Q', regs, slot * 8, (value + slot * 0x1111) & MAX64)
    struct.pack_into('<Q', regs, gp_offset if gp_offset <= 40 else 0, value & MAX64)
    stack = struct.pack('<QQ', (value if gp_offset > 40 else ~value) & MAX64, 0x7777)
    case['buffers']['va'] = {'size': 24, 'bytes': struct.pack('<II', gp_offset, 48).hex() + '00' * 16}
    case['buffers']['regs'] = {'size': 64, 'bytes': bytes(regs).hex()}
    case['buffers']['stack'] = {'size': 16, 'bytes': stack.hex()}
    case['memory'] += [{'addr': 'buf:va+8', 'pointer': 'stack'}, {'addr': 'buf:va+16', 'pointer': 'regs'}]
    return case


VALUES = [0, 1, 7, 8, 9, 10, 15, 16, 35, 36, 255, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000,
          0x7fffffffffffffff, 0x8000000000000000, MAX64, MAX64 - 9, 1234567890123]
BASES = [-1, 0, 1, 2, 3, 8, 10, 16, 35, 36, 37, 0x80000002]
TEXT_FLAGS = [0, SIGNED, SIGNED | PLUS, ALT, ALT | CAPITAL, SIGNED | ALT | PLUS | CAPITAL, PLUS, SPACE | SIGNED]

TEXTS = ['0', '7', '42', '-42', '+42', '-0', '+0', '0x1f', '0X1F', '017', '00', '0x0', '-0x10',
         '123456789', 'x', '', 'ff']


def make(which):
    out = []
    if which == 'text':
        for v in VALUES:
            for b in BASES:
                for f in TEXT_FLAGS:
                    out.append(text_case(f'v{v:x}_b{b & 0xffffffff:x}_f{f:x}', v, b, f))
    elif which == 'write':
        for text in TEXTS:
            for flags in (0, ZERO, LEFT, SPACE, ALT, ZERO | ALT, LEFT | ALT, KEEP_ZERO, SPACE | ZERO,
                          LEFT | ZERO | SPACE):
                for width in (-1, 0, 1, 3, 8):
                    for precision in (-1, 0, 1, 4):
                        out.append(write_case(f't{text}_f{flags:x}_w{width}_p{precision}', text,
                                              flags=flags, width=width, precision=precision))
            for room in (-1, 0, 1, 2, 5):
                out.append(write_case(f't{text}_room{room}_w4', text, room=room, width=4, precision=2))
                out.append(write_case(f't{text}_room{room}', text, room=room))
            out.append(write_case(f't{text}_count_only', text, flags=ALT, width=8, precision=4, count_only=True))
            out.append(write_case(f't{text}_count_only_left', text, flags=LEFT, width=9, count_only=True))
            out.append(write_case(f't{text}_neg_length', text, length=-3, width=4))
            out.append(write_case(f't{text}_long_length', text, length=len(text) + 2, width=2))
    else:
        for conv in 'diuoxbXcs':
            for value in (0, 1, 8, 255, 0x7fffffff, 0x80000000, 0xffffffff, 0x123456789, MAX64,
                          0x8000000000000000):
                for length in (0, ord('h'), ord('l'), ord('L'), ord('I'), ord('q')):
                    out.append(integer_case(f'{conv}_v{value:x}_l{length:x}', conv, value, length=length))
                for flags in (ALT, ALT | CAPITAL, PLUS | SPACE, SPACE, SIGNED, ZERO | LEFT, ALT | ZERO):
                    out.append(integer_case(f'{conv}_v{value:x}_f{flags:x}', conv, value, flags=flags,
                                            width=12, precision=-1))
                out.append(integer_case(f'{conv}_v{value:x}_p0', conv, value, flags=ALT, precision=0))
                out.append(integer_case(f'{conv}_v{value:x}_stack', conv, value, gp_offset=48))
                out.append(integer_case(f'{conv}_v{value:x}_last_reg', conv, value, gp_offset=40))
                out.append(integer_case(f'{conv}_v{value:x}_l_stack', conv, value, length=ord('l'), gp_offset=48))
                out.append(integer_case(f'{conv}_v{value:x}_room2', conv, value, width=4, room=2))
                out.append(integer_case(f'{conv}_v{value:x}_count', conv, value, flags=ALT, width=10,
                                        count_only=True))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ('text', 'write', 'integer'):
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for index, item in enumerate(make(sys.argv[2])):
        name = ''.join(c if c.isalnum() or c in '_-' else '_' for c in item['id'])
        with open(os.path.join(sys.argv[1], f'{index:04d}_{name}.json'), 'w') as handle:
            json.dump(item, handle, indent=1)


if __name__ == '__main__':
    main()
