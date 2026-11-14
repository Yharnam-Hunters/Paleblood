#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the FaceGenParam / FaceParam functions (game/param/face_params.cpp).
Each function has its own generator, <name without param_>_cases.py, which calls write() here.

The tables hold rows with invented bytes (each row a different pattern), so every write a setter
makes into the table is compared. The repository is special_fixtures.Repository (every other slot
holds a decoy). The originals have no handling for a missing row (they search forever or write
through null), so the setter cases only use rows that exist. No game data is used.
"""
import json
import os
import struct
import sys

from find_row_fixtures import REPOSITORY, table_bytes
from special_fixtures import Repository, case

FACE_GEN, FACE = 23, 24
ROW_SIZE = 0x100
CURRENT = 4

# name: (address, kind, slot, field or (first field, last index))
FUNCTIONS = {
    'param_face_gen_is_current': ('0x02317790', 'current', FACE_GEN, None),
    'param_face_is_current': ('0x023193f0', 'current', FACE, None),
    'param_find_row_face_gen': ('0x02317560', 'lookup', FACE_GEN, None),
    'param_face_gen_set_geometry': ('0x02317ad0', 'indexed', FACE_GEN, (0x00, 49)),
    'param_face_gen_set_texture': ('0x02317be0', 'indexed', FACE_GEN, (0x32, 49)),
    'param_face_gen_set_part': ('0x02317cf0', 'part', FACE_GEN, None),
    'param_face_gen_set_part_color': ('0x02318280', 'part_color', FACE_GEN, None),
    'param_face_gen_set_decal': ('0x023188b0', 'decal', FACE_GEN, 0x84),
    'param_face_gen_set_hair_parts_id': ('0x023189f0', 'field', FACE_GEN, 0x68),
    'param_face_gen_set_hair_red': ('0x02318af0', 'field', FACE_GEN, 0x69),
    'param_face_gen_set_hair_green': ('0x02318bf0', 'field', FACE_GEN, 0x6a),
    'param_face_gen_set_hair_blue': ('0x02318cf0', 'field', FACE_GEN, 0x6b),
    'param_face_gen_set_left_eye_red': ('0x02318df0', 'field', FACE_GEN, 0x6d),
    'param_face_gen_set_left_eye_green': ('0x02318ef0', 'field', FACE_GEN, 0x6e),
    'param_face_gen_set_left_eye_blue': ('0x02318ff0', 'field', FACE_GEN, 0x6f),
    'param_apply_char_make_face': ('0x018e4110', 'apply', FACE_GEN, None),
    'param_face_set_decal': ('0x0231a340', 'decal', FACE, 0x24),
    'param_face_set_body_scale': ('0x0231a480', 'body_scale', FACE, None),
    'param_face_set_age': ('0x0231a5e0', 'field', FACE, 0x2d),
    'param_face_set_gender': ('0x0231a6e0', 'field', FACE, 0x2e),
    'param_face_set_caricature_geometry': ('0x0231a7e0', 'field', FACE, 0x2f),
    'param_face_set_caricature_texture': ('0x0231a8e0', 'field', FACE, 0x30),
    'param_face_set_geometry': ('0x0231a9e0', 'indexed', FACE, (0x31, 60)),
    'param_face_set_texture': ('0x0231aaf0', 'indexed', FACE, (0x6e, 35)),
    'param_face_set_asymmetry': ('0x0231ac00', 'asymmetry', FACE, None),
}

IDS = [10, 20, 30, 0x7fffff00]


def row_table(ids=IDS, format=2, version=CURRENT):
    """A table with one patterned row per id (row k: byte i is (0x11 * k + 3 * i) & 0xff)."""
    start = 0x30 if format == 2 else 0x40
    first_row = (start + 24 * len(ids) + 15) & ~15
    offsets = [first_row + ROW_SIZE * k for k in range(len(ids))]
    data, header = table_bytes([(i, k) for k, i in enumerate(ids)], format=format, flags=2 if format >= 4 else 0,
                               offsets=offsets, size=first_row + ROW_SIZE * len(ids), version=version)
    data = bytearray(data)
    for k in range(len(ids)):
        at = header + offsets[k]
        data[at:at + ROW_SIZE] = bytes((0x11 * k + 3 * i) & 0xff for i in range(ROW_SIZE))
    return bytes(data), header


def without_repository(out):
    out['memory'] = [m for m in out['memory'] if m['addr'] != REPOSITORY]
    out['memory'].append({'addr': REPOSITORY, 'bytes': '00' * 8})
    return out


def build(address, returns, cid, args, slot, table, extra_buffers=None):
    r = Repository({slot: row_table() if table is None else table})
    out = case(address, returns, cid, args, r)
    # The face lookup's result is 0x74 bytes; the others leave the output buffer alone.
    out['buffers']['out'] = {'size': 0x78, 'bytes': 'cc' * 0x78}
    out['buffers'].update(extra_buffers or {})
    return out


def u64(v):
    return v & 0xffffffffffffffff


def common(address, slot, args_for, extra_buffers=None):
    """Cases every setter shares: a negative id (also with no repository), a table that is not
    loaded or has no holder (any id)."""
    def c(cid, id, table=None):
        return build(address, 'void', cid, args_for(id), slot, table, extra_buffers)
    return [
        c('negative_id', -1),
        without_repository(c('negative_id_no_repository', -0x80000000)),
        c('not_loaded', 20, 'missing'),
        c('without_holder', 20, 'no_holder'),
    ]


def field_cases(address, slot):
    def c(cid, id, value, table=None):
        return build(address, 'void', cid, {'rdi': u64(id), 'rsi': u64(value)}, slot, table)
    return common(address, slot, lambda id: {'rdi': u64(id), 'rsi': 0x5a}) + [
        c('first_row', 10, 0x5a), c('second_row', 20, 0x00), c('last_row', 0x7fffff00, 0xff),
        c('upper_bits_ignored', 30, 0xdeadbe7f), c('wide_format', 20, 0x81, row_table(format=4)),
        c('format_3', 30, 0x42, row_table(format=3)), c('single_row', 20, 0x99, row_table(ids=[20])),
    ]


def indexed_cases(address, slot, last):
    def c(cid, id, index, value=0xa5, table=None):
        return build(address, 'void', cid, {'rdi': u64(id), 'rsi': u64(index), 'rdx': u64(value)}, slot, table)
    return common(address, slot, lambda id: {'rdi': u64(id), 'rsi': 1, 'rdx': 0xa5}) + [
        c('index_0', 10, 0), c('index_1', 20, 1), c('index_last', 30, last), c('index_past_last', 30, last + 1),
        c('index_negative', 20, -1), c('index_large', 20, 0x80000000), c('index_upper_bits', 20, 0x500000002),
        c('value_upper_bits', 0x7fffff00, 7, 0x1234), c('wide_format', 20, last, 0x3c, row_table(format=5)),
        c('past_last_not_loaded', 20, last + 1, table='missing'),
    ]


def part_cases(address, slot):
    def c(cid, id, part, value=0x6b, table=None):
        return build(address, 'void', cid, {'rdi': u64(id), 'rsi': u64(part), 'rdx': u64(value)}, slot, table)
    return common(address, slot, lambda id: {'rdi': u64(id), 'rsi': 1, 'rdx': 0x6b}) + [
        c(f'part_{p}', [10, 20, 30, 0x7fffff00][p % 4], p) for p in range(8)
    ] + [
        c('part_8', 20, 8), c('part_negative', 20, -1), c('value_upper_bits', 10, 3, 0x777),
        c('wide_format', 30, 7, 0x12, row_table(format=4)), c('format_3', 10, 0, 0x34, row_table(format=3)),
    ]


def part_color_cases(address, slot):
    def c(cid, id, part, channel, value=0xc4, table=None):
        return build(address, 'void', cid, {'rdi': u64(id), 'rsi': u64(part), 'rdx': u64(channel),
                                            'rcx': u64(value)}, slot, table)
    out = common(address, slot, lambda id: {'rdi': u64(id), 'rsi': 1, 'rdx': 0, 'rcx': 0xc4})
    out += [c(f'part_{p}_channel_{p % 3}', [10, 20, 30, 0x7fffff00][p % 4], p, p % 3) for p in range(8)]
    return out + [
        c('channel_2_part_0', 10, 0, 2), c('channel_3', 20, 1, 3), c('channel_negative', 20, 1, -1),
        # The channel is checked before the repository is read.
        without_repository(c('channel_3_no_repository', 20, 1, 3)),
        c('part_8', 20, 8, 0), c('part_negative', 20, -1, 1), c('value_upper_bits', 30, 5, 1, 0xabc),
        c('wide_format', 0x7fffff00, 6, 2, 0x99, row_table(format=4)),
    ]


def decal_cases(address, slot):
    decal = {'decal': {'size': 8, 'bytes': '9a8b7c6d' + 'cc' * 4}}

    def c(cid, id, pointer='buf:decal', table=None):
        return build(address, 'void', cid, {'rdi': u64(id), 'rsi': pointer}, slot, table, decal)
    return common(address, slot, lambda id: {'rdi': u64(id), 'rsi': 'buf:decal'}, decal) + [
        c('first_row', 10), c('last_row', 0x7fffff00), c('null_pointer', 20, 0),
        # A null pointer returns before the repository is read.
        without_repository(c('null_pointer_no_repository', 20, 0)),
        c('wide_format', 30, table=row_table(format=4)),
    ]


def body_scale_cases(address, slot):
    def c(cid, id, index, value=0x3e, table=None):
        return build(address, 'void', cid, {'rdi': u64(id), 'rsi': u64(index), 'rdx': u64(value)}, slot, table)
    return common(address, slot, lambda id: {'rdi': u64(id), 'rsi': 0, 'rdx': 0x3e}) + [
        c(f'index_{i}', [10, 20, 30, 0x7fffff00][i % 4], i) for i in range(5)
    ] + [
        c('index_5', 20, 5), c('index_negative', 20, -1), c('value_upper_bits', 10, 2, 0xf00d),
        c('wide_format', 20, 4, 0x11, row_table(format=4)),
    ]


def asymmetry_cases(address, slot):
    def c(cid, id, index, version=CURRENT, value=0xe7, table=None):
        return build(address, 'void', cid, {'rdi': u64(id), 'rsi': u64(index), 'rdx': u64(value)}, slot,
                     row_table(version=version) if table is None else table)
    return common(address, slot, lambda id: {'rdi': u64(id), 'rsi': 0, 'rdx': 0xe7}) + [
        c('index_0', 10, 0), c('index_last', 30, 25), c('index_past_last', 30, 26), c('index_negative', 20, -1),
        c('version_3', 20, 3, 3), c('version_5', 20, 3, 5), c('version_0x104', 20, 3, 0x104),
        c('version_3_past_last', 20, 26, 3), c('value_upper_bits', 0x7fffff00, 12, CURRENT, 0x1ff),
        c('wide_format', 20, 9, table=row_table(format=4)),
    ]


def current_cases(address, slot):
    def c(cid, table):
        return build(address, 'i8', cid, {}, slot, table)
    return [
        c('version_4', row_table()), c('version_3', row_table(version=3)), c('version_5', row_table(version=5)),
        c('version_0x104', row_table(version=0x104)), c('version_0', row_table(version=0)),
        c('not_loaded', 'missing'), c('without_holder', 'no_holder'),
    ]


def lookup_cases(address, slot):
    def c(cid, id, table=None):
        return build(address, 'void', cid, {'rdi': 'buf:out', 'rsi': u64(id)}, slot, table)
    gap = table_bytes([(10, 0), (20, -1)])
    return [
        c('first_row', 10), c('second_row', 20), c('last_row', 0x7fffff00), c('missing', 25), c('missing_below', 0),
        c('negative_id', -1), without_repository(c('negative_id_no_repository', -9)),
        c('not_loaded', 10, 'missing'), c('without_holder', 10, 'no_holder'), c('negative_row_number', 20, gap),
        c('wide_format', 30, row_table(format=4)), c('empty_table', 10, table_bytes([])),
    ]


def apply_cases(address, slot):
    face = bytearray((0x40 + 5 * i) & 0xff for i in range(0x108))

    def c(cid, id, state=1, version=CURRENT, table=None):
        data = bytearray(face)
        data[0x18:0x1c] = struct.pack('<i', state)
        return build(address, 'void', cid, {'rdi': 'buf:face', 'rsi': u64(id)}, slot,
                     row_table(version=version) if table is None else table,
                     {'face': {'size': len(data), 'bytes': bytes(data).hex()}})
    return [
        c('current_table', 20), c('first_row', 10), c('last_row', 0x7fffff00), c('older_table', 20, version=3),
        c('newer_table', 30, version=5), c('not_editing', 20, state=0), c('state_2', 20, state=2),
        c('missing_row', 25), c('negative_id', -1), c('not_loaded', 20, table='missing'),
        c('wide_format', 30, table=row_table(format=4)),
    ]


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot, field = FUNCTIONS[name]
    builders = {
        'current': lambda: current_cases(address, slot),
        'lookup': lambda: lookup_cases(address, slot),
        'indexed': lambda: indexed_cases(address, slot, field[1]),
        'part': lambda: part_cases(address, slot),
        'part_color': lambda: part_color_cases(address, slot),
        'decal': lambda: decal_cases(address, slot),
        'field': lambda: field_cases(address, slot),
        'apply': lambda: apply_cases(address, slot),
        'body_scale': lambda: body_scale_cases(address, slot),
        'asymmetry': lambda: asymmetry_cases(address, slot),
    }
    cases = builders[kind]()
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
