#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the lookups in game/param/fallback_lookups.cpp, for tools/verify.py
run. Each function has its own generator, <name without param_>_cases.py, which calls write()
here.

The repository is special_fixtures.Repository (every unused slot holds a decoy table). Argument
registers carry garbage in their upper halves where the argument is 32 bits. The missing
repository report is stubbed only where the function reads nothing from the repository after it.
All ids, rows and values are invented; no game data is used.
"""
import json
import os
import struct
import sys

from find_row_fixtures import FATAL_ERROR, REPOSITORY, table_bytes, u32, u64
from special_fixtures import Repository, case, table_with_rows

GAME_AREA, LOCK_CAM, ITEM_LOT_LVDEP, MENU_PROPERTY_LAYOUT = 30, 33, 51, 60
SHARED_AREA = 2900100
LOCK_RANGE, DEFAULT_RANGE = 0x10, 0x2924
GARBAGE = 0xaaaaaaaa00000000

# name: (address, kind, slot)
FUNCTIONS = {
    'param_find_row_game_area': ('0x0231b4c0', 'game_area', GAME_AREA),
    'param_find_row_item_lot_lvdep': ('0x023202a0', 'lvdep', ITEM_LOT_LVDEP),
    'param_lock_cam_range': ('0x01c92e90', 'lock_cam', LOCK_CAM),
    'param_menu_property_layout_row': ('0x020648f0', 'layout', MENU_PROPERTY_LAYOUT),
    'param_check_repository_for_id': ('0x0231ad20', 'check', None),
}


def arg32(v):
    return GARBAGE | (v & 0xffffffff)


def without_repository(out, stubbed):
    out['memory'].append({'addr': REPOSITORY, 'bytes': u64(0).hex()})
    out['stubs'] = [{'address': FATAL_ERROR, 'ret': 0, 'argc': 4}] if stubbed else []
    return out


def game_area_cases(address, slot):
    ids = [5, 100, SHARED_AREA - 1, SHARED_AREA, SHARED_AREA + 1, SHARED_AREA + 699, SHARED_AREA + 700, 0x7fffffff]
    table = table_bytes([(i, k) for k, i in enumerate(ids)])
    without_base = table_bytes([(i, k) for k, i in enumerate(ids) if i != SHARED_AREA])

    def c(cid, id, t=None):
        return case(address, 'void', cid, {'rdi': 'buf:out', 'rsi': arg32(id)},
                    Repository({slot: table if t is None else t}))

    return [
        c('plain', 100), c('plain_missing', 101), c('below_range', SHARED_AREA - 1),
        c('range_first', SHARED_AREA), c('range_second', SHARED_AREA + 1), c('range_middle', SHARED_AREA + 350),
        c('range_last', SHARED_AREA + 699), c('past_range', SHARED_AREA + 700), c('past_range_missing', SHARED_AREA + 701),
        c('range_without_base_row', SHARED_AREA + 1, without_base), c('range_own_row_ignored', SHARED_AREA + 699, without_base),
        c('largest', 0x7fffffff), c('zero', 0),
        c('negative_id', -1), c('most_negative_id', -0x80000000),
        without_repository(case(address, 'void', 'negative_id_no_repository', {'rdi': 'buf:out', 'rsi': arg32(-5)},
                                Repository({})), False),
        c('row_number_negative', SHARED_AREA + 5, table_bytes([(SHARED_AREA, -1)])),
        c('wide_format', SHARED_AREA + 9, table_bytes([(i, k) for k, i in enumerate(ids)], format=4, flags=2)),
        c('format_1', 5, table_bytes([(i, k) for k, i in enumerate(ids)], format=1)),
        c('not_loaded', 100, 'missing'), c('without_holder', SHARED_AREA, 'no_holder'),
    ]


def lvdep_cases(address, slot):
    ids = [100, 101, 102, 105, 199, 200, 300, 301, 0x7fffff9c, 0x7fffffff]
    table = table_bytes([(i, k) for k, i in enumerate(ids)], ids=ids)

    def c(cid, id, level, t=None):
        return case(address, 'void', cid, {'rdi': 'buf:out', 'rsi': arg32(id), 'rdx': arg32(level)},
                    Repository({slot: table if t is None else t}))

    # Descriptor ids that break the run: a gap below the group, one above the level.
    odd = table_bytes([(100, 0), (101, 1)], offsets=[0x100, 0x200, 0x300, 0x400], ids=[100, 101, 99, 103])
    past = table_bytes([(100, 3), (101, 1)], offsets=[0x100, 0x200, 0x300], ids=[100, 101, 102])
    return [
        c('level_0', 100, 0), c('level_1', 100, 1), c('level_2', 137, 2), c('level_4', 100, 4),
        c('level_5', 150, 5), c('level_99', 199, 99), c('level_100', 100, 100), c('group_200', 250, 50),
        c('group_300_level_1', 300, 1), c('group_missing', 450, 3), c('below_100', 50, 3),
        c('level_negative', 100, -1), c('level_most_negative', 100, -0x80000000),
        c('level_largest_wraps', 100, 0x7fffffff), c('top_group', 0x7fffffff, 99),
        c('top_group_level_0', 0x7fffffff, 0),
        c('negative_id', -100, 5),
        without_repository(case(address, 'void', 'negative_id_no_repository',
                                {'rdi': 'buf:out', 'rsi': arg32(-1), 'rdx': arg32(5)}, Repository({})), False),
        c('id_below_group', 101, 5, odd), c('id_above_level', 100, 2, odd),
        c('row_past_count', 100, 5, past),
        c('row_number_negative', 100, 5, table_bytes([(100, -2)])),
        c('row_number_largest', 100, 5, table_bytes([(100, 0x7fffffff)])),
        c('wide_format', 100, 7, table_bytes([(i, k) for k, i in enumerate(ids)], format=4, flags=2, ids=ids)),
        c('format_1', 100, 3, table_bytes([(i, k) for k, i in enumerate(ids)], format=1, ids=ids)),
        c('not_loaded', 100, 3, 'missing'), c('without_holder', 100, 3, 'no_holder'),
    ]


def lock_cam_cases(address, slot):
    def row(value):
        data = bytearray(b'\x5a' * 0x40)
        data[LOCK_RANGE:LOCK_RANGE + 4] = struct.pack('<f', value)
        return bytes(data)

    index = [(0, 0), (3, 1), (7, 2), (0x80000000, 3), (0xffffffff, 4)]
    rows = [row(10.0), row(3.5), row(-7.25), row(1e30), row(0.0)]
    table = table_with_rows(index, rows)
    no_zero = table_with_rows(index[1:], rows[1:])

    def c(cid, id, t=None, default=42.5):
        r = Repository({slot: table if t is None else t})
        owner = bytearray(b'\x33' * (DEFAULT_RANGE + 8))
        owner[DEFAULT_RANGE:DEFAULT_RANGE + 4] = struct.pack('<f', default)
        r.buffers['owner'] = {'size': len(owner), 'bytes': owner.hex()}
        return case(address, 'f32', cid, {'rdi': 'buf:owner', 'rsi': arg32(id)}, r)

    return [
        c('found', 3), c('found_negative_value', 7), c('found_zero_id', 0), c('found_high_id', 0x80000000),
        c('found_all_ones', -1), c('missing_uses_row_0', 5), c('missing_uses_row_0_high', 0x7fffffff),
        c('missing_without_row_0', 5, no_zero), c('found_without_row_0', 3, no_zero),
        c('row_number_negative_uses_row_0', 3, table_with_rows([(0, 0), (3, -1)], rows[:2])),
        c('row_0_number_negative', 5, table_with_rows([(0, -1), (3, 1)], rows[:2])),
        c('empty', 3, table_bytes([])), c('default_nan_bits', 5, no_zero, default=float('inf')),
        c('wide_format', 7, table_with_rows(index, rows, format=5)),
        c('format_1', 9, table_with_rows(index, rows, format=1)),
        c('not_loaded', 3, 'missing'), c('without_holder', 3, 'no_holder'), c('without_table', 3, 'no_table'),
    ]


def layout_cases(address, slot):
    ids = [0, 2, 9, 1000, 0x80000000, 0xfffffffe]
    table = table_bytes([(i, k) for k, i in enumerate(ids)])

    def c(cid, id, t=None):
        return case(address, 'i64', cid, {'rdi': arg32(id)}, Repository({slot: table if t is None else t}))

    return [
        c('first', 0), c('middle', 9), c('last_unsigned', 0xfffffffe), c('high_bit', 0x80000000),
        c('missing', 3), c('missing_all_ones', 0xffffffff), c('large', 1000),
        c('row_number_negative', 9, table_bytes([(2, 0), (9, -4)])),
        c('empty', 2, table_bytes([])),
        c('wide_format', 1000, table_bytes([(i, k) for k, i in enumerate(ids)], format=4, flags=2)),
        c('format_1', 2, table_bytes([(i, k) for k, i in enumerate(ids)], format=1)),
        c('not_loaded', 2, 'missing'), c('without_holder', 2, 'no_holder'), c('without_table', 2, 'no_table'),
    ]


def check_cases(address):
    def c(cid, id, repository=True):
        out = case(address, 'void', cid, {'rdi': arg32(id)}, Repository({}))
        return out if repository else without_repository(out, id >= 0)

    return [
        c('present', 5), c('present_zero', 0), c('present_negative', -1),
        c('missing', 5, False), c('missing_zero', 0, False), c('missing_largest', 0x7fffffff, False),
        c('missing_negative', -1, False), c('missing_most_negative', -0x80000000, False),
    ]


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot = FUNCTIONS[name]
    if kind == 'check':
        cases = check_cases(address)
    else:
        cases = {'game_area': game_area_cases, 'lvdep': lvdep_cases, 'lock_cam': lock_cam_cases,
                 'layout': layout_cases}[kind](address, slot)
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
