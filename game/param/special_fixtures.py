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

from find_row_fixtures import REPOSITORY, SLOT_COUNT, SLOT_SIZE, SLOTS, decoy_table, table_bytes, u32

EQUIP_WEAPON, EQUIP_PROTECTOR = 0, 1
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
# Where an equipment row keeps its reinforcement type (a signed 16-bit value).
REINFORCE_TYPE = {EQUIP_WEAPON: (0xd6, REINFORCE_WEAPON), EQUIP_PROTECTOR: (0xc8, REINFORCE_PROTECTOR)}
ROW_SIZE = 0x100


def table_with_rows(index, rows, format=2):
    """A table whose rows hold data: `rows` are each row's bytes (padded to ROW_SIZE), placed
    after the descriptors, with the index after them."""
    start = 0x30 if format == 2 else 0x40
    first_row = (start + 12 * len(rows) + 15) & ~15
    offsets = [first_row + ROW_SIZE * k for k in range(len(rows))]
    data, header = table_bytes(index, format=format, offsets=offsets, size=first_row + ROW_SIZE * len(rows))
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


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot, _ = FUNCTIONS[name]
    cases = {'equip': equip_cases, 'run': run_cases, 'spec': spec_cases}[kind](address, slot)
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
