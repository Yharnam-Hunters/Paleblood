#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the table walks of the solo parameter repository
(game/param/row_iteration.cpp): row counts, next and previous row ids, and the id lists. Each
function has its own generator, <name without param_>_cases.py, which calls write() here.

The repository is special_fixtures.Repository (every unused slot holds a decoy table). Index
ids and descriptor ids differ (descriptor ids are 0x7000 + row number unless a case sets them),
so a result shows which one was read. The id lists call the engine's id array push_back, which
the cases stub: each call is compared with the id it is given, and a stub's write stands in for
the array growing (or, in one case, for the table's count changing under the loop). All ids and
rows are invented; no game data is used.
"""
import json
import os
import struct
import sys

from find_row_fixtures import FATAL_ERROR, REPOSITORY, table_bytes, u32, u64
from special_fixtures import Repository, case, table_with_rows

EQUIP_WEAPON, EQUIP_PROTECTOR, EQUIP_ACCESSORY, EQUIP_GOODS = 0, 1, 2, 3
MAGIC, CHARA_INIT, HOLYGRAIL_EX = 14, 21, 52
PUSH_BACK, PUSH_BACK_COPY = '0x02bc1bb0', '0x016da6a0'
PUSH_BACK_PROTECTOR, PUSH_BACK_WEAPON = '0x019f8d70', '0x01b0d5f0'
STEP_BYTE = 0xd2   # the protector byte the matching steps compare
# Where the variant lists read their reinforcement entries, and the weapon fields they filter on.
ORIGINS = {EQUIP_PROTECTOR: 0x40, EQUIP_WEAPON: 0x60}
WEAPON_BYTE, WEAPON_WORD = 0xe2, 0x4
LIST_ORDER = 0x26  # the accessory field the filtered list reads (int16)
COUNT_AT = 0xa     # the header's row count
GARBAGE = 0xaaaaaaaa00000000  # upper half of a 32-bit argument register, to be ignored
ARRAY_BEGIN = 0x0000123400000000  # the id array's storage: never dereferenced by the walk

# name: (address, kind, slot, push_back)
FUNCTIONS = {
    'param_row_count_chara_init': ('0x02315060', 'count', CHARA_INIT, None),
    'param_row_count_equip_param_goods': ('0x0231e610', 'count', EQUIP_GOODS, None),
    'param_row_count_magic': ('0x02321320', 'count', MAGIC, None),
    'param_row_count_holygrail_ex': ('0x0231ff00', 'count', HOLYGRAIL_EX, None),
    'param_next_row_id_equip_param_accessory': ('0x02312070', 'next', EQUIP_ACCESSORY, None),
    'param_next_row_id_equip_param_goods': ('0x0231ebb0', 'next', EQUIP_GOODS, None),
    'param_next_row_id_magic': ('0x02321680', 'next', MAGIC, None),
    'param_next_row_id_equip_param_protector': ('0x02324fa0', 'next', EQUIP_PROTECTOR, None),
    'param_previous_row_id_equip_param_accessory': ('0x023121b0', 'previous', EQUIP_ACCESSORY, None),
    'param_previous_row_id_equip_param_goods': ('0x0231ecf0', 'previous', EQUIP_GOODS, None),
    'param_previous_row_id_magic': ('0x023217c0', 'previous', MAGIC, None),
    'param_previous_row_id_equip_param_protector': ('0x023250e0', 'previous', EQUIP_PROTECTOR, None),
    'param_list_row_ids_equip_param_accessory': ('0x02312520', 'list', EQUIP_ACCESSORY, PUSH_BACK),
    'param_list_row_ids_equip_param_goods': ('0x0231f0e0', 'list', EQUIP_GOODS, PUSH_BACK),
    'param_list_row_ids_equip_param_protector': ('0x02325820', 'list', EQUIP_PROTECTOR, PUSH_BACK_COPY),
    'param_list_listed_ids_equip_param_accessory': ('0x02312370', 'listed', EQUIP_ACCESSORY, PUSH_BACK_COPY),
    'param_next_matching_id_equip_param_protector': ('0x02325240', 'next_matching', EQUIP_PROTECTOR, None),
    'param_previous_matching_id_equip_param_protector': ('0x023253c0', 'previous_matching', EQUIP_PROTECTOR, None),
    'param_list_versions_equip_param_protector': ('0x02325620', 'versions', EQUIP_PROTECTOR, PUSH_BACK_PROTECTOR),
    'param_list_versions_equip_param_weapon': ('0x0232c110', 'versions', EQUIP_WEAPON, PUSH_BACK_WEAPON),
    'param_list_row_ids_equip_param_weapon': ('0x0232bf50', 'weapon_list', EQUIP_WEAPON, PUSH_BACK_COPY),
    'param_clear_id_list_equip_param_protector': ('0x023255c0', 'clear', EQUIP_PROTECTOR, None),
    'param_clear_id_list_equip_param_weapon': ('0x0232bc80', 'clear', EQUIP_WEAPON, None),
}


def with_count(table, count):
    """The table with its header's row count replaced (the descriptors are left as they are)."""
    data, header = table
    data = bytearray(data)
    data[header + COUNT_AT:header + COUNT_AT + 2] = struct.pack('<H', count)
    return bytes(data), header


def count_cases(address, slot):
    def c(cid, table):
        return case(address, 'i32', cid, {'rdi': GARBAGE}, Repository({slot: table}))

    five = table_bytes([(1, 0), (3, 1), (5, 2), (7, 3), (9, 4)])
    return [
        c('five_rows', five),
        c('one_row', table_bytes([(5, 0)])),
        c('empty', table_bytes([])),
        c('large', table_bytes([(10 * k + 1, k) for k in range(300)])),
        c('count_high_bit', with_count(five, 0x8001)),
        c('count_largest', with_count(five, 0xffff)),
        c('wide_format', table_bytes([(1, 0), (3, 1)], format=5, flags=2)),
        c('not_loaded', 'missing'),
        c('without_holder', 'no_holder'),
    ]


def step_cases(address, slot):
    five = [(1, 0), (3, 1), (5, 2), (7, 3), (9, 4)]
    ids = [0x11, 0x80000022, 0xffffffff, 0x44, 0x7fffffff]

    def c(cid, id, table=None, tables=None):
        if tables is None:
            tables = {slot: table if table is not None else table_bytes(five)}
        return case(address, 'i32', cid, {'rdi': GARBAGE | (id & 0xffffffff)}, Repository(tables))

    # The index may name rows out of order, or past the counted rows.
    shuffled = table_bytes([(1, 4), (3, 0), (7, 2), (8, 1), (9, 3)])
    past = table_bytes([(3, 0), (5, 2), (6, 1)], offsets=[0x100, 0x200, 0x300, 0x400])
    beyond = table_bytes([(3, 3), (5, 2), (6, 1)])
    huge = table_bytes([(2, 0), (4, 0x7fffffff), (6, -0x80000000), (8, -1)])
    big = table_bytes([(10 * k + 1, k) for k in range(300)])
    return [
        c('first', 1),
        c('middle', 5),
        c('last', 9),
        c('second', 3),
        c('second_last', 7),
        c('missing_between', 4),
        c('missing_below', 0),
        c('missing_above', 11),
        c('negative_id', -1),
        c('most_negative_id', -0x80000000),
        c('negative_id_empty', -1, table_bytes([])),
        c('empty', 5, table_bytes([])),
        c('single_row', 5, table_bytes([(5, 0)])),
        c('single_row_negative_id', -3, table_bytes([(5, 0)])),
        c('descriptor_ids_high_bit', 3, table_bytes(five, ids=ids)),
        c('descriptor_ids_high_bit_2', 5, table_bytes(five, ids=ids)),
        c('descriptor_ids_negative_first', -1, table_bytes(five, ids=ids)),
        c('shuffled_rows_1', 1, shuffled),
        c('shuffled_rows_3', 3, shuffled),
        c('shuffled_rows_7', 7, shuffled),
        c('shuffled_rows_8', 8, shuffled),
        c('row_past_count', 5, past),
        c('row_at_count_minus_one', 6, past),
        c('row_beyond_count', 3, beyond),
        c('row_number_negative', 8, huge),
        c('row_number_most_negative', 6, huge),
        c('row_number_largest', 4, huge),
        c('row_number_zero', 2, huge),
        c('unsigned_ids', 0x7ffffff0, table_bytes([(1, 0), (0x7ffffff0, 1), (0x80000001, 2), (0xfffffff0, 3)])),
        c('large_table_first', 1, big),
        c('large_table_middle', 1491, big),
        c('large_table_last', 2991, big),
        c('large_table_missing', 1500, big),
        c('large_table_negative', -7, big),
        c('format_0', 3, table_bytes(five, format=0)),
        c('format_1', 5, table_bytes(five, format=1)),
        c('format_3_flag_ignored', 5, table_bytes(five, format=3, flags=2)),
        c('format_4_narrow', 7, table_bytes(five, format=4, flags=0xfd)),
        c('format_4_wide', 7, table_bytes(five, format=4, flags=2, ids=ids)),
        c('format_4_wide_negative_id', -1, table_bytes(five, format=4, flags=2, ids=ids)),
        c('format_255_wide', 3, table_bytes(five, format=0xff, flags=0xff, ids=ids)),
        c('format_1_negative_id', -1, table_bytes(five, format=1, ids=ids)),
        c('not_loaded', 5, tables={slot: 'missing'}),
        c('not_loaded_negative_id', -1, tables={slot: 'missing'}),
        c('without_holder', 5, tables={slot: 'no_holder'}),
    ]


def list_cases(address, slot, push_back, listed):
    orders = [0, -1, 5, -0x8000, 0x7fff, 1]

    def rows(n):
        out = []
        for k in range(n):
            data = bytearray(b'\x5a' * 0x40)
            data[LIST_ORDER:LIST_ORDER + 2] = struct.pack('<h', orders[k % len(orders)])
            out.append(bytes(data))
        return out

    def included(n):
        return [k for k in range(n) if not listed or orders[k % len(orders)] >= 0]

    def c(cid, table, rows_in_table, final_end=None, shrink=None, end_before=None):
        array = (u64(0x1111) + u64(ARRAY_BEGIN) + u64(ARRAY_BEGIN + 0x40 if end_before is None else end_before)
                 + u64(ARRAY_BEGIN + 0x400) + u64(0x2222))
        array_at = None
        if shrink is not None:
            # The array sits after the table in the same buffer, so a push_back stub can reach the
            # table's count through it.
            data, header = table
            array_at = (len(data) + 15) & ~15
            table = (data.ljust(array_at, b'\0') + array, header)
        r = Repository({slot: table})
        if array_at is None:
            r.buffers['ids'] = {'size': len(array), 'bytes': array.hex()}
            ids = 'buf:ids'
        else:
            ids = f'buf:s{slot}_table+{array_at}'
        pushed = included(rows_in_table) if shrink is None else [k for k in included(rows_in_table) if k < shrink[1]]
        stubs = [{'address': push_back, 'ret': 0, 'argc': 2, 'argi': [0],
                  'argmem': [{'arg': 1, 'size': 4}]} for _ in pushed]
        # The array grows by one id per call; the last call leaves its final end.
        for n, stub in enumerate(stubs):
            end = ARRAY_BEGIN + 4 * (n + 1) if final_end is None or n + 1 < len(stubs) else final_end
            stub['writes'] = [{'arg': 0, 'offset': 0x10, 'bytes': u64(end).hex()}]
        if shrink is not None:
            at, count = shrink
            stubs[at]['writes'].append({'arg': 0, 'offset': table[1] + COUNT_AT - array_at,
                                        'bytes': struct.pack('<H', count).hex()})
        out = case(address, 'i64', cid, {'rdi': ids}, r)
        out['stubs'] = stubs
        return out

    index5 = [(1, 0), (3, 1), (5, 2), (7, 3), (9, 4)]
    index6 = index5 + [(11, 5)]
    big = [(10 * k + 1, k) for k in range(40)]
    return [
        c('six_rows', table_with_rows(index6, rows(6)), 6),
        c('one_row', table_with_rows([(5, 0)], rows(1)), 1),
        c('second_row_alone', table_with_rows([(5, 0), (6, 1)], rows(2)), 2),
        c('empty', table_bytes([]), 0),
        c('forty_rows', table_with_rows(big, rows(40)), 40),
        c('wide_format', table_with_rows(index6, rows(6), format=5), 6),
        c('descriptor_ids_high_bit', with_ids(table_with_rows(index5, rows(5)), [0x80000000, 0xffffffff, 0, 7, 0x7fffffff]), 5),
        # The push_back may change the table's count: the loop stops early, not late.
        c('count_shrinks', table_with_rows(index6, rows(6)), 6, shrink=(1, 3)),
        c('count_shrinks_to_one', table_with_rows(index6, rows(6)), 6, shrink=(0, 1)),
        # The count is the end minus the start, in bytes, shifted as unsigned.
        c('end_before_start', table_with_rows(index6, rows(6)), 6, final_end=ARRAY_BEGIN - 8),
        c('end_not_aligned', table_with_rows(index6, rows(6)), 6, final_end=ARRAY_BEGIN + 0x13),
        c('not_loaded', 'missing', 0),
        c('not_loaded_end_before_start', 'missing', 0, end_before=ARRAY_BEGIN - 0x40),
        c('without_holder', 'no_holder', 0),
    ]


def with_ids(table, ids):
    """The table with its first descriptors' ids replaced (narrow descriptors, format 2)."""
    data, header = table
    data = bytearray(data)
    for k, i in enumerate(ids):
        at = header + 0x30 + 12 * k
        data[at:at + 4] = u32(i)
    return bytes(data), header



def matching_step_cases(address, slot, forward):
    # Each row's step byte; index ids 1, 11, 21, ... name rows 0, 1, 2, ... (descriptor ids differ).
    values = [3, 5, 3, 0, 255, 3, 5, 7]

    def rows(vals):
        out = []
        for v in vals:
            data = bytearray(b'\x5a' * 0x100)
            data[STEP_BYTE] = v
            out.append(bytes(data))
        return out

    index = [(10 * k + 1, k) for k in range(len(values))]
    table = table_with_rows(index, rows(values))

    def c(cid, id, value, t=None, tables=None):
        if tables is None:
            tables = {slot: table if t is None else t}
        return case(address, 'i32', cid, {'rdi': GARBAGE | (id & 0xffffffff), 'rsi': GARBAGE | (value & 0xffffffff)},
                    Repository(tables))

    # Ten rows of data but eight counted ones: the index can name rows 8 and 9, past the count.
    past = table_with_rows(index[:7] + [(75, 9)], rows(values + [3, 9]))
    out = [
        c('from_first', 1, 3), c('from_third', 21, 3), c('from_sixth', 51, 3),
        c('from_second_value_5', 11, 5), c('from_last', 71, 3), c('from_last_value_7', 71, 7),
        c('value_7', 1, 7), c('value_0', 1, 0), c('value_255', 1, 255),
        c('value_above_byte', 1, 0x103), c('value_negative', 1, -1), c('value_most_negative', 1, -0x80000000),
        c('missing_id', 4, 3), c('missing_id_value_5', 40, 5), c('missing_above', 99, 7),
        c('negative_id', -1, 3), c('negative_id_value_7', -5, 7), c('most_negative_id_value_5', -0x80000000, 5),
        c('row_number_minus_one', 5, 3, table_with_rows([(5, -1)] + index[1:], rows(values))),
        c('row_number_minus_three', 5, 3, table_with_rows([(5, -3)] + index[1:], rows(values))),
        c('row_past_count', 75, 3, past), c('row_past_count_value_7', 75, 7, past),
        c('shuffled_rows', 31, 3, table_with_rows([(1, 6), (11, 0), (21, 3), (31, 2), (41, 7), (51, 1), (61, 5), (71, 4)], rows(values))),
        c('empty', 5, 3, table_bytes([])),
        c('empty_negative_id', -1, 3, table_bytes([])),
        c('single_row', -1, 3, table_with_rows([(5, 0)], rows([3]))),
        c('wide_format', 21, 3, table_with_rows(index, rows(values), format=5)),
        c('wide_format_back', 61, 5, table_with_rows(index, rows(values), format=4)),
        c('format_1', 11, 3, table_with_rows(index, rows(values), format=1)),
        c('large_table', 1491, 3, table_with_rows([(10 * k + 1, k) for k in range(300)], rows([k % 7 for k in range(300)]))),
        c('not_loaded', 1, 3, tables={slot: 'missing'}),
        c('not_loaded_negative_id', -1, 3, tables={slot: 'missing'}),
        c('without_holder', 1, 3, tables={slot: 'no_holder'}),
    ]
    if forward:
        # The next row after row 2^31 - 1 is past any count (stepping back from it would read
        # descriptors far outside the table).
        out.append(c('row_number_largest', 5, 3, table_with_rows([(5, 0x7fffffff)] + index[1:], rows(values))))
    return out


def array_bytes(begin, end):
    return u64(0x1111) + u64(begin) + u64(end) + u64(ARRAY_BEGIN + 0x400) + u64(0x2222)


def push_stubs(push_back, ids, final_end=None):
    """One stubbed push_back per id, compared with the id; each moves the array's end on."""
    stubs = []
    for n, _ in enumerate(ids):
        end = ARRAY_BEGIN + 4 * (n + 1) if final_end is None or n + 1 < len(ids) else final_end
        stubs.append({'address': push_back, 'ret': 0, 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 4}],
                      'writes': [{'arg': 0, 'offset': 0x10, 'bytes': u64(end).hex()}]})
    return stubs


def versions_cases(address, slot, push_back):
    weapon = slot == EQUIP_WEAPON
    origins_at = ORIGINS[slot]
    # Each row: its reinforcement entries (padded with -1 to 16) and, for weapons, its list byte.
    specs = [
        ([0, 1, 2], 4),
        ([], 4),
        (list(range(16)), 4),
        ([5] * 14, 9),
        ([-0x80000000, 3, 4], 4),
        ([0x7fffffff] * 15 + [-1], 4),
        ([7, -1, 7, 7], 0),
        ([0] * 15 + [-5], 4),
    ]

    def row(origins, byte):
        data = bytearray(b'\x5a' * 0x100)
        entries = (origins + [-1] * 16)[:16]
        data[origins_at:origins_at + 64] = struct.pack('<16i', *entries)
        if weapon:
            data[WEAPON_BYTE] = byte
        return bytes(data)

    def versions(origins):
        n = 0
        while n < 15 and n < len(origins) and origins[n] >= 0:
            n += 1
        return n

    def expected(specs_in_table, ids, value, count=None):
        out = []
        for k, (origins, byte) in enumerate(specs_in_table[:count]):
            if weapon and byte != value:
                continue
            out += [(ids[k] + j) & 0xffffffff for j in range(versions(origins) + 1)]
        return out

    def c(cid, rows_spec, value=4, ids=None, format=2, final_end=None, shrink=None):
        index = [(10 * k + 1, k) for k in range(len(rows_spec))]
        table = table_with_rows(index, [row(o, b) for o, b in rows_spec], format=format)
        if ids is not None:
            table = with_descriptor_ids(table, ids, format)
        desc_ids = ids if ids is not None else [0x7000 + k for k in range(len(rows_spec))]
        array_at = None
        if shrink is not None:
            data, header = table
            array_at = (len(data) + 15) & ~15
            table = (data.ljust(array_at, b'\0') + array_bytes(ARRAY_BEGIN, ARRAY_BEGIN + 0x40), header)
        r = Repository({slot: table})
        if array_at is None:
            r.buffers['ids'] = {'size': 0x28, 'bytes': array_bytes(ARRAY_BEGIN, ARRAY_BEGIN + 0x40).hex()}
            where = 'buf:ids'
        else:
            where = f'buf:s{slot}_table+{array_at}'
        if shrink is None:
            pushed = expected(rows_spec, desc_ids, value)
        else:
            # The count drops to shrink[1] during push number shrink[0]; the protector list then
            # adds -1 for each row left, the weapon list skips them.
            at, count = shrink
            pushed = expected(rows_spec, desc_ids, value)[:at + 1]
            done_rows, total = 0, 0
            for k, (o, b) in enumerate(rows_spec):
                if weapon and b != value:
                    done_rows = k + 1
                    continue
                total += versions(o) + 1
                done_rows = k + 1
                if total > at:
                    break
            pushed = expected(rows_spec[:done_rows], desc_ids, value)
            for k in range(done_rows, len(rows_spec)):
                if k >= count and not weapon:
                    pushed.append(0xffffffff)
                elif k < count:
                    pushed += expected([rows_spec[k]], [desc_ids[k]], value)
        stubs = push_stubs(push_back, pushed, final_end)
        if shrink is not None:
            stubs[shrink[0]]['writes'].append({'arg': 0, 'offset': table[1] + COUNT_AT - array_at,
                                               'bytes': struct.pack('<H', shrink[1]).hex()})
        args = {'rdi': where}
        if weapon:
            args['rsi'] = GARBAGE | (value & 0xffffffff)
        out = case(address, 'i64', cid, args, r)
        out['stubs'] = stubs
        return out

    wrap_ids = [0x7ffffffe, 0xfffffffe, 5, 6, 0x80000000, 9, 10, 11]
    out = [
        c('eight_rows', specs),
        c('one_row', specs[:1]),
        c('no_versions', [([], 4)]),
        c('all_sixteen', [(list(range(16)), 4)]),
        c('ids_wrap', specs, ids=wrap_ids),
        c('wide_format', specs, format=5),
        c('format_1', specs, format=1),
        c('end_before_start', specs[:2], final_end=ARRAY_BEGIN - 8),
        c('count_shrinks', specs, shrink=(4, 3)),
        c('empty', []),
    ]
    if weapon:
        out += [c('value_9', specs, value=9), c('value_0', specs, value=0), c('value_none', specs, value=1),
                c('value_above_byte', specs, value=0x104), c('value_negative', specs, value=-1)]
        high = [(o, 0xff) for o, _ in specs[:3]]
        out += [c('byte_ff_value_negative', high, value=-1), c('byte_ff_value_255', high, value=255)]
    for cid, state in (('not_loaded', 'missing'), ('without_holder', 'no_holder')):
        r = Repository({slot: state})
        r.buffers['ids'] = {'size': 0x28, 'bytes': array_bytes(ARRAY_BEGIN, ARRAY_BEGIN + 0x40).hex()}
        args = {'rdi': 'buf:ids'}
        if weapon:
            args['rsi'] = GARBAGE | 4
        out.append(case(address, 'i64', cid, args, r))
    return out


def with_descriptor_ids(table, ids, format):
    data, header = table
    data = bytearray(data)
    start = 0x30 if format == 2 else 0x40
    size = 24 if format >= 4 else 12
    for k, i in enumerate(ids):
        at = header + start + size * k
        data[at:at + 4] = u32(i)
    return bytes(data), header


def weapon_list_cases(address, slot, push_back):
    words = [0, -1, 5, -1, -2, 0x7fffffff]

    def rows(n):
        out = []
        for k in range(n):
            data = bytearray(b'\x5a' * 0x100)
            data[WEAPON_WORD:WEAPON_WORD + 4] = struct.pack('<i', words[k % len(words)])
            out.append(bytes(data))
        return out

    def c(cid, flag, n=6, format=2, state=None):
        index = [(10 * k + 1, k) for k in range(n)]
        r = Repository({slot: table_with_rows(index, rows(n), format=format) if state is None else state})
        r.buffers['ids'] = {'size': 0x28, 'bytes': array_bytes(ARRAY_BEGIN, ARRAY_BEGIN + 0x40).hex()}
        listed = [] if state is not None else [k for k in range(n) if not (flag & 0xff) or words[k % len(words)] != -1]
        out = case(address, 'i64', cid, {'rdi': 'buf:ids', 'rsi': GARBAGE | flag}, r)
        out['stubs'] = push_stubs(push_back, listed)
        return out

    return [
        c('all', 0), c('listed', 1), c('flag_high_bits_only', 0x100), c('flag_ff', 0xff), c('flag_low_bit_and_high', 0x201),
        c('one_row', 1, n=1), c('two_rows', 1, n=2), c('forty_rows', 1, n=40), c('forty_rows_all', 0, n=40),
        c('wide_format', 1, format=5), c('format_1', 0, format=1), c('empty', 1, n=0),
        c('not_loaded', 1, state='missing'), c('without_holder', 0, state='no_holder'),
    ]


def clear_cases(address, slot):
    def c(cid, begin, end, repository=True):
        r = Repository({slot: table_bytes([(1, 0)])})
        r.buffers['ids'] = {'size': 0x28, 'bytes': array_bytes(begin, end).hex()}
        out = case(address, 'i64', cid, {'rdi': 'buf:ids'}, r)
        if not repository:
            out['memory'].append({'addr': REPOSITORY, 'bytes': u64(0).hex()})
            out['stubs'] = [{'address': FATAL_ERROR, 'ret': 0, 'argc': 4}]
        return out

    return [
        c('filled', ARRAY_BEGIN, ARRAY_BEGIN + 0x40),
        c('empty', ARRAY_BEGIN, ARRAY_BEGIN),
        c('end_before_start', ARRAY_BEGIN, ARRAY_BEGIN - 8),
        c('null_array', 0, 0x20),
        c('no_repository', ARRAY_BEGIN, ARRAY_BEGIN + 0x40, repository=False),
    ]

def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot, push_back = FUNCTIONS[name]
    if kind == 'count':
        cases = count_cases(address, slot)
    elif kind in ('next', 'previous'):
        cases = step_cases(address, slot)
    elif kind in ('next_matching', 'previous_matching'):
        cases = matching_step_cases(address, slot, kind == 'next_matching')
    elif kind == 'versions':
        cases = versions_cases(address, slot, push_back)
    elif kind == 'weapon_list':
        cases = weapon_list_cases(address, slot, push_back)
    elif kind == 'clear':
        cases = clear_cases(address, slot)
    else:
        cases = list_cases(address, slot, push_back, kind == 'listed')
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
