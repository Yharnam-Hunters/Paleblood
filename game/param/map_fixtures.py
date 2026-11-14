#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the map table lookups (game/param/map_lookups.cpp): GameMapParam by
map number and the ReturnPointParam scans. Each function has its own generator,
<name without param_>_cases.py, which calls write() here.

The repository is special_fixtures.Repository (every unused slot holds a decoy table). Map
numbers, entity ids, warp chair numbers and rows are invented; no game data is used. The
return point scans call the engine's id array push_back, which the cases stub: each call is
compared with the pointer it gets and the id it points at.
"""
import json
import os
import struct
import sys

from find_row_fixtures import REPOSITORY, table_bytes
from special_fixtures import ROW_SIZE, Repository, case, table_with_rows

GAME_MAP, RETURN_POINT = 49, 50
PUSH_BACK = '0x02bc1bb0'

# name: (address, kind)
FUNCTIONS = {
    'param_find_row_game_map': ('0x0231be50', 'game_map'),
    'param_return_point_warp_chair': ('0x02326c60', 'warp_chair'),
    'param_return_point_entity': ('0x02326d60', 'entity'),
    'param_return_points_of_map': ('0x02326e60', 'of_map'),
}


def map_number(area, block, cc, dd):
    return area * 1000000 + block * 10000 + cc * 100 + dd


def game_map_cases(address):
    ids = [0, map_number(4, 3, 2, 1), map_number(21, 0, 0, 0), map_number(21, 1, 0, 0),
           map_number(255, 255, 255, 254)]
    table = table_bytes([(i, k) for k, i in enumerate(ids)])
    no_default = table_bytes([(i, k) for k, i in enumerate(ids[1:])])
    default_without_row = table_bytes([(0, -1), (map_number(21, 0, 0, 0), 0)])
    map_without_row = table_bytes([(0, 1), (map_number(21, 0, 0, 0), -1)])

    def c(cid, packed, tables=None):
        packed_bytes = bytes(packed)
        r = Repository({GAME_MAP: table} if tables is None else tables)
        r.buffers['map'] = {'size': 8, 'bytes': (packed_bytes + b'\xcc' * 4).hex()}
        return case(address, 'void', cid, {'rdi': 'buf:out', 'rsi': 'buf:map'}, r)

    return [
        c('own_row', [1, 2, 3, 4]),  # m04_03_02_01: bytes from the lowest part up
        c('own_row_area_21', [0, 0, 0, 21]),
        c('own_row_block_1', [0, 0, 1, 21]),
        c('largest_parts', [254, 255, 255, 255]),
        c('default_row', [0, 0, 2, 21]),
        c('map_zero', [0, 0, 0, 0]),
        c('no_map', [255, 255, 255, 255]),
        c('no_map_no_table', [255, 255, 255, 255], {GAME_MAP: 'missing'}),
        c('almost_no_map', [255, 255, 255, 127]),
        c('no_default_row', [9, 9, 9, 9], {GAME_MAP: no_default}),
        c('no_default_own_row', [1, 2, 3, 4], {GAME_MAP: no_default}),
        c('default_without_row', [5, 0, 0, 30], {GAME_MAP: default_without_row}),
        c('own_entry_without_row', [0, 0, 0, 21], {GAME_MAP: map_without_row}),
        c('not_loaded', [1, 2, 3, 4], {GAME_MAP: 'missing'}),
        c('without_holder', [1, 2, 3, 4], {GAME_MAP: 'no_holder'}),
        c('empty_table', [1, 2, 3, 4], {GAME_MAP: table_bytes([])}),
        c('wide_format', [0, 0, 1, 21], {GAME_MAP: table_bytes([(i, k) for k, i in enumerate(ids)], format=4, flags=2)}),
        c('wide_default', [7, 0, 1, 21], {GAME_MAP: table_bytes([(i, k) for k, i in enumerate(ids)], format=5, flags=2)}),
    ]


def return_point(area, block, entity, chair, registers=1):
    row = bytearray(b'\x5a' * ROW_SIZE)
    row[0:0xe] = struct.pack('<hhiiBb', area, block, entity, 0x6000 + entity % 100, registers, chair)
    return bytes(row)


# Rows: (area, block, entity id, warp chair number).
ROWS = [(21, 0, 1000, 3), (21, 0, 1001, -1), (21, 1, 1002, 0), (22, 0, 1000, 5), (21, 0, 1004, -2),
        (-1, -1, 0, 127), (21, 0, 1006, -128), (23, 7, 1007, 9), (21, 0, 1008, 3), (25, 0, -1, 4)]


def point_table(rows=ROWS, count=None, format=2):
    data = [return_point(*r) for r in rows]
    n = len(rows) if count is None else count
    return table_with_rows([(k, k) for k in range(n)], data, format=format)


def scan_cases(address, kind):
    def c(cid, key, tables=None, block=None, pushes=None):
        r = Repository({RETURN_POINT: point_table()} if tables is None else tables)
        if kind == 'of_map':
            r.buffers['ids'] = {'size': 0x28, 'bytes': 'cc' * 0x28}
            stubs = [{'address': PUSH_BACK, 'ret': 0, 'argc': 2, 'argmem': [{'arg': 1, 'size': 4}]}
                     for _ in range(pushes)]
            out = case(address, 'void', cid, {'rdi': 'buf:ids', 'rsi': key & 0xffffffffffffffff,
                                              'rdx': block & 0xffffffffffffffff}, r)
            out['stubs'] = stubs
            return out
        return case(address, 'i32', cid, {'rdi': key & 0xffffffffffffffff}, r)

    def shrink_after_first(cid, area, block, rows, first_row, shrink_to, pushes):
        """The push_back stub lowers the table's row count on its first call (by writing through
        the pointer it gets, which points into the first matching row): later rows past the new
        count are skipped, though the scan runs to the count it started with."""
        table, header = point_table(rows)
        first_row_offset = (0x30 + 12 * len(rows) + 15) & ~15
        row_at = first_row_offset + ROW_SIZE * first_row
        out = c(cid, area, {RETURN_POINT: (table, header)}, block, pushes)
        out['stubs'][0]['writes'] = [{'arg': 1, 'offset': 0xa - (row_at + 4), 'bytes': struct.pack('<H', shrink_to).hex()}]
        return out

    def without_repository(out):
        # A negative key returns before the repository is read, so a missing one is fine.
        out['memory'] = [m for m in out['memory'] if m['addr'] != REPOSITORY]
        out['memory'].append({'addr': REPOSITORY, 'bytes': '00' * 8})
        return out

    fewer = point_table(count=3)
    wide = point_table(format=4)
    common = [
        c('not_loaded', 1000 if kind != 'entity' else 3, {RETURN_POINT: 'missing'}, 21, 0),
        c('without_holder', 1000 if kind != 'entity' else 3, {RETURN_POINT: 'no_holder'}, 21, 0),
        c('empty_table', 1000 if kind != 'entity' else 3, {RETURN_POINT: point_table([])}, 21, 0),
    ]
    if kind == 'warp_chair':
        return common + [
            c('first_row', 1000), c('later_row', 1002), c('chair_none', 1001), c('chair_negative', 1004),
            c('chair_largest', 0), c('chair_smallest', 1006), c('last_row', 1008), c('missing', 1003),
            c('missing_large', 0x7fffffff), c('negative_id', -1), c('most_negative_id', -0x80000000),
            without_repository(c('negative_id_no_repository', -5)),
            c('row_past_count', 1007, {RETURN_POINT: fewer}), c('within_count', 1002, {RETURN_POINT: fewer}),
            c('wide_format', 1007, {RETURN_POINT: wide}),
            c('upper_bits_ignored', 0x1234567800000000 | 1002),
        ]
    if kind == 'entity':
        return common + [
            without_repository(c('negative_no_repository', -3)),
            c('first_match', 3), c('chair_zero', 0), c('chair_five', 5), c('chair_largest', 127), c('chair_nine', 9),
            c('missing', 4), c('above_byte_range', 0x17f), c('negative', -1), c('negative_byte', -128),
            c('byte_128', 0x80), c('byte_255', 0xff),
            c('most_negative', -0x80000000), c('row_past_count', 9, {RETURN_POINT: fewer}),
            c('within_count', 0, {RETURN_POINT: fewer}), c('wide_format', 9, {RETURN_POINT: wide}),
            c('upper_bits_ignored', 0x7777777700000000 | 5),
        ]
    # of_map: area 21 block 0 has entities 1000, 1004, 1006, 1008 with chairs (1001's is -1).
    return common + [
        c('area_21_block_0', 21, None, 0, 4), c('area_21_block_1', 21, None, 1, 1), c('area_22', 22, None, 0, 1),
        c('area_minus_1', -1, None, -1, 1), c('area_wide_minus_1', 0xffff, None, 0xffff, 0),
        c('area_wide_block_minus_1', 0xffff, None, -1, 0), c('area_minus_1_block_wide', -1, None, 0xffff, 0),
        c('no_points', 24, None, 0, 0), c('block_mismatch', 23, None, 6, 0),
        c('upper_bits_ignored', 0x1500000015, None, 0xabcd00000000, 4),
        c('row_past_count', 21, {RETURN_POINT: fewer}, 0, 1), c('wide_format', 21, {RETURN_POINT: wide}, 0, 4),
        shrink_after_first('count_shrinks', 21, 0, ROWS, 0, 2, 1),
        shrink_after_first('count_drops_to_zero', 21, 0, ROWS, 0, 0, 1),
    ]


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind = FUNCTIONS[name]
    cases = game_map_cases(address) if kind == 'game_map' else scan_cases(address, kind)
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
