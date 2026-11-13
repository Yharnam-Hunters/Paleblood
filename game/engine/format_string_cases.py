#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for DLFormat's %s conversion (game/engine/format_string.cpp), for tools/verify.py run.

usage: format_string_cases.py OUT_DIR write_wide|argument

Each case builds the formatter's output state (`state`, 0x30 bytes: cursor into `text`, room,
flags, width, precision, length modifier, count-only flag) and, for the argument conversion, a
va_list (`va`) whose register save area (`regs`) and stack area (`stack`) both hold string
pointers, so the gp_offset decides which is read. wcslen / strlen are scripted imports whose
length may differ from the string (the code trusts it; the strings are long enough).
"""
import itertools
import json
import os
import struct
import sys

WRITE_WIDE, ARGUMENT = '0x024e77f0', '0x024e62b0'
NULL_TEXT = '0x04b384b4'   # L"<NULL>"
WIDE = 'Hunterあé wide string, long enough for every length used'
NARROW = b'Narrow \xe9\x82\xa0 bytes, long enough for every length used\x00'


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def state_bytes(room, flags, width, precision, length, count_only):
    data = bytearray(b'\x3c' * 0x30)
    data[0x10:0x14] = struct.pack('<i', room)
    data[0x18:0x1c] = struct.pack('<I', flags & 0xffffffff)
    data[0x1c:0x20] = struct.pack('<i', width)
    data[0x20:0x24] = struct.pack('<I', precision & 0xffffffff)
    data[0x26:0x28] = struct.pack('<h', length)
    data[0x28] = 1 if count_only else 0
    return data.hex()


def base(address, cid, room, flags, width, precision, length, count_only):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i8',
            'args': {'rdi': 'buf:state'},
            'buffers': {'state': {'size': 0x30, 'bytes': state_bytes(room, flags, width, precision, length, count_only)},
                        'text': {'size': 0x80, 'bytes': 'ab' * 0x80},
                        'wide': {'size': 0x80, 'bytes': WIDE.encode('utf-16-le')[:0x80].hex()},
                        'narrow': {'size': 0x40, 'bytes': NARROW[:0x40].hex()}},
            'memory': [{'addr': 'buf:state+8', 'pointer': 'text'}], 'imports': [], 'stubs': []}


def write_wide(cid, room=40, flags=0, width=0, precision=-1, count_only=False, text=True, length=10):
    case = base(WRITE_WIDE, cid, room, flags, width, precision, 0, count_only)
    case['args']['rsi'] = 'buf:wide' if text else '0x0'
    case['imports'].append({'name': 'wcslen', 'argc': 1, 'ret': hex(length if text else 6)})
    return case


def argument(cid, room=40, flags=0, width=0, precision=-1, count_only=False, length_modifier=0,
             text=True, length=10, gp_offset=8):
    case = base(ARGUMENT, cid, room, flags, width, precision, length_modifier, count_only)
    narrow = bool(flags & 0x40) and length_modifier == ord('h')
    target = ('narrow' if narrow else 'wide') if text else None
    case['args']['rsi'] = 'buf:va'
    case['buffers']['va'] = {'size': 24, 'bytes': u32(gp_offset) + u32(48) + '00' * 16}
    case['buffers']['regs'] = {'size': 64, 'bytes': '00' * 64}
    case['buffers']['stack'] = {'size': 16, 'bytes': '00' * 16}
    case['memory'] += [{'addr': 'buf:va+8', 'pointer': 'stack'}, {'addr': 'buf:va+16', 'pointer': 'regs'}]
    if target:
        if gp_offset <= 40:
            case['memory'].append({'addr': f'buf:regs+{gp_offset}', 'pointer': target})
        case['memory'].append({'addr': 'buf:stack+0', 'pointer': target})
    if narrow and text:
        case['imports'].append({'name': 'strlen', 'argc': 1, 'ret': hex(length)})
    else:
        case['imports'].append({'name': 'wcslen', 'argc': 1, 'ret': hex(length if text else 6)})
    return case


ROOMS = [-1, 0, 1, 2, 3, 6, 11, 40]
WIDTHS = [-1, 0, 1, 4, 12]
PRECISIONS = [-1, 0, 3, 10]
FLAGS = [0, 0x01, 0x10, 0x11, 0x40, 0xffffffef]
LENGTHS = [0, 5, 10, 0x100000005]


def make(which):
    out = []
    if which == 'write_wide':
        for room, width, precision in itertools.product(ROOMS, WIDTHS, PRECISIONS):
            out.append(write_wide(f'r{room}_w{width}_p{precision}', room=room, width=width, precision=precision))
        for flags, width, count_only in itertools.product(FLAGS, WIDTHS, (False, True)):
            out.append(write_wide(f'f{flags:x}_w{width}_c{int(count_only)}', flags=flags, width=width,
                                  count_only=count_only, room=11, length=5))
        for length, room in itertools.product(LENGTHS, ROOMS):
            out.append(write_wide(f'len{length:x}_r{room}', length=length, room=room, width=3, flags=0x11))
        for room, width, flags in itertools.product((3, 8, 40), (0, 5, 12), (0, 0x10)):
            out.append(write_wide(f'null_r{room}_w{width}_f{flags:x}', text=False, room=room, width=width, flags=flags))
    else:
        for flags, modifier in itertools.product(FLAGS, (0, ord('h'), ord('l'), ord('L'), 0x168)):
            for room, width in ((40, 0), (11, 8), (3, 0), (6, 12)):
                out.append(argument(f'f{flags:x}_m{modifier:x}_r{room}_w{width}', flags=flags,
                                    length_modifier=modifier, room=room, width=width))
        for flags, gp in itertools.product((0, 0x40), (0, 8, 40, 48, 0x29, 0x2f, 0x80000000)):
            out.append(argument(f'gp{gp:x}_f{flags:x}', flags=flags, length_modifier=ord('h'), gp_offset=gp))
        for flags, text, count_only in itertools.product((0, 0x40, 0x41, 0x50), (True, False), (False, True)):
            out.append(argument(f'text{int(text)}_f{flags:x}_c{int(count_only)}', flags=flags, text=text,
                                length_modifier=ord('h'), count_only=count_only, width=9, room=40))
        for room, width, precision, length in itertools.product(ROOMS, (0, 4, 12), PRECISIONS, (5, 10)):
            out.append(argument(f'narrow_r{room}_w{width}_p{precision}_l{length}', flags=0x40,
                                length_modifier=ord('h'), room=room, width=width, precision=precision, length=length))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ('write_wide', 'argument'):
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for index, item in enumerate(make(sys.argv[2])):
        with open(os.path.join(sys.argv[1], f'{index:03d}_{item["id"]}.json'), 'w') as handle:
            json.dump(item, handle, indent=1)


if __name__ == '__main__':
    main()
