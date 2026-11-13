#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the row lookups in game/param/special_lookups.cpp that need more than
find_row_fixtures.py's single table: the equipment lookups (equipment and reinforcement tables,
rows with data), the menu runs of consecutive ids, and the menu property lookup with its built-in
fallback. Each function has its own generator, <name without param_>_cases.py, which calls
write() here.

As in find_row_fixtures.py, every slot that a case does not use holds a decoy table. All ids,
rows and built-in entries are invented; the built-in menu property rows are written over the
image's for each case, so no game data is read or used.
"""
import json
import os
import struct
import sys

from find_row_fixtures import REPOSITORY, SLOT_COUNT, SLOT_SIZE, SLOTS, decoy_table, table_bytes, u32, u64

EQUIP_WEAPON, EQUIP_PROTECTOR, EQUIP_GOODS = 0, 1, 3
REINFORCE_WEAPON, REINFORCE_PROTECTOR = 4, 5
NEW_MENU_COLOR, MENU_PROPERTY_SPEC, MENU_VALUE, CHAR_MAKE_LIST = 58, 59, 61, 57
BUILT_IN_SPECS, BUILT_IN_COUNT, BUILT_IN_SIZE = 0x05528730, 13, 0x24

# name: (address, kind, slot, returns)
FUNCTIONS = {
    'param_find_row_equip_param_weapon': ('0x02329aa0', 'equip', EQUIP_WEAPON, 'void'),
    'param_find_row_equip_param_protector': ('0x02323840', 'equip', EQUIP_PROTECTOR, 'void'),
    'param_find_rows_char_make_menu_list_item': ('0x01f83130', 'run', CHAR_MAKE_LIST, 'void'),
    'param_find_rows_new_menu_color_table': ('0x01f82cc0', 'run', NEW_MENU_COLOR, 'void'),
    'param_find_rows_menu_value_table': ('0x0206ddd0', 'run', MENU_VALUE, 'void'),
    'param_menu_property_spec': ('0x0206ecc0', 'spec', MENU_PROPERTY_SPEC, 'i64'),
}
# The weapon steps (0x0232b6d0 next, 0x0232b950 previous): (address, kind, slot, backwards).
FUNCTIONS.update({
    'param_next_weapon_id_with_flag': ('0x0232b6d0', 'weapon_step', EQUIP_WEAPON, False),
    'param_previous_weapon_id_with_flag': ('0x0232b950', 'weapon_step', EQUIP_WEAPON, True),
})
# The set checks: (address, kind, slot, the set table's details).
SET_CHECKS = {
    # slot: (capacity, built flag, ids, set numbers, cache set offset, row set offset, id at, unit)
    EQUIP_GOODS: (70, 0x05993278, 0x05993040, 0x05993160, 0x32, 0x32, 0x0, 1),
    EQUIP_PROTECTOR: (50, 0x05993798, 0x05993600, 0x059936d0, 0xd8, 0xca, 0x10, 1),
    EQUIP_WEAPON: (50, 0x05993d18, 0x05993b80, 0x05993c50, 0xd8, 0xd8, 0x10, 100000),
}
FUNCTIONS.update({
    'param_check_set_equip_param_goods': ('0x0231eec0', 'set_check', EQUIP_GOODS, None),
    'param_check_set_equip_param_protector': ('0x023259c0', 'set_check', EQUIP_PROTECTOR, None),
    'param_check_set_equip_param_weapon': ('0x0232bce0', 'set_check', EQUIP_WEAPON, None),
})
WEAPON_STEP_FLAGS = 0x100   # the 40-bit flag field the steps test (bit 0, 1 or 2 by mode)
GARBAGE = 0xaaaaaaaa00000000  # upper half of a 32-bit argument register, to be ignored
# Where an equipment row keeps its reinforcement type (a signed 16-bit value).
REINFORCE_TYPE = {EQUIP_WEAPON: (0xd6, REINFORCE_WEAPON), EQUIP_PROTECTOR: (0xc8, REINFORCE_PROTECTOR)}
ROW_SIZE = 0x100


def table_with_rows(index, rows, format=2, row_size=ROW_SIZE, flags=0, ids=None):
    """A table whose rows hold data: `rows` are each row's bytes (padded to row_size), placed
    after the descriptors, with the index after them."""
    start = 0x30 if format == 2 else 0x40
    descriptor = 24 if format >= 4 and flags & 2 else 12
    first_row = (start + descriptor * len(rows) + 15) & ~15
    offsets = [first_row + row_size * k for k in range(len(rows))]
    data, header = table_bytes(index, format=format, flags=flags, offsets=offsets, ids=ids,
                               size=first_row + row_size * len(rows))
    data = bytearray(data)
    for k, row in enumerate(rows):
        at = header + offsets[k]
        data[at:at + len(row)] = row
    return bytes(data), header


class Repository:
    """Slot tables: a (bytes, header offset) pair, 'missing' (slot not loaded), 'no_holder' or
    'no_table' (a holder whose file has no table); every other slot holds the decoy."""

    def __init__(self, tables):
        self.buffers = {}
        self.memory = [{'addr': REPOSITORY, 'pointer': 'repo'}]
        self.buffers['repo'] = {'size': SLOTS + SLOT_SIZE * SLOT_COUNT}
        decoy, decoy_header = decoy_table()
        self.add_table('decoy', (decoy, decoy_header))
        for s in range(SLOT_COUNT):
            at = SLOTS + SLOT_SIZE * s
            table = tables.get(s, 'decoy')
            loaded = 0 if table == 'missing' else 1
            self.memory.append({'addr': f'buf:repo+{at}', 'bytes': u32(loaded).hex()})
            if table in ('missing', 'no_holder'):
                continue
            name = 'decoy' if table == 'decoy' else f's{s}'
            if table == 'no_table':
                self.buffers[f'{name}_holder'] = {'size': 0x78}
                self.buffers[f'{name}_file'] = {'size': 0x78}
                self.memory.append({'addr': f'buf:{name}_holder+112', 'pointer': f'{name}_file'})
            elif table != 'decoy':
                self.add_table(name, table)
            self.memory.append({'addr': f'buf:repo+{at + 8}', 'pointer': f'{name}_holder'})

    def add_table(self, name, table):
        data, header = table
        self.buffers[f'{name}_holder'] = {'size': 0x78}
        self.buffers[f'{name}_file'] = {'size': 0x78}
        self.buffers[f'{name}_table'] = {'size': len(data), 'bytes': data.hex()}
        self.memory.append({'addr': f'buf:{name}_holder+112', 'pointer': f'{name}_file'})
        self.memory.append({'addr': f'buf:{name}_file+112', 'pointer': f'{name}_table+{header}'})


def case(address, returns, cid, args, repository, extra_memory=()):
    buffers = dict(repository.buffers)
    buffers['out'] = {'size': 0x30, 'bytes': 'cc' * 0x30}
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
            'buffers': buffers, 'memory': repository.memory + list(extra_memory), 'stubs': [], 'imports': []}


def equip_cases(address, slot):
    type_offset, reinforce_slot = REINFORCE_TYPE[slot]

    def row(reinforce_type):
        data = bytearray(b'\x5a' * ROW_SIZE)
        data[type_offset:type_offset + 2] = struct.pack('<h', reinforce_type)
        return bytes(data)

    # Base items 0, 100, 200 (missing), 1000, 0x7fffffd0, with these reinforcement types.
    equip_index = [(0, 0), (100, 1), (1000, 2), (0x7fffffd0, 3), (5000, 4)]
    equip_rows = [row(0), row(5), row(300), row(-3), row(0x7fff)]
    reinforce_ids = list(range(0, 25)) + list(range(300, 330)) + [0x7fff + 50]
    reinforce = table_bytes([(i, k) for k, i in enumerate(reinforce_ids)],
                            offsets=[0x8000 + 0x20 * k for k in range(len(reinforce_ids))])
    full = {slot: table_with_rows(equip_index, equip_rows), reinforce_slot: reinforce}

    def c(cid, id, tables=None):
        return case(address, 'void', cid, {'rdi': 'buf:out', 'rsi': id & 0xffffffff},
                    Repository(full if tables is None else tables))

    return [
        c('base_item', 100),
        c('level_5', 105),
        c('level_99_reinforcement_missing', 199),
        c('item_zero', 0),
        c('item_zero_level_7', 7),
        c('base_item_missing', 250),
        c('large_reinforcement_type', 1029),
        c('negative_reinforcement_id', 2147483601),
        c('largest_id', 0x7fffffff),
        c('largest_type', 5050),
        c('negative_id', -1),
        c('negative_id_level', -105),
        c('equip_not_loaded', 105, {slot: 'missing', reinforce_slot: reinforce}),
        c('equip_without_holder', 105, {slot: 'no_holder', reinforce_slot: reinforce}),
        c('reinforcement_not_loaded', 105, {slot: full[slot], reinforce_slot: 'missing'}),
        c('wide_format', 105, {slot: table_with_rows(equip_index, equip_rows, format=5),
                               reinforce_slot: reinforce}),
    ]


def run_cases(address, slot):
    ids = [0, 1, 10, 11, 12, 13, 20, 22, 0xfffffffe, 0xffffffff]
    table = table_bytes([(i, k) for k, i in enumerate(ids)])

    def c(cid, id, tables=None):
        return case(address, 'void', cid, {'rdi': 'buf:out', 'rsi': id & 0xffffffff},
                    Repository({slot: table} if tables is None else tables))

    with_gap = table_bytes([(10, 0), (11, 1), (12, -1), (13, 2)])
    return [
        c('run_of_four', 10),
        c('run_of_one', 13),
        c('run_from_middle', 11),
        c('single', 20),
        c('missing', 21),
        c('from_zero', 0),
        c('wraps_around', -2),
        c('negative_row_number_ends_run', 10, {slot: with_gap}),
        c('first_row_number_negative', 12, {slot: with_gap}),
        c('empty_table', 10, {slot: table_bytes([])}),
        c('not_loaded', 10, {slot: 'missing'}),
        c('without_holder', 10, {slot: 'no_holder'}),
        c('without_table', 10, {slot: 'no_table'}),
        c('wide_format', 11, {slot: table_bytes([(i, k) for k, i in enumerate(ids)], format=4, flags=2)}),
    ]


def spec_cases(address, slot):
    ids = [3, 7, 100, 0x80000000]
    table = table_bytes([(i, k) for k, i in enumerate(ids)])
    built_in_ids = [50, 51, 52, 7, 0xffffffff, 60, 61, 62, 63, 64, 64, 66, 99]
    built_in = b''.join(u32(i) + bytes([0x40 + k]) * 0x20 for k, i in enumerate(built_in_ids))
    built_in_memory = [{'addr': f'0x{BUILT_IN_SPECS:08x}', 'bytes': built_in.hex()}]

    def c(cid, id, tables=None):
        return case(address, 'i64', cid, {'rdi': id & 0xffffffff},
                    Repository({slot: table} if tables is None else tables), built_in_memory)

    return [
        c('in_table', 100),
        c('in_table_and_built_in', 7),
        c('in_table_unsigned', -0x80000000),
        c('built_in_first', 50),
        c('built_in_last', 99),
        c('built_in_duplicate', 64),
        c('built_in_negative', -1),
        c('nowhere', 1000),
        c('zero', 0),
        c('not_loaded_built_in', 51, {slot: 'missing'}),
        c('not_loaded_in_table_id', 100, {slot: 'missing'}),
        c('without_holder', 61, {slot: 'no_holder'}),
        c('without_table', 62, {slot: 'no_table'}),
        c('without_table_nowhere', 5, {slot: 'no_table'}),
        c('negative_row_number', 3, {slot: table_bytes([(3, -1), (7, 0)])}),
    ]


def weapon_step_cases(address, slot, backwards):
    # Weapon ids (index and descriptor ids alike) and the flags of each row; 105 and 410 are
    # upgrades whose lookup reads their base rows 100 and 400.
    ids = [100, 105, 200, 300, 400, 410, 500, 600]
    flags = [0b001, 0b110, 0b010, 0b000, 0b100, 0b011, 0b111, 0b011]
    weapon_row_size = 0x110

    def rows(values):
        out = []
        for v in values:
            data = bytearray(b'\x5a' * weapon_row_size)
            data[WEAPON_STEP_FLAGS:WEAPON_STEP_FLAGS + 5] = bytes([v | 0xf8, 0xff, 0xff, 0xff, 0xff])
            out.append(bytes(data))
        return out

    def table(index=None, format=2, table_flags=0, descriptor_ids=None, n=len(ids)):
        index = [(i, k) for k, i in enumerate(ids[:n])] if index is None else index
        return table_with_rows(index, rows(flags[:n]), format=format, row_size=weapon_row_size, flags=table_flags,
                               ids=ids[:n] if descriptor_ids is None else descriptor_ids)

    def c(cid, id, step, mode, t=None, tables=None):
        if tables is None:
            tables = {slot: table() if t is None else t}
        args = {'rdi': GARBAGE | (id & 0xffffffff), 'rsi': GARBAGE | (step & 0xffffffff),
                'rdx': GARBAGE | (mode & 0xffffffff)}
        return case(address, 'i32', cid, args, Repository(tables))

    start = 600 if backwards else 100
    out = []
    for mode in (0, 1, 2, 3, 7, -1):
        for id in (100, 105, 200, 300, 400, 410, 500, 600):
            out.append(c(f'mode{mode & 0xff:x}_from{id}', id, 1, mode))
    for step in (2, 3, 7, 8, -1, -2, 0x7fffffff, -0x80000000):
        for mode in (0, 2):
            out.append(c(f'step{step & 0xffffffff:x}_mode{mode}', start, step, mode))
    out += [
        c('step0_flag_set', 100, 0, 0), c('step0_mode3', 300, 0, 3),
        c('negative_id', -1, 1, 0), c('negative_id_mode3', -5, 1, 3), c('most_negative_id', -0x80000000, 1, 1),
        c('missing_id', 150, 1, 0), c('missing_above', 700, 1, 0), c('missing_below', 50, 1, 2),
        c('row_number_negative', 300, 1, 0, table(index=[(i, -2 if i == 300 else k) for k, i in enumerate(ids)])),
        c('row_number_past_count', 300, 1, 0, table(index=[(i, 20 if i == 300 else k) for k, i in enumerate(ids)])),
        # Row 4's descriptor names an id the index lacks: the walk stops there unless it matches.
        c('descriptor_not_indexed', start, 1, 1, table(descriptor_ids=ids[:4] + [403] + ids[5:])),
        c('one_row', 100, 1, 0, table(n=1)), c('one_row_negative', -1, 1, 0, table(n=1)),
        c('empty', 100, 1, 0, table(n=0)), c('empty_negative', -1, 1, 0, table(n=0)),
        c('wide_format', start, 1, 2, table(format=5, table_flags=2)),
        c('wide_negative', -1, 1, 2, table(format=4, table_flags=2)),
        c('format_4_narrow', start, 1, 1, table(format=4)),
        c('format_1', start, 1, 0, table(format=1)), c('format_1_negative', -1, 1, 0, table(format=1)),
        c('not_loaded', 100, 1, 0, tables={slot: 'missing'}),
        c('not_loaded_negative', -1, 1, 0, tables={slot: 'missing'}),
        c('without_holder', 100, 1, 0, tables={slot: 'no_holder'}),
    ]
    return out


def set_check_cases(address, slot):
    capacity, built_at, ids_at, sets_at, cache_offset, row_offset, id_at, unit = SET_CHECKS[slot]

    def rows(sets):
        out = []
        for v in sets:
            data = bytearray(b'\x5a' * ROW_SIZE)
            data[cache_offset:cache_offset + 2] = struct.pack('<h', v)
            out.append(bytes(data))
        return out

    def table(ids, sets, format=2, table_flags=0):
        return table_with_rows([(i, k) for k, i in enumerate(ids)], rows(sets), format=format, flags=table_flags, ids=ids)

    # Eight rows: sets 0 (ids 0, 2, 7), 1 (ids 3, 4) and 2 (id 6), two rows without a set.
    ids = [unit * (k + 1) for k in range(8)]
    sets = [0, -1, 0, 1, 1, -2, 2, 0]

    def cache_words(values):
        values = list(values) + [-1] * (capacity - len(values))
        return b''.join(struct.pack('<i', v) for v in values).hex()

    def c(cid, id, row_set=0, marked=b'', t=None, tables=None, built=None, row=True):
        """built: None builds the cache from the table on this call; else (ids, set numbers)."""
        if tables is None:
            tables = {slot: table(ids, sets) if t is None else t}
        r = Repository(tables)
        lookup = bytearray(b'\xcc' * 0x30)
        lookup[0:4] = struct.pack('<i', 0x7777)
        lookup[id_at:id_at + 4] = struct.pack('<i', id)
        r.buffers['lookup'] = {'size': 0x30, 'bytes': bytes(lookup).hex()}
        row_bytes = bytearray(b'\x5a' * ROW_SIZE)
        row_bytes[row_offset:row_offset + 2] = struct.pack('<h', row_set)
        r.buffers['row'] = {'size': ROW_SIZE, 'bytes': bytes(row_bytes).hex()}
        r.buffers['marked'] = {'size': 0x10, 'bytes': (bytes(marked) + bytes(0x10 - len(marked))).hex()}
        extra = [{'addr': 'buf:lookup+8', **({'pointer': 'row+0'} if row else {'bytes': u64(0).hex()})}]
        if built is None:
            extra += [{'addr': f'0x{built_at:08x}', 'bytes': '00'},
                      {'addr': f'0x{ids_at:08x}', 'bytes': cache_words([0x3c3c3c3c] * capacity)},
                      {'addr': f'0x{sets_at:08x}', 'bytes': cache_words([0x3c3c] * capacity)}]
        else:
            extra += [{'addr': f'0x{built_at:08x}', 'bytes': '01'},
                      {'addr': f'0x{ids_at:08x}', 'bytes': cache_words(built[0])},
                      {'addr': f'0x{sets_at:08x}', 'bytes': cache_words(built[1])}]
        out = case(address, 'i32', cid, {'rdi': 'buf:lookup', 'rsi': 'buf:marked'}, r, extra)
        if built is None:
            # The cache is filled with 0xff bytes by the C library's memset, scripted here.
            fill = [{'arg': 0, 'bytes': 'ff' * 4 * capacity}]
            out['imports'] = [{'name': 'memset', 'argc': 3, 'ret': f'0x{ids_at:08x}', 'writes': fill},
                              {'name': 'memset', 'argc': 3, 'ret': f'0x{sets_at:08x}', 'writes': fill}]
        return out

    u = unit
    full = [u * (k + 1) for k in range(capacity + 6)]
    out = [
        c('first_of_set_0', u, 0), c('second_of_set_0', 3 * u, 0, marked=b'\x81'),
        c('last_of_set_0_others_marked', 8 * u, 0, marked=b'\x05'), c('set_0_one_other_marked', 8 * u, 0, marked=b'\x01'),
        c('set_1_complete', 5 * u, 1, marked=b'\x08'), c('set_1_unmarked', 4 * u, 1),
        c('set_2_single', 7 * u, 2), c('set_2_single_marked_before', 7 * u, 2, marked=b'\x10'),
        c('set_without_rows', 2 * u, 9, marked=b'\xff'), c('id_not_in_cache', 9 * u, 0, marked=b'\x07'),
        c('id_not_in_cache_unmarked', 9 * u, 1, marked=b'\x04'), c('all_marked', u, 0, marked=b'\xff\xff'),
        c('row_set_negative', u, -1), c('row_set_most_negative', u, -0x8000), c('row_missing', u, 0, row=False),
        c('negative_id', -u, 0), c('negative_id_all_marked', -u, 0, marked=b'\xff' * 16), c('most_negative_id', -0x80000000, 0), c('zero_id', 0, 0),
        c('row_set_large', u, 0x7fff),
        c('wide_format', 3 * u, 0, marked=b'\x81', t=table(ids, sets, format=5, table_flags=2)),
        c('format_1', 3 * u, 0, marked=b'\x81', t=table(ids, sets, format=1)),
        c('empty_table', u, 0, t=table([], [])),
        c('not_loaded', u, 0, tables={slot: 'missing'}), c('without_holder', u, 1, tables={slot: 'no_holder'}),
        c('full_table', 2 * u, 0, marked=b'\xfd' + b'\xff' * 15, t=table(full, [0] * len(full))),
        c('full_table_last_entry', capacity * u, 0, marked=b'\xff' * 16, t=table(full, [0] * len(full))),
        c('full_table_past_capacity', (capacity + 3) * u, 0, marked=b'\xff' * 16, t=table(full, [0] * len(full))),
        c('built_cache', 0x1234 * u, 3, marked=b'\x02\x00\x00\x00\x00\x00\x01',
          built=([5 * u, 0x1234 * u, 7 * u] + [-1] * 45 + [9 * u], [3, 3, 4] + [-1] * 45 + [3])),
        c('built_cache_complete', 5 * u, 3, marked=b'\x02\x00\x00\x00\x00\x00\x01',
          built=([5 * u, 0x1234 * u, 7 * u] + [-1] * 45 + [9 * u], [3, 3, 4] + [-1] * 45 + [3])),
        c('built_cache_ignores_table', 3 * u, 0, built=([], [])),
        c('built_cache_duplicate_ids', 5 * u, 3, marked=b'\x00',
          built=([5 * u, 5 * u, 6 * u], [3, 3, 3])),
        c('built_cache_negative_set_entry', 5 * u, 0, built=([5 * u, 6 * u], [0, -1])),
    ]
    if unit != 1:
        rounded = [u, u + 5, 2 * u, 3 * u - 1, 3 * u, 4 * u + 100, 5 * u, 6 * u]
        out += [
            c('base_id_rounded', 2 * u + 300, 0, marked=b'\x01', t=table(rounded, [0, 0, 0, 0, 1, 0, 1, 0])),
            c('unrounded_rows_skipped', 4 * u, 0, marked=b'\x03', t=table(rounded, [0, 0, 0, 0, 1, 0, 0, 0])),
            c('base_id_below_unit', u - 1, 0, t=table([0, u], [0, 0])),
            c('negative_base_id_all_marked', -5, 0, marked=b'\xff' * 16, t=table([0, u], [0, 0])),
            c('base_id_largest', 0x7fffffff, 0, t=table(rounded, [0] * 8)),
            c('full_then_unrounded', 2 * u, 0, marked=b'\xff' * 16, t=table(full + [u * (capacity + 9) + 1], [0] * (len(full) + 1))),
        ]
    return out


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot, extra = FUNCTIONS[name]
    if kind == 'weapon_step':
        cases = weapon_step_cases(address, slot, extra)
    elif kind == 'set_check':
        cases = set_check_cases(address, slot)
    else:
        cases = {'equip': equip_cases, 'run': run_cases, 'spec': spec_cases}[kind](address, slot)
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
