#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the FD4 handle check and ordering (game/engine/fd4_handle.cpp), for tools/verify.py run.

usage: fd4_handle_cases.py OUT_DIR valid|less|refresh

- valid: a zero handle; handles with only the generation or only the padding bytes nonzero;
  indices 0, in the middle, the last index itself and one past it; a last index of 0 and
  0xffffffff; generation equal, different and differing in the top bit.
- less: values smaller, equal and larger, and signed (-1 against 1); the first or the second
  reference invalid, the second valid only in the first's table (its own table differs); versions
  equal (no refresh) and different (both refreshed by the game's own refresh, 0x013e99b0, which
  runs as original code and copies the entries' values and the table's version, so the order is
  decided on the refreshed values); a refresh that turns the order around.
- refresh: the handle checks of valid (a zero handle, the index past the last one, a different
  generation: the reference is left as it is) and valid handles copying the table's version and
  the entry's value (negative, the extremes); a reference lying over its own entry (its version
  over the entry's value) and over its table (its value over the table's version), so both are
  read before either is stored.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'valid': '0x013e9040', 'less': '0x01398290', 'refresh': '0x013e99b0'}
ENTRY = 0xe0
ENTRIES = 6


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'i8', 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def add_table(c, name, last_index=ENTRIES - 1, generations=(1, 2, 3, 4, 5, 6), values=None, version=7,
              entries=True):
    table = bytearray(b'\x5c' * 0x68)
    struct.pack_into('<I', table, 0x10, last_index)
    struct.pack_into('<i', table, 0x60, version)
    c['buffers'][name] = {'size': len(table), 'bytes': table.hex()}
    if entries:
        data = bytearray(b'\x13' * ENTRY * ENTRIES)
        for i, g in enumerate(generations):
            data[ENTRY * i + 0x2c] = g
            struct.pack_into('<q', data, ENTRY * i + 8, (values or [100 * (i + 1)] * ENTRIES)[i])
        c['buffers'][name + '_entries'] = {'size': len(data), 'bytes': data.hex()}
        c['memory'].append({'addr': f'buf:{name}+0x18', 'pointer': f'{name}_entries'})
    else:
        c['memory'].append({'addr': f'buf:{name}+0x18', 'bytes': '00' * 8})


def reference(index, generation, padding=b'\0\0\0', version=7, value=0):
    return struct.pack('<IB', index, generation) + padding + struct.pack('<iIq', version, 0x2b2b2b2b, value)


def valid_case(cid, index, generation, padding=b'\0\0\0', **table):
    c = base('valid', cid)
    add_table(c, 'table', **table)
    c['buffers']['reference'] = {'size': 0x20, 'bytes': (b'\0' * 8 + reference(index, generation, padding)).hex()}
    c['memory'].append({'addr': 'buf:reference', 'pointer': 'table'})
    c['args'] = {'rdi': 'buf:table', 'rsi': 'buf:reference'}
    return c


def valid_cases():
    return [valid_case('zero_handle', 0, 0, generations=(0,) * 6),
            valid_case('generation_only', 0, 1), valid_case('generation_only_mismatch', 0, 2),
            valid_case('padding_only', 0, 0, b'\0\x01\0', generations=(0,) * 6),
            valid_case('padding_only_mismatch', 0, 0, b'\0\0\x80'),
            valid_case('index_middle', 2, 3), valid_case('index_middle_mismatch', 2, 4),
            valid_case('generation_top_bit', 2, 0x83), valid_case('index_last', 5, 6),
            valid_case('index_past_last', 5, 6, last_index=4),
            valid_case('last_index_0_index_0', 0, 1, last_index=0),
            valid_case('last_index_0_index_1', 1, 2, last_index=0),
            valid_case('last_index_ffffffff', 4, 5, last_index=0xffffffff),
            valid_case('index_ffffffff_past', 0xffffffff, 1, last_index=5)]


def less_case(cid, a, b, second_table=False, values=None, version=7):
    """a, b: (index, generation, version, value) for the references."""
    c = base('less', cid)
    add_table(c, 'table', values=values, version=version)
    if second_table:
        add_table(c, 'other', values=values, version=version)
    for name, (index, generation, ver, value) in (('first', a), ('second', b)):
        c['buffers'][name] = {'size': 0x30, 'bytes': (b'\x4d' * 0x10 + b'\0' * 8 + reference(index, generation, version=ver, value=value)).hex()}
    c['memory'].append({'addr': 'buf:first+0x10', 'pointer': 'table'})
    c['memory'].append({'addr': 'buf:second+0x10', 'pointer': 'other' if second_table else 'table'})
    c['args'] = {'rdi': 'buf:first', 'rsi': 'buf:second'}
    return c


def less_cases():
    return [less_case('smaller', (1, 2, 7, 10), (2, 3, 7, 20)),
            less_case('equal', (1, 2, 7, 10), (2, 3, 7, 10)),
            less_case('larger', (1, 2, 7, 30), (2, 3, 7, 20)),
            less_case('signed', (1, 2, 7, -1), (2, 3, 7, 1)),
            less_case('signed_reverse', (1, 2, 7, 1), (2, 3, 7, -1)),
            less_case('largest_values', (1, 2, 7, -0x8000000000000000), (2, 3, 7, 0x7fffffffffffffff)),
            less_case('first_invalid', (1, 9, 7, 10), (2, 3, 7, 20)),
            less_case('first_empty', (0, 0, 7, 10), (2, 3, 7, 20)),
            less_case('second_invalid', (1, 2, 7, 10), (2, 9, 7, 20)),
            less_case('second_past_last', (1, 2, 7, 10), (6, 0x13, 7, 20)),
            less_case('other_table', (1, 2, 7, 10), (2, 3, 7, 20), second_table=True),
            less_case('versions_differ_refreshed', (1, 2, 5, 10), (2, 3, 7, 20), values=[1, 300, 200, 4, 5, 6]),
            less_case('versions_differ_same_order', (1, 2, 5, 10), (2, 3, 6, 20), values=[1, 200, 300, 4, 5, 6]),
            less_case('versions_differ_second_newer', (1, 2, 7, 10), (2, 3, 8, 20), values=[1, -5, -9, 4, 5, 6]),
            less_case('versions_equal_not_refreshed', (1, 2, 3, 10), (2, 3, 3, 20), values=[1, 300, 200, 4, 5, 6]),
            less_case('same_reference_entry', (2, 3, 1, 10), (2, 3, 2, 20))]


def refresh_case(cid, index, generation, padding=b'\0\0\0', ref_version=3, ref_value=0x1111, over=None,
                 **table):
    c = base('refresh', cid)
    c['returns'] = 'void'
    add_table(c, 'table', **table)
    ref = reference(index, generation, padding, version=ref_version, value=ref_value)
    if over == 'entry':
        # The reference's handle is the entry's first 8 bytes, its version the entry's value.
        at = f'table_entries+{0xe0 * index - 8:#x}'
        c['memory'].append({'addr': f'buf:{at}', 'bytes': (b'\x77' * 8 + ref[:0x10]).hex()})
        c['args'] = {'rdi': 'buf:table', 'rsi': f'buf:{at}'}
    elif over == 'table':
        # The reference's value lies over the table's version.
        c['memory'].append({'addr': 'buf:table+0x50', 'bytes': ref[:0x10].hex()})
        c['args'] = {'rdi': 'buf:table', 'rsi': 'buf:table+0x48'}
    else:
        c['buffers']['reference'] = {'size': 0x20, 'bytes': (b'\0' * 8 + ref).hex()}
        c['memory'].append({'addr': 'buf:reference', 'pointer': 'table'})
        c['args'] = {'rdi': 'buf:table', 'rsi': 'buf:reference'}
    return c


def refresh_cases():
    values = [-1, 0x7fffffffffffffff, -0x8000000000000000, 400, 500, 600]
    return [refresh_case('zero_handle', 0, 0, generations=(0,) * 6),
            refresh_case('generation_mismatch', 2, 4), refresh_case('index_past_last', 5, 6, last_index=4),
            refresh_case('padding_only', 0, 0, b'\0\x01\0', generations=(0,) * 6),
            refresh_case('copies_middle', 3, 4, values=values, version=-5),
            refresh_case('copies_first', 0, 1, values=values, version=0x7fffffff),
            refresh_case('copies_extreme', 2, 3, values=values, version=-0x80000000),
            refresh_case('copies_last_index', 5, 6, values=values),
            refresh_case('same_version_still_copied', 4, 5, ref_version=7, ref_value=1, values=values),
            refresh_case('generation_top_bit', 1, 0x82, generations=(1, 0x82, 3, 4, 5, 6), values=values),
            refresh_case('over_its_entry', 3, 4, values=values, version=0x12345678, over='entry'),
            refresh_case('over_its_table', 2, 3, values=values, version=0x2468ace, over='table')]


def cases(variant):
    if variant == 'refresh':
        return refresh_cases()
    return valid_cases() if variant == 'valid' else less_cases()


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
