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

from find_row_fixtures import table_bytes, u32, u64
from special_fixtures import Repository, case, table_with_rows

EQUIP_PROTECTOR, EQUIP_ACCESSORY, EQUIP_GOODS = 1, 2, 3
MAGIC, CHARA_INIT, HOLYGRAIL_EX = 14, 21, 52
PUSH_BACK, PUSH_BACK_COPY = '0x02bc1bb0', '0x016da6a0'
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


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot, push_back = FUNCTIONS[name]
    if kind == 'count':
        cases = count_cases(address, slot)
    elif kind in ('next', 'previous'):
        cases = step_cases(address, slot)
    else:
        cases = list_cases(address, slot, push_back, kind == 'listed')
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
