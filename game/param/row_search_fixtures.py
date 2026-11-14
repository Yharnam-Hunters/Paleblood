#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the accessors in game/param/row_searches.cpp, for tools/verify.py
run. Each function has its own generator, <name without param_>_cases.py, which calls write()
here.

The repository is special_fixtures.Repository (every unused slot holds a decoy table). Argument
registers carry garbage in their upper halves where the argument is 32 bits. The gem category
lists call the engine's lookup array push_back, which the cases stub: each call is compared with
the id and row of the lookup it is given. All ids, rows and values are invented; no game data is
used.
"""
import json
import os
import struct
import sys

from find_row_fixtures import REPOSITORY, SLOT_SIZE, SLOTS, table_bytes, u32, u64
from special_fixtures import Repository, case, table_with_rows

WEAPON, NPC, ATK_NPC, ATK_PC, BEHAVIOR_NPC, BEHAVIOR_PC = 0, 6, 7, 8, 12, 13
WEAPON_GEN, PROTECTOR_GEN, GEM_GEN, GEM_EFFECT, GEM_CATEGORY = 40, 41, 42, 43, 44
RESIDENT_FX, HOLYGRAIL_EX = 47, 52
PUSH_BACK_LOOKUP = '0x0231c840'
# The generation tables' row sizes (their rows follow each other).
ROW_STRIDE = {PROTECTOR_GEN: 0x2c, WEAPON_GEN: 0x30}
GARBAGE = 0xaaaaaaaa00000000

# name: (address, kind, slot)
FUNCTIONS = {
    'param_find_row_atk_param': ('0x02312fd0', 'atk', None),
    'param_find_row_behavior_param': ('0x023133c0', 'behavior', None),
    'param_table_view': ('0x0204a2c0', 'view', None),
    'param_find_row_resident_fx': ('0x02326780', 'resident_fx', RESIDENT_FX),
    'param_gem_effect_for_level': ('0x0231d330', 'gem_effect', GEM_EFFECT),
    'param_gem_gen_first_row_from': ('0x0231da10', 'first_from', GEM_GEN),
    'param_gem_gen_last_row_to': ('0x0231daf0', 'last_to', GEM_GEN),
    'param_gem_gen_row_id': ('0x0231dbe0', 'row_id', GEM_GEN),
    'param_holygrail_ex_row_number': ('0x0231ff70', 'row_number', HOLYGRAIL_EX),
    'param_find_row_number_holygrail_ex': ('0x02320020', 'by_row_number', HOLYGRAIL_EX),
    'param_list_gem_category_by_selector_0c': ('0x0231c380', 'gem_category_0c', GEM_CATEGORY),
    'param_list_gem_category_by_selector_08': ('0x0231c510', 'gem_category_08', GEM_CATEGORY),
    'param_clamp_protector_gen_indexes': ('0x023235b0', 'clamp', PROTECTOR_GEN),
    'param_clamp_weapon_gen_indexes': ('0x02329810', 'clamp', WEAPON_GEN),
}

FIVE = [(1, 0), (3, 1), (5, 2), (7, 3), (9, 4)]
BIG = [(10 * k + 1, k) for k in range(300)]


def arg32(v):
    return GARBAGE | (v & 0xffffffff)


def no_repository(address, returns, cid, args):
    out = case(address, returns, cid, args, Repository({}))
    out['buffers'] = {'out': out['buffers']['out']}
    out['memory'] = [{'addr': REPOSITORY, 'bytes': u64(0).hex()}]
    return out


def paired_cases(address, swapped):
    npc = table_bytes(FIVE, offsets=[0x1000 + 0x40 * k for k in range(5)])
    pc = table_bytes([(1, 0), (2, 1), (3, 2)], offsets=[0x2000, 0x2100, 0x2200])
    weapon = table_bytes([(3, 0), (7, 1)], offsets=[0x3000, 0x3100])
    # The behavior lookup's tables sit 5 slots after the attack lookup's.
    first = BEHAVIOR_NPC if swapped else ATK_NPC
    tables = {first: npc, first + 1: pc, WEAPON: weapon, NPC: weapon}
    if swapped:
        tables[first - 1] = weapon  # what a user of -1 would read: never, it is refused

    def args(user, id):
        first_arg, second_arg = (id, user) if swapped else (user, id)
        return {'rdi': 'buf:out', 'rsi': arg32(first_arg), 'rdx': arg32(second_arg)}

    def c(cid, user, id, t=None):
        return case(address, 'void', cid, args(user, id), Repository(tables if t is None else t))

    return [
        c('npc_found', 0, 5),
        c('npc_first', 0, 1),
        c('npc_missing', 0, 4),
        c('player_found', 1, 2),
        c('player_missing', 1, 5),
        c('user_2', 2, 5),
        c('user_largest', 0x7fffffff, 3),
        c('user_minus_1', -1, 3),   # the attack lookup reads NpcParam's slot
        c('user_minus_7', -7, 3),   # ... and here EquipParamWeapon's
        c('user_most_negative', -0x80000000, 3) if swapped else c('user_minus_6', -6, 3),
        c('negative_id', 0, -1),
        c('most_negative_id', 1, -0x80000000),
        no_repository(address, 'void', 'negative_id_no_repository', args(0, -5)),
        no_repository(address, 'void', 'user_2_no_repository', args(2, 5)),
        c('npc_not_loaded', 0, 5, {first: 'missing', first + 1: pc}),
        c('player_without_holder', 1, 2, {first: npc, first + 1: 'no_holder'}),
        c('wide_format', 0, 7, {first: table_bytes(FIVE, format=4, flags=2, offsets=[0x10, 0x20, 0x30, 0x123456789a0, 0x50])}),
        c('format_1', 1, 3, {first + 1: table_bytes([(1, 0), (3, 1)], format=1)}),
        c('row_number_negative', 0, 5, {first: table_bytes([(5, -1)])}),
        c('large_table', 1, 1491, {first + 1: table_bytes(BIG)}),
        c('large_table_missing', 1, 1500, {first + 1: table_bytes(BIG)}),
    ]


def view_cases(address):
    def c(cid, type, tables=None, extra_slot=None):
        r = Repository(tables or {})
        if extra_slot is not None:
            # A slot just past the repository's last (type 62), holding a table.
            r.buffers['repo'] = {'size': SLOTS + SLOT_SIZE * 63}
            r.add_table('past', extra_slot)
            r.memory.append({'addr': f'buf:repo+{SLOTS + SLOT_SIZE * 62}', 'bytes': u32(1).hex()})
            r.memory.append({'addr': f'buf:repo+{SLOTS + SLOT_SIZE * 62 + 8}', 'pointer': 'past_holder'})
        return case(address, 'i64', cid, {'rdi': 'buf:out', 'rsi': arg32(type)}, r)

    own = table_bytes(FIVE)
    return [
        c('type_0', 0, {0: own}),
        c('type_23', 23, {23: own}),
        c('type_61', 61, {61: own}),
        c('type_62_past_the_slots', 62, extra_slot=own),
        c('type_63', 63),
        c('type_largest', 0x7fffffff),
        c('not_loaded', 5, {5: 'missing'}),
        c('without_holder', 5, {5: 'no_holder'}),
        c('without_table', 5, {5: 'no_table'}),
    ]


def resident_fx_cases(address, slot):
    def c(cid, id, table=None, t=None):
        tables = {slot: table if table is not None else table_bytes(FIVE)} if t is None else t
        return case(address, 'void', cid, {'rdi': 'buf:out', 'rsi': arg32(id)}, Repository(tables))

    return [
        c('found_first', 1),
        c('found_last', 9),
        c('missing', 4),
        c('missing_above', 10),
        c('negative_id', -1),
        no_repository(address, 'void', 'negative_id_no_repository', {'rdi': 'buf:out', 'rsi': arg32(-3)}),
        c('row_number_negative', 5, table_bytes([(5, -7)])),
        c('row_number_past_count', 5, table_bytes([(3, 0), (5, 2)], offsets=[0x100, 0x200, 0x300])),
        c('row_number_largest', 5, table_bytes([(5, 0x7fffffff)])),
        c('rows_out_of_order', 7, table_bytes([(1, 4), (3, 0), (7, 2), (8, 1), (9, 3)])),
        c('empty', 5, table_bytes([])),
        c('large_table', 2991, table_bytes(BIG)),
        c('wide_format', 7, table_bytes(FIVE, format=4, flags=2, offsets=[0x10, 0x20, 0x30, 0xffffffff00000040, 0x50])),
        c('format_1', 3, table_bytes(FIVE, format=1)),
        c('not_loaded', 5, t={slot: 'missing'}),
        c('without_holder', 5, t={slot: 'no_holder'}),
    ]


def gem_effect_cases(address, slot):
    def row(group, rank, low, high):
        data = bytearray(b'\x5a' * 0x20)
        data[0x4:0x14] = struct.pack('<iiii', group, rank, low, high)
        return bytes(data)

    # (group, rank, lowest level, highest level)
    rows = [row(1, 10, 0, 50), row(1, 30, 10, 40), row(1, 20, 0, 99), row(2, 25, 0, 99),
            row(1, 30, 0, 99), row(1, 45, 20, 30), row(1, -1, 0, 99), row(3, 0, 5, 5)]
    index = [(100 + k, k) for k in range(len(rows))]
    table = table_with_rows(index, rows)

    def c(cid, level, group, t=None):
        return case(address, 'i32', cid, {'rdi': arg32(level), 'rsi': arg32(group)},
                    Repository({slot: table if t is None else t}))

    ids = [0x80000000 + k for k in range(len(rows))]
    high_ids = table_with_rows(index, rows)
    data, header = high_ids
    data = bytearray(data)
    for k, i in enumerate(ids):
        data[header + 0x30 + 12 * k:header + 0x34 + 12 * k] = u32(i)
    high_ids = (bytes(data), header)
    return [
        c('level_35', 35, 1),       # rank 30 (first of the two), not 45 (range 20-30)
        c('level_25', 25, 1),       # rank 20: 30 is above the level, 45 too
        c('level_30', 30, 1),       # ranks up to the level: 30 counts, then 45 is above it
        c('level_45', 45, 1),
        c('level_99', 99, 1),
        c('level_5', 5, 1),         # no rank up to 5 except -1, which never counts
        c('level_0', 0, 1),
        c('level_negative', -1, 1),
        c('group_2', 50, 2),
        c('group_2_low', 10, 2),
        c('group_3_exact', 5, 3),   # rank 0 at level 5
        c('group_3_outside', 6, 3),
        c('group_missing', 50, 9),
        c('group_negative', 50, -1),
        c('empty', 50, 1, table_bytes([])),
        c('high_ids', 35, 1, high_ids),
        c('wide_format', 35, 1, table_with_rows(index, rows, format=5)),
        c('not_loaded', 35, 1, 'missing'),
        c('without_holder', 35, 1, 'no_holder'),
    ]


def gem_gen_cases(address, slot, kind):
    ids = [5, 10, 10, 20, 0x80000000, 0xfffffff0]
    index = [(i, k) for k, i in enumerate(ids)]
    table = table_bytes(index, ids=ids)
    # Descriptors past the count (the row id accessor doesn't check the count).
    past = table_bytes(index[:3], ids=ids, offsets=[0x100 * k for k in range(6)])

    def c(cid, value, t=None):
        return case(address, 'i32', cid, {'rdi': arg32(value)}, Repository({slot: table if t is None else t}))

    if kind == 'row_id':
        return [
            c('row_0', 0),
            c('row_3', 3),
            c('row_high_bit', 4),
            c('row_last', 5),
            c('row_past_count', 4, past),
            c('row_at_count', 3, past),
            c('row_negative', -1),
            no_repository(address, 'i32', 'row_negative_no_repository', {'rdi': arg32(-1)}),
            c('wide_format', 2, table_bytes(index, ids=ids, format=4, flags=2)),
            c('format_1', 3, table_bytes(index, ids=ids, format=1)),
            c('format_0', 1, table_bytes(index, ids=ids, format=0)),
            c('not_loaded', 0, 'missing'),
            c('without_holder', 0, 'no_holder'),
        ]
    return [
        c('exact_first', 5),
        c('exact_duplicate', 10),
        c('exact_last_small', 20),
        c('between', 7),
        c('between_2', 15),
        c('below_all', 0),
        c('above_small', 21),
        c('largest', 0x7fffffff),
        c('negative', -1),
        no_repository(address, 'i32', 'negative_no_repository', {'rdi': arg32(-5)}),
        c('empty', 5, table_bytes([])),
        c('single_row_below', 3, table_bytes([(5, 0)], ids=[5])),
        c('single_row_above', 6, table_bytes([(5, 0)], ids=[5])),
        c('past_count_not_searched', 20, past),
        c('wide_format', 15, table_bytes(index, ids=ids, format=4, flags=2)),
        c('format_1', 15, table_bytes(index, ids=ids, format=1)),
        c('format_3_flag_ignored', 11, table_bytes(index, ids=ids, format=3, flags=2)),
        c('not_loaded', 5, 'missing'),
        c('without_holder', 5, 'no_holder'),
        c('large_table', 1495, table_bytes(BIG, ids=[10 * k + 1 for k in range(300)])),
    ]


def row_number_cases(address, slot):
    def c(cid, id, t=None):
        r = Repository({slot: table_bytes(FIVE) if t is None else t})
        r.buffers['lookup'] = {'size': 0x10, 'bytes': (u32(id) + b'\xcc' * 12).hex()}
        return case(address, 'i32', cid, {'rdi': 'buf:lookup'}, r)

    return [
        c('first', 1),
        c('last', 9),
        c('missing', 4),
        c('negative_missing', -1),
        c('negative_in_index', -1, table_bytes([(1, 0), (3, 1), (0xffffffff, 2)])),
        c('row_number_negative', 5, table_bytes([(5, -9)])),
        c('row_number_large', 5, table_bytes([(5, 0x12345)])),
        c('rows_out_of_order', 7, table_bytes([(1, 4), (3, 0), (7, 2), (8, 1), (9, 3)])),
        c('empty', 5, table_bytes([])),
        c('large_table', 1491, table_bytes(BIG)),
        c('index_before_header', 3, table_bytes([(1, 0), (3, 1), (5, 0)], backwards=True)),
        c('not_loaded', 5, 'missing'),
        c('without_holder', 5, 'no_holder'),
    ]


def by_row_number_cases(address, slot):
    ids = [1, 3, 5, 0x80000007, 9]
    # Index ids as the descriptors' (row 3's id is negative as a signed number); id 4 is not in it.
    table = table_bytes([(1, 0), (3, 1), (5, 2), (9, 4), (0x80000007, 3)], ids=ids)
    unlisted = table_bytes([(1, 0), (3, 1), (9, 2)], ids=[1, 4, 9])

    def c(cid, row_number, t=None):
        return case(address, 'void', cid, {'rdi': 'buf:out', 'rsi': arg32(row_number)},
                    Repository({slot: table if t is None else t}))

    return [
        c('row_0', 0),
        c('row_2', 2),
        c('row_last', 4),
        c('row_negative_id', 3),
        c('row_at_count', 5),
        c('row_far_past', 0x7fffffff),
        c('row_negative', -1),
        c('id_not_in_index', 1, unlisted),
        c('empty', 0, table_bytes([])),
        c('wide_format', 2, table_bytes([(1, 0), (3, 1), (5, 2), (9, 4), (0x80000007, 3)], ids=ids, format=4, flags=2)),
        c('not_loaded', 0, 'missing'),
        c('without_holder', 0, 'no_holder'),
    ]


def gem_category_cases(address, slot, selector):
    def row(category):
        # The other selector holds a value no case asks for.
        data = bytearray(b'\x5a' * 0x20)
        data[0x8:0x10] = struct.pack('<ii', 0x13572468, 0x13572468)
        data[selector:selector + 4] = struct.pack('<i', category)
        return bytes(data)

    categories = [3, 1, 3, -1, 3, 7]
    rows = [row(v) for v in categories]
    index = [(10 * (k + 1), k) for k in range(len(rows))]

    def c(cid, category, t=None, pushes=None):
        r = Repository({slot: table_with_rows(index, rows) if t is None else t})
        r.buffers['rows'] = {'size': 0x28, 'bytes': (u64(0x1111) + u64(0x5000) + u64(0x5040) + u64(0x6000) + u64(0x2222)).hex()}
        if pushes is None:
            pushes = sum(1 for v in categories if v == category)
        out = case(address, 'void', cid, {'rdi': 'buf:rows', 'rsi': arg32(category)}, r)
        out['stubs'] = [{'address': PUSH_BACK_LOOKUP, 'ret': 0, 'argc': 2, 'argi': [0],
                         'argmem': [{'arg': 1, 'size': 4}, {'arg': 1, 'offset': 8, 'size': 8}]}
                        for _ in range(pushes)]
        return out

    # Row 2's id is not in the index, and row 4's is negative as a signed number.
    odd = table_with_rows([(10, 0), (20, 1), (0x80000050, 4), (40, 3)], rows[:5])
    data, header = odd
    data = bytearray(data)
    for k, i in ((2, 30), (4, 0x80000050)):
        data[header + 0x30 + 12 * k:header + 0x34 + 12 * k] = u32(i)
    odd = (bytes(data), header)
    return [
        c('three_rows', 3),
        c('one_row', 7),
        c('negative_category', -1),
        c('none', 5),
        c('zero', 0),
        c('ids_odd', 3, odd, pushes=3),
        c('empty', 3, table_bytes([]), pushes=0),
        c('wide_format', 3, table_with_rows(index, rows, format=5)),
        c('not_loaded', 3, 'missing', pushes=0),
        c('without_holder', 3, 'no_holder', pushes=0),
    ]


def clamp_cases(address, slot):
    stride = ROW_STRIDE[slot]
    values = [0, 63, 64, 0xffffffff, 0x80000000, 0x7fffffff, 1, 0x40, 62, 0x3f]

    def table(rows, format=2, flags=0, gap=False):
        """`rows` rows of `stride` bytes after the first row's data offset; other descriptors hold
        bogus offsets (the clamp reads only the first)."""
        start = 0x30 if format == 2 else 0x40
        wide = format >= 4 and flags & 2
        first = (start + (24 if wide else 12) * max(rows, 1) + 15) & ~15
        offsets = [first] + [0xbad00000 + k for k in range(1, rows)]
        size = first + stride * rows
        data, header = table_bytes([(k + 1, k) for k in range(rows)], format=format, flags=flags,
                                   offsets=offsets if rows else [], size=max(size, start))
        data = bytearray(data)
        for r in range(rows):
            for e in range(5):
                at = header + first + stride * r + 8 * e
                data[at:at + 8] = u32(0x1000 + 0x10 * r + e) + u32(values[(r * 5 + e) % len(values)])
            at = header + first + stride * r + 0x28
            data[at:at + stride - 0x28] = bytes([0xf0 + r]) * (stride - 0x28)
        return bytes(data), header

    def c(cid, t):
        return case(address, 'void', cid, {}, Repository({slot: t}))

    return [
        c('two_rows', table(2)),
        c('one_row', table(1)),
        c('five_rows', table(5)),
        c('empty', table(0)),
        c('format_4_narrow', table(3, format=4, flags=0xfd)),
        c('format_4_wide', table(3, format=4, flags=2)),
        c('format_1', table(2, format=1)),
        c('not_loaded', 'missing'),
        c('without_holder', 'no_holder'),
        c('without_table', 'no_table'),
    ]


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot = FUNCTIONS[name]
    cases = {
        'atk': lambda: paired_cases(address, False),
        'behavior': lambda: paired_cases(address, True),
        'view': lambda: view_cases(address),
        'resident_fx': lambda: resident_fx_cases(address, slot),
        'gem_effect': lambda: gem_effect_cases(address, slot),
        'first_from': lambda: gem_gen_cases(address, slot, kind),
        'last_to': lambda: gem_gen_cases(address, slot, kind),
        'row_id': lambda: gem_gen_cases(address, slot, kind),
        'row_number': lambda: row_number_cases(address, slot),
        'by_row_number': lambda: by_row_number_cases(address, slot),
        'gem_category_0c': lambda: gem_category_cases(address, slot, 0xc),
        'gem_category_08': lambda: gem_category_cases(address, slot, 0x8),
        'clamp': lambda: clamp_cases(address, slot),
    }[kind]()
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
