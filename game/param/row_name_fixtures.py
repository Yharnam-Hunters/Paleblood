#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the row name lookups and row-at accessors of the solo parameter
repository (game/param/row_names.cpp), for tools/verify.py run. Each function has its own
generator, row_name_<type>_cases.py or row_at_<type>_cases.py, which calls write() here.

The repository and tables are built as in find_row_fixtures.py: every other slot holds a decoy
table, so a lookup in the wrong slot answers differently. A name lookup reads the id from a
lookup result (bytes after the id are 0xcc); names are only addressed, never read, so the name
offsets are arbitrary values. All ids, offsets and rows are invented; no game data is used.
"""
import json
import os
import sys

from find_row_fixtures import REPOSITORY, SLOT_COUNT, SLOT_SIZE, SLOTS, decoy_table, table_bytes, u32, u64

# name: (address, kind, table type index in the repository)
FUNCTIONS = {
    'param_row_name_equip_param_weapon': ('0x02329c20', 'name', 0),
    'param_row_name_equip_param_protector': ('0x023239d0', 'name', 1),
    'param_row_name_equip_param_accessory': ('0x02311da0', 'name', 2),
    'param_row_name_equip_param_goods': ('0x0231e680', 'name', 3),
    'param_row_name_chara_init': ('0x023150d0', 'name', 21),
    'param_row_name_face_gen': ('0x02317800', 'name', 23),
    'param_row_name_weapon_gen': ('0x02329630', 'name', 40),
    'param_row_name_gem_gen': ('0x0231d750', 'name', 42),
    'param_row_name_gem_gen_2': ('0x0231df60', 'name', 42),
    'param_row_name_gem_effect': ('0x0231d210', 'name', 43),
    'param_row_name_holygrail_ex': ('0x0231f840', 'name', 52),
    'param_row_at_equip_param_goods': ('0x0231e4f0', 'at', 3),
    'param_row_at_magic': ('0x02321200', 'at', 14),
    'param_row_at_menu_color_table': ('0x02321be0', 'at', 18),
    'param_row_at_chara_init': ('0x02314f40', 'at', 21),
}


def repository(slot, table, loaded=1, holder=True):
    """Buffers and memory for a repository whose slot `slot` holds `table` (bytes, header)."""
    buffers, memory = {}, []
    decoy, decoy_header = decoy_table()
    buffers['repo'] = {'size': SLOTS + SLOT_SIZE * SLOT_COUNT}
    buffers['decoy_holder'] = {'size': 0x78}
    buffers['decoy_file'] = {'size': 0x78}
    buffers['decoy_table'] = {'size': len(decoy), 'bytes': decoy.hex()}
    memory.append({'addr': REPOSITORY, 'pointer': 'repo'})
    memory.append({'addr': 'buf:decoy_holder+112', 'pointer': 'decoy_file'})
    memory.append({'addr': 'buf:decoy_file+112', 'pointer': f'decoy_table+{decoy_header}'})
    for s in range(SLOT_COUNT):
        at = SLOTS + SLOT_SIZE * s
        if s != slot:
            memory.append({'addr': f'buf:repo+{at}', 'bytes': u32(1).hex()})
            memory.append({'addr': f'buf:repo+{at + 8}', 'pointer': 'decoy_holder'})
            continue
        memory.append({'addr': f'buf:repo+{at}', 'bytes': u32(loaded).hex()})
        if not holder:
            continue
        data, header = table
        buffers['holder'] = {'size': 0x78}
        buffers['file'] = {'size': 0x78}
        buffers['table'] = {'size': len(data), 'bytes': data.hex()}
        memory.append({'addr': f'buf:repo+{at + 8}', 'pointer': 'holder'})
        memory.append({'addr': 'buf:holder+112', 'pointer': 'file'})
        memory.append({'addr': 'buf:file+112', 'pointer': f'table+{header}'})
    return buffers, memory


def name_cases(address, slot):
    def c(cid, id, *index_args, loaded=1, holder=True, with_repository=True, **table_kw):
        if not index_args:
            index_args = ([(5, 0)],)
        if with_repository:
            buffers, memory = repository(slot, table_bytes(*index_args, **table_kw), loaded, holder)
        else:
            buffers, memory = {}, [{'addr': REPOSITORY, 'bytes': u64(0).hex()}]
        buffers['lookup'] = {'size': 0x10, 'bytes': (u32(id) + b'\xcc' * 12).hex()}
        return {'schema': 1, 'address': address, 'id': cid, 'returns': 'i64',
                'args': {'rdi': 'buf:lookup', 'rsi': 0x1234}, 'buffers': buffers, 'memory': memory,
                'stubs': [], 'imports': []}

    five = [(1, 0), (3, 1), (5, 2), (7, 3), (9, 4)]
    names = [0x400, 0x0, 0x480, 0x80000000, 0xffffffff]
    big = [(10 * k + 1, k) for k in range(300)]
    return [
        c('first', 1, five, names=names),
        c('name_offset_zero', 3, five, names=names),
        c('middle', 5, five, names=names),
        c('name_offset_high_bit', 7, five, names=names),
        c('name_offset_all_ones', 9, five, names=names),
        c('missing_between', 4, five, names=names),
        c('missing_above', 11, five, names=names),
        c('missing_below', 0, five, names=names),
        c('id_zero', 0, [(0, 0), (2, 1)]),
        c('id_largest', 0x7fffffff, [(3, 0), (0x7fffffff, 1)]),
        c('negative_id', -1, five),
        c('most_negative_id', -0x80000000, five),
        c('negative_id_no_repository', -7, with_repository=False),
        c('row_number_negative', 5, [(5, -1)]),
        # The index may give a row number past the counted rows: the name lookup checks it.
        c('row_number_past_count', 5, [(3, 0), (5, 2)], offsets=[0x100, 0x200, 0x300]),
        c('row_number_at_count', 5, [(3, 0), (5, 2), (8, 1)], offsets=[0x100, 0x200, 0x300, 0x400]),
        c('row_number_last', 8, [(3, 0), (5, 2), (8, 1)], offsets=[0x100, 0x200, 0x300], names=[0x10, 0x20, 0x30]),
        c('row_number_large', 5, [(5, 0x7fffffff)]),
        c('rows_out_of_order', 7, [(1, 4), (3, 0), (7, 2), (8, 1), (9, 3)], names=[0x50, 0x60, 0x70, 0x80, 0x90]),
        c('unsigned_ids', 1, [(1, 0), (0x80000001, 1), (0x90000000, 2)]),
        c('empty_table', 5, []),
        c('format_1', 7, five, format=1, names=names),
        c('format_3_flag_ignored', 5, five, format=3, flags=2, names=names),
        c('format_4_narrow', 9, five, format=4, flags=0xfd, names=names),
        c('format_4_wide', 9, five, format=4, flags=2, names=[0x400, 0, 0x480, 0x123456789a0, 0xffffffff00000040]),
        c('format_4_wide_zero', 3, five, format=4, flags=2, names=[0x400, 0, 0x480, 0x1, 0x2]),
        c('format_4_wide_high_only', 5, five, format=4, flags=2, names=[0x400, 0, 0x100000000 << 8, 0x1, 0x2]),
        c('format_255_wide', 7, five, format=0xff, flags=0xff, names=names),
        c('format_0', 1, five, format=0, names=names),
        c('slot_not_loaded', 5, five, loaded=0),
        c('slot_loaded_other_value', 5, five, loaded=0x80000000),
        c('slot_without_holder', 5, holder=False),
        c('large_table_found', 1491, big),
        c('large_table_missing', 1500, big),
        c('large_table_last', 2991, big),
        c('index_before_header', 3, [(1, 0), (3, 1), (5, 0)], backwards=True),
        c('size_not_aligned', 5, five, size=0x6d),
    ]


def at_cases(address, slot):
    def c(cid, row_number, *index_args, loaded=1, holder=True, **table_kw):
        if not index_args:
            index_args = ([(5, 0)],)
        buffers, memory = repository(slot, table_bytes(*index_args, **table_kw), loaded, holder)
        buffers['out'] = {'size': 0x18, 'bytes': 'cc' * 0x18}
        return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
                'args': {'rdi': 'buf:out', 'rsi': 0xaaaaaaaa00000000 | (row_number & 0xffffffff)},
                'buffers': buffers, 'memory': memory, 'stubs': [], 'imports': []}

    three = [(4, 0), (6, 1), (8, 2)]
    ids = [0x11, 0x80000022, 0xffffffff]
    offsets = [0x100, 0x80000000, 0xfffffff0]
    return [
        c('row_0', 0, three, ids=ids, offsets=offsets),
        c('row_1_high_bit_id', 1, three, ids=ids, offsets=offsets),
        c('row_last', 2, three, ids=ids, offsets=offsets),
        c('row_at_count', 3, three, ids=ids, offsets=offsets),
        c('row_far_past_count', 0x7fffffff, three, ids=ids, offsets=offsets),
        c('row_number_negative', -1, three, ids=ids, offsets=offsets),
        c('row_number_most_negative', -0x80000000, three, ids=ids, offsets=offsets),
        c('empty_table', 0, []),
        # The count is the index's; descriptors beyond it are not rows.
        c('descriptor_past_count', 2, [(4, 0), (6, 1)], ids=ids, offsets=offsets),
        c('format_1', 1, three, format=1, ids=ids, offsets=offsets),
        c('format_3_flag_ignored', 2, three, format=3, flags=2, ids=ids, offsets=offsets),
        c('format_4_narrow', 1, three, format=4, flags=0xfd, ids=ids, offsets=offsets),
        c('format_4_wide', 1, three, format=4, flags=2, ids=ids, offsets=[0x100, 0x123456789a0, 0x2]),
        c('format_5_wide_wraps', 2, three, format=5, flags=3, ids=ids, offsets=[0, 1, 0xffffffff00000040]),
        c('format_0', 0, three, format=0, ids=ids, offsets=offsets),
        c('large_table', 299, [(10 * k + 1, k) for k in range(300)]),
        c('large_table_past', 300, [(10 * k + 1, k) for k in range(300)]),
        c('slot_not_loaded', 0, three, loaded=0),
        c('slot_loaded_other_value', 1, three, loaded=2),
        c('slot_without_holder', 0, holder=False),
    ]


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, slot = FUNCTIONS[name]
    cases = {'name': name_cases, 'at': at_cases}[kind](address, slot)
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for case in cases:
        with open(os.path.join(target, f"{case['id']}.json"), 'w') as f:
            json.dump(case, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
