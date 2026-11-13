#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's text encoding conversions (game/engine/text_conversion.cpp), for
tools/verify.py run.

usage: text_conversion_cases.py OUT_DIR to_multibyte|size|to_wide|wide_size

The encoding is a buffer whose first byte is the kind (0 UTF-8, 1 none, 2 and 3 the Shift-JIS
tables, 4 double-byte measuring, 5 none). The executable's own conversion tables are used. wcslen
/ strlen are scripted imports, called when the count is 0; a length of SIZE_MAX makes the count
wrap back to 0, which reaches the measuring loops' terminator-only paths. The UTF-8 to wide
conversion is a stub (the original's own function, not replaced here).
"""
import json
import os
import struct
import sys

TO_MULTIBYTE, SIZE, TO_WIDE, WIDE_SIZE = '0x024e2b90', '0x024e2dc0', '0x024e2a80', '0x024e2ff0'
UTF8_TO_WIDE = '0x024e88c0'
KINDS = [0, 1, 2, 3, 4, 5, 0x80]
MAX = 0xffffffffffffffff

# Wide strings: ASCII, Latin, the UTF-8 width edges, Japanese (hiragana, kanji, halfwidth
# katakana and its edges), characters whose table row is missing or whose code is 0, the
# highest values, and embedded terminators.
WIDE = {
    'ascii': [0x41, 0x62, 0x7f, 0],
    'latin': [0x41, 0xe9, 0xff, 0],
    'utf8_edges': [0x7f, 0x80, 0x7ff, 0x800, 0xffff, 0],
    'hiragana': [0x3042, 0x3044, 0x41, 0],
    'kanji': [0x4e00, 0x65e5, 0x672c, 0],
    'halfwidth': [0xff60, 0xff61, 0xff9f, 0xffa0, 0],
    'unmapped_row': [0x41, 0x0e01, 0x42, 0],
    'surrogates': [0xd800, 0xdc00, 0],
    'mixed': [0x41, 0x3042, 0xff71, 0xe9, 0x4e00, 0x5a, 0],
    'empty': [0],
    'inner_terminator': [0x41, 0, 0x3042, 0],
    'private_use': [0xe000, 0xf8f0, 0],
}

# Byte strings: ASCII, Shift-JIS two-byte characters, halfwidth katakana, a lead byte cut off by
# the count, invalid trail bytes and embedded terminators.
BYTES = {
    'ascii': b'Ab~\x00',
    'hiragana': b'\x82\xa0\x82\xa2A\x00',
    'kanji': b'\x88\x9f\x93\xfa\x00',
    'halfwidth': b'\xa1\xb1\xdf\x00',
    'lead_last': b'A\x82',
    'bad_trail': b'\x81\x00\x41\x00',
    'high_lead': b'\xe0\x40\xfc\xfc\x00',
    'undefined_bytes': b'\x80\xa0\xfd\xff\x00',
    'empty': b'\x00',
    'inner_terminator': b'A\x00\x82\xa0\x00',
    'utf8_two': b'\xc3\xa9A\xdf\xbf\x00',
    'utf8_three': b'\xe3\x81\x82\xef\xbf\xbf\x00',
    'utf8_cut_two': b'A\xc3\x00',
    'utf8_cut_three': b'\xe3\x81\x00',
    'utf8_cut_three_early': b'\xe3\x00\x82\x00',
    'utf8_four': b'A\xf0\x9f\x98\x80\x00',
    'utf8_lone_trail': b'\x80A\x00',
    'single_lead_end': b'AB\x82',
    'single_ascii_end': b'AB',
}


def wide_hex(chars, size):
    data = b''.join(struct.pack('<H', c) for c in chars)
    return (data + b'\x5a\x5a' * size)[:size * 2].hex()


def u64(value):
    return struct.pack('<Q', value & MAX).hex()


def length_of(chars):
    return chars.index(0) if 0 in chars else len(chars)


def to_multibyte(cid, kind, name, out_size, count, length=None):
    chars = WIDE[name]
    imports = []
    if count == 0:
        imports.append({'name': 'wcslen', 'argc': 1,
                        'ret': hex(length_of(chars) if length is None else length)})
    return {'schema': 1, 'address': TO_MULTIBYTE, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:encoding', 'rsi': 'buf:out', 'rdx': hex(out_size),
                     'rcx': 'buf:source', 'r8': 'buf:count'},
            'buffers': {'encoding': {'size': 8, 'bytes': bytes([kind]).hex() + '6d' * 7},
                        'out': {'size': 48, 'bytes': 'cc' * 48},
                        'source': {'size': 32, 'bytes': wide_hex(chars, 16)},
                        'count': {'size': 8, 'bytes': u64(count)}},
            'memory': [], 'imports': imports, 'stubs': []}


def size(cid, kind, name, count, length=None):
    chars = WIDE[name]
    imports = []
    if count == 0:
        ret = length_of(chars) if length is None else length
        imports.append({'name': 'wcslen', 'argc': 1, 'ret': hex(ret)})
        if ret == MAX and kind == 2:
            imports.append({'name': 'wcslen', 'argc': 1, 'ret': hex(length_of(chars))})
    return {'schema': 1, 'address': SIZE, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:source', 'rsi': hex(count), 'rdx': 'buf:encoding'},
            'buffers': {'encoding': {'size': 8, 'bytes': bytes([kind]).hex() + '6d' * 7},
                        'source': {'size': 32, 'bytes': wide_hex(chars, 16)}},
            'memory': [], 'imports': imports, 'stubs': []}


def to_wide(cid, kind, name, out_count, count, length=None, utf8_ret=5):
    data = BYTES[name]
    imports, stubs = [], []
    if count == 0:
        ret = (data.index(0) if 0 in data else len(data)) if length is None else length
        imports.append({'name': 'strlen', 'argc': 1, 'ret': hex(ret)})
    if kind == 0:
        stubs.append({'address': UTF8_TO_WIDE, 'argc': 4, 'ret': hex(utf8_ret)})
    return {'schema': 1, 'address': TO_WIDE, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:encoding', 'rsi': 'buf:out', 'rdx': hex(out_count),
                     'rcx': 'buf:source', 'r8': 'buf:count'},
            'buffers': {'encoding': {'size': 8, 'bytes': bytes([kind]).hex() + '6d' * 7},
                        'out': {'size': 48, 'bytes': 'cc' * 48},
                        'source': {'size': 32, 'bytes': (data + b'\x5a' * 32)[:32].hex()},
                        'count': {'size': 8, 'bytes': u64(count)}},
            'memory': [], 'imports': imports, 'stubs': stubs}


def wide_size(cid, kind, name, count, length=None):
    data = BYTES[name]
    imports = []
    if count == 0:
        ret = (data.index(0) if 0 in data else len(data)) if length is None else length
        imports.append({'name': 'strlen', 'argc': 1, 'ret': hex(ret)})
    return {'schema': 1, 'address': WIDE_SIZE, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:source', 'rsi': hex(count), 'rdx': 'buf:encoding'},
            'buffers': {'encoding': {'size': 8, 'bytes': bytes([kind]).hex() + '6d' * 7},
                        'source': {'size': 32, 'bytes': (data + b'\x5a' * 32)[:32].hex()}},
            'memory': [], 'imports': imports, 'stubs': []}


def make(which):
    out = []
    if which == 'to_multibyte':
        for kind in KINDS:
            for name in WIDE:
                out.append(to_multibyte(f'k{kind}_{name}_whole', kind, name, 48, 0))
        for kind in (0, 2, 3):
            for name in ('mixed', 'hiragana', 'utf8_edges', 'kanji', 'ascii'):
                for out_size in (0, 1, 2, 3, 4, 5):
                    out.append(to_multibyte(f'k{kind}_{name}_out{out_size}', kind, name, out_size, 0))
                for count in (1, 2, 3, 20):
                    out.append(to_multibyte(f'k{kind}_{name}_count{count}', kind, name, 48, count))
        out.append(to_multibyte('k3_huge_out', 3, 'mixed', MAX, 0))
        out.append(to_multibyte('k0_huge_out', 0, 'mixed', 0x7fffffffffffffff, 0))
    elif which == 'size':
        for kind in KINDS:
            for name in WIDE:
                out.append(size(f'k{kind}_{name}_whole', kind, name, 0))
                for count in (1, 2, 3, 20):
                    out.append(size(f'k{kind}_{name}_count{count}', kind, name, count))
                out.append(size(f'k{kind}_{name}_wrapped', kind, name, 0, length=MAX))
    elif which == 'wide_size':
        for kind in KINDS:
            for name in BYTES:
                out.append(wide_size(f'k{kind}_{name}_whole', kind, name, 0))
                for count in (1, 2, 3, 4, 5, 20):
                    out.append(wide_size(f'k{kind}_{name}_count{count}', kind, name, count))
                out.append(wide_size(f'k{kind}_{name}_wrapped', kind, name, 0, length=MAX))
    else:
        for kind in KINDS:
            for name in BYTES:
                out.append(to_wide(f'k{kind}_{name}_whole', kind, name, 24, 0))
        for kind in (2, 3):
            for name in BYTES:
                for out_count in (0, 1, 2, 3):
                    out.append(to_wide(f'k{kind}_{name}_out{out_count}', kind, name, out_count, 0))
                for count in (1, 2, 3, 20):
                    out.append(to_wide(f'k{kind}_{name}_count{count}', kind, name, 24, count))
            out.append(to_wide(f'k{kind}_negative_count', kind, 'ascii', 24, 1 << 63))
            out.append(to_wide(f'k{kind}_huge_out', kind, 'hiragana', MAX, 0))
        out.append(to_wide('k0_utf8_negative', 0, 'ascii', 24, 0, utf8_ret=0xffffffff80000000))
        out.append(to_wide('k0_utf8_given_count', 0, 'kanji', 3, 4, utf8_ret=0x123456789))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ('to_multibyte', 'size', 'to_wide', 'wide_size'):
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for index, item in enumerate(make(sys.argv[2])):
        with open(os.path.join(sys.argv[1], f'{index:03d}_{item["id"]}.json'), 'w') as handle:
            json.dump(item, handle, indent=1)


if __name__ == '__main__':
    main()
