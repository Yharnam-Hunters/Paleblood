#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the path table find (game/loading/path_hash_table.cpp), for tools/verify.py run.

usage: path_hash_table_cases.py OUT_DIR

The engine's path comparison (0x02db46f0) is a scripted stub, compared with both key pointers and
answering in turn, so each case fixes which entry of the chain matches (first, middle, last,
none) and an empty bucket. Keys: a stale hash (junk in the stored hash, recomputed and stored)
over texts with every letter boundary ('@', 'A', 'Z', '[', '`', 'a', 'z', '{'), backslashes,
slashes, characters above ASCII (not folded), 0xffff and a long text that wraps the 32-bit hash;
a null text (hash 0); an empty text; a hash that is not stale (used as stored, the text
ignored); a stale byte other than 1. Bucket counts 1, 3, 7, 64, 0x1000 and 0xffffffff; the
buckets around the chosen one hold a decoy entry, so a wrong bucket calls the stub with
another entry. Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESS = '0x013e68f0'
SAME_PATH = '0x02db46f0'
ENTRY_SIZE, NEXT, KEY = 0x60, 0x58, 0x8


def fold(c):
    if 0x41 <= c <= 0x5a:
        return c + 0x20
    return 0x2f if c == 0x5c else c


def path_hash(text):
    h = 0
    for c in text:
        f = fold(c)
        if f == 0:
            break
        h = (h * 0x89 + f) & 0xffffffff
    return h


def case(cid, text, count=7, matches=None, chain=3, stale=1, stored=0x5eed5eed, null_text=False):
    """matches: the index in the chain that matches (None: none). text: code units (no NUL)."""
    h = path_hash(text) if stale and not null_text else (0 if null_text else stored)
    bucket = h % count
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64', 'args': {}, 'buffers': {},
         'memory': [], 'imports': [], 'stubs': []}
    units = list(text) + [0]
    c['buffers']['text'] = {'size': 2 * len(units) + 4, 'bytes': b''.join(struct.pack('<H', u) for u in units).hex()}
    c['buffers']['key'] = {'size': 0x10, 'bytes': (b'\0' * 8 + struct.pack('<IB', stored, stale) + b'\x9a' * 3).hex()}
    if not null_text:
        c['memory'].append({'addr': 'buf:key', 'pointer': 'text'})
    table = bytearray(b'\x71' * 0x28)
    struct.pack_into('<I', table, 0x1c, count)
    c['buffers']['table'] = {'size': 0x28, 'bytes': table.hex()}
    nb = min(bucket + 2, count)
    c['buffers']['buckets'] = {'size': 8 * nb}
    c['memory'].append({'addr': 'buf:table+0x20', 'pointer': 'buckets'})
    c['buffers']['decoy'] = {'size': ENTRY_SIZE, 'bytes': '44' * ENTRY_SIZE}
    c['memory'].append({'addr': f'buf:decoy+{NEXT:#x}', 'bytes': '00' * 8})
    for b in (bucket - 1, bucket + 1):
        if 0 <= b < nb:
            c['memory'].append({'addr': f'buf:buckets+{8 * b:#x}', 'pointer': 'decoy'})
    if chain:
        for i in range(chain):
            c['buffers'][f'entry{i}'] = {'size': ENTRY_SIZE, 'bytes': f'{0x50 + i:02x}' * ENTRY_SIZE}
            c['memory'].append({'addr': f'buf:entry{i}+{NEXT:#x}',
                                **({'pointer': f'entry{i + 1}'} if i + 1 < chain else {'bytes': '00' * 8})})
        c['memory'].append({'addr': f'buf:buckets+{8 * bucket:#x}', 'pointer': 'entry0'})
        calls = chain if matches is None else matches + 1
        for i in range(calls):
            c['stubs'].append({'address': SAME_PATH, 'argc': 2, 'ret': 1 if i == matches else 0})
    c['args'] = {'rdi': 'buf:table', 'rsi': 'buf:key'}
    return c


def u(s):
    return [ord(ch) for ch in s]


def cases():
    out = []
    for name, text in (('lower', u('chr/c0000.anibnd')), ('upper', u('CHR/C0000.ANIBND')),
                       ('backslashes', u('data\\Map\\m10\\')), ('slashes', u('data/map/m10/')),
                       ('letter_edges', u('@AZ[`az{')), ('above_ascii', [0xc0, 0xc9, 0x3b1, 0x30a2]),
                       ('unit_ffff', [0xffff, 0x41, 0x5c]), ('unit_0001', [0x61, 0x1, 0x62]), ('one_unit', [0x5a]),
                       ('long_wraps', u('a' * 40 + 'B\\c' * 20))):
        for matches in (0, 1, 2, None):
            out.append(case(f'{name}_match_{matches}', text, matches=matches))
    for count in (1, 3, 64, 0x1000):
        out.append(case(f'count_{count:x}', u('param/GameParam'), count=count, matches=1))
    out.append(case('count_ffffffff', u('a'), count=0xffffffff, matches=0))
    out += [case('empty_text', [], matches=0), case('null_text', [], null_text=True, matches=1),
            case('empty_bucket', u('sound/se'), chain=0),
            case('hash_current', u('ignored'), stale=0, stored=0x0000000b, count=7, matches=0),
            case('hash_current_large', u('ignored'), stale=0, stored=0xfffffffe, count=64, matches=None),
            case('stale_byte_80', u('Menu\\Title'), stale=0x80, matches=0),
            case('single_entry_no_match', u('x'), chain=1, matches=None)]
    return out


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
