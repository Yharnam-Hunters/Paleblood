#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the solo parameter repository's per-type row lookups
(game/param/find_row.cpp), for tools/verify.py run. Each lookup has its own generator,
find_row_<type>_cases.py, which calls write() here with its function's name.

Every case builds a repository (the singleton at 0x05940340) whose slots all hold a decoy table,
except the slot of the lookup's own type, which holds the case's table. A PARAM table is laid out
as the game keeps it: its size word 16 bytes before the header, the header, the row descriptors
in the case's format, and the loader's (id, row number) index at the size rounded up to 16 bytes
from the header (its data version byte varies with the format). Rows are never read, only their addresses computed, so the data offsets are
arbitrary values. The output starts filled with 0xcc, so the four bytes the lookup leaves alone
are checked too. All ids and rows are invented; no game data is used.
"""
import json
import os
import struct
import sys

REPOSITORY = '0x05940340'
FATAL_ERROR = '0x024b55b0'
SLOTS, SLOT_SIZE, SLOT_COUNT = 0x70, 0x48, 62
# The result is 16 bytes; a few lookups add a data version byte or two words after it.
OUT_SIZE = 0x18
# The NpcThinkParam lookup's two extra 32-bit words (upper halves set, to be ignored); the other
# lookups take no third and fourth argument and must leave these registers unread.
WORD_0, WORD_1 = 0xaaaaaaaa11112222, 0x55555555fedcba98
PREFIX = 0x40  # room before the header for the size word (and a backwards index)

# name: (address, table type index in the repository)
LOOKUPS = {
    'param_find_row_equip_param_accessory': ('0x02311c80', 2),
    'param_find_row_ai_sound': ('0x02312b60', 48),
    'param_find_row_bullet': ('0x02314860', 11),
    'param_find_row_calc_correct_graph': ('0x02314b60', 32),
    'param_find_row_chara_init': ('0x02314e20', 21),
    'param_find_row_decal': ('0x023155c0', 38),
    'param_find_row_dungeon_sub_feat_lot': ('0x02316c70', 54),
    'param_find_row_face': ('0x023192d0', 24),
    'param_find_row_face_range': ('0x0231af40', 25),
    'param_find_row_gem_category': ('0x0231c1e0', 44),
    'param_find_row_gem_drop_doping': ('0x0231ca40', 45),
    'param_find_row_gem_drop_modify': ('0x0231cd30', 46),
    'param_find_row_gem_effect': ('0x0231d0f0', 43),
    'param_find_row_gem_gen': ('0x0231d630', 42),
    'param_find_row_gem_gen_2': ('0x0231de40', 42),
    'param_find_row_equip_param_goods': ('0x0231e3d0', 3),
    'param_find_row_hit_mtrl': ('0x0231f460', 35),
    'param_find_row_holygrail_ex': ('0x0231f720', 52),
    'param_find_row_knock_back': ('0x02320dd0', 36),
    'param_find_row_magic': ('0x023210e0', 14),
    'param_find_row_menu_color_table': ('0x02321ac0', 18),
    'param_find_row_move': ('0x02321ea0', 20),
    'param_find_row_equip_mtrl_set': ('0x02322260', 22),
    'param_find_row_npc_think': ('0x02322b40', 9),  # also stores two words beside the result
    'param_find_row_object': ('0x02323290', 10),
    'param_find_row_reinforce_param_protector': ('0x02326200', 5),
    'param_find_row_ragdoll': ('0x023264c0', 26),
    'param_find_row_ritual_required_mat': ('0x02327120', 55),
    'param_find_row_shop_lineup': ('0x02327450', 27),
    'param_find_row_skeleton': ('0x02327880', 31),
    'param_find_row_sp_effect': ('0x02328aa0', 15),  # also returns the data version
    'param_find_row_obj_act': ('0x02328f90', 34),
    'param_find_row_talk': ('0x02329250', 17),  # also returns the data version
    'param_find_row_weapon_gen': ('0x02329510', 40),
    'param_find_row_reinforce_param_weapon': ('0x0232c930', 4),
    'param_find_row_action_button': ('0x023128a0', 39),
    'param_find_row_return_point': ('0x02326b40', 50),
    'param_find_row_item_lot': ('0x02320640', 19),  # only from data version 2 on
    'param_find_row_dungeon_feature': ('0x023167f0', 53),  # publishes header word 0x06
    'param_find_row_npc': ('0x023226c0', 6),  # returns two header words, checks the version
}
DUNGEON_FEATURE_WORD = '0x05992cb0'


def u32(v):
    return struct.pack('<I', v & 0xffffffff)


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff)


def table_bytes(index, format=2, flags=0, offsets=None, size=None, backwards=False, past_end=(),
                version=None, word_06=0x6e06, names=None, ids=None):
    """index: (id, row number) pairs in index order; offsets: each row's data offset (one row per
    index entry by default); past_end: index entries after the counted ones, which the search must
    never reach; names, ids: each row descriptor's name offset and id (invented values by default).
    Returns (buffer bytes, header offset in the buffer)."""
    rows = len(offsets) if offsets is not None else len(index)
    if offsets is None:
        offsets = [0x1000 + 0x40 * k for k in range(rows)]
    wide = format >= 4 and flags & 2
    start = 0x30 if format == 2 else 0x40
    header = bytearray(start)
    header[0xa:0xc] = struct.pack('<H', len(index))
    # The data version varies with the format unless a case sets it.
    header[0x6:0x8] = struct.pack('<H', word_06)
    header[0x8:0xa] = struct.pack('<H', 0x3a5c + format if version is None else version)
    header[0x2d] = format & 0xff
    header[0x2e] = flags & 0xff
    if names is None:
        names = [0x9000 + k for k in range(rows)]
    if ids is None:
        ids = [0x7000 + k for k in range(rows)]
    descriptors = b''
    for k, off in enumerate(offsets):
        if wide:
            descriptors += u32(ids[k]) + u32(0xdddddddd) + u64(off) + u64(names[k])
        else:
            descriptors += u32(ids[k]) + u32(off) + u32(names[k])
    body = bytes(header) + descriptors
    pairs = b''.join(u32(i) + u32(r) for i, r in list(index) + list(past_end))
    if backwards:
        # The index sits before the header: a negative rounded size, and the size word after it.
        assert len(pairs) <= 0x20
        size_word = (-0x30) & 0xffffffff
        prefix = bytearray(PREFIX)
        prefix[PREFIX - 0x30:PREFIX - 0x30 + len(pairs)] = pairs
        prefix[PREFIX - 0x10:PREFIX - 0xc] = u32(size_word)
        return bytes(prefix) + body, PREFIX
    if size is None:
        size = len(body)
    rounded = (size + 15) & ~15
    prefix = bytearray(PREFIX)
    prefix[PREFIX - 0x10:PREFIX - 0xc] = u32(size)
    out = bytearray(body.ljust(rounded, b'\xee'))
    if rounded < len(out):
        raise ValueError('index would overlap the descriptors')
    out[rounded:rounded + len(pairs)] = pairs
    return bytes(prefix) + bytes(out), PREFIX


def decoy_table():
    # The same ids as the cases use, other rows: a lookup in the wrong slot answers differently.
    ids = [0, 1, 3, 5, 7, 9, 10, 20, 100, 1000, 0x7fffffff]
    return table_bytes([(i, k) for k, i in enumerate(ids)], format=2,
                       offsets=[0x55550000 + 0x10 * k for k in range(len(ids))])


def build(address, slot, cid, id, table=None, loaded=1, holder=True, repository=True):
    buffers = {'out': {'size': OUT_SIZE, 'bytes': 'cc' * OUT_SIZE}}
    memory = []
    stubs = []
    if repository:
        decoy, decoy_header = decoy_table()
        buffers['repo'] = {'size': SLOTS + SLOT_SIZE * SLOT_COUNT}
        buffers['decoy_holder'] = {'size': 0x78}
        buffers['decoy_file'] = {'size': 0x78}
        buffers['decoy_table'] = {'size': len(decoy), 'bytes': decoy.hex()}
        memory.append({'addr': REPOSITORY, 'pointer': 'repo'})
        memory.append({'addr': 'buf:decoy_holder+112', 'pointer': 'decoy_file'})
        memory.append({'addr': f'buf:decoy_file+112', 'pointer': f'decoy_table+{decoy_header}'})
        for s in range(SLOT_COUNT):
            at = SLOTS + SLOT_SIZE * s
            if s == slot:
                memory.append({'addr': f'buf:repo+{at}', 'bytes': u32(loaded).hex()})
                if holder:
                    memory.append({'addr': f'buf:repo+{at + 8}', 'pointer': 'holder'})
            else:
                memory.append({'addr': f'buf:repo+{at}', 'bytes': u32(1).hex()})
                memory.append({'addr': f'buf:repo+{at + 8}', 'pointer': 'decoy_holder'})
        if holder:
            data, header = table if table is not None else table_bytes([(5, 0)])
            buffers['holder'] = {'size': 0x78}
            buffers['file'] = {'size': 0x78}
            buffers['table'] = {'size': len(data), 'bytes': data.hex()}
            memory.append({'addr': 'buf:holder+112', 'pointer': 'file'})
            memory.append({'addr': 'buf:file+112', 'pointer': f'table+{header}'})
    else:
        memory.append({'addr': REPOSITORY, 'bytes': u64(0).hex()})
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:out', 'rsi': id & 0xffffffff, 'rdx': WORD_0, 'rcx': WORD_1},
            'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': []}


def cases(address, slot):
    def c(cid, id, *index_args, **kw):
        table_kw = {k: kw.pop(k) for k in ('format', 'flags', 'offsets', 'size', 'backwards', 'past_end', 'version', 'word_06') if k in kw}
        table = table_bytes(*index_args, **table_kw) if index_args else None
        return build(address, slot, cid, id, table, **kw)

    sorted_ids = [(1, 0), (3, 1), (5, 2), (7, 3), (9, 4)]
    big = [(10 * k + 1, k) for k in range(300)]
    out = [
        c('found_first', 1, sorted_ids),
        c('found_middle', 5, sorted_ids),
        c('found_last', 9, sorted_ids),
        c('missing_between', 4, sorted_ids),
        c('missing_below', 0, sorted_ids),
        c('missing_above', 10, sorted_ids),
        c('empty_table', 5, []),
        c('single_row_found', 5, [(5, 0)]),
        c('single_row_missing', 6, [(5, 0)]),
        c('id_zero', 0, [(0, 0), (2, 1)]),
        c('id_largest', 0x7fffffff, [(3, 0), (0x7fffffff, 1)]),
        c('format_1_uses_late_header', 7, sorted_ids, format=1),
        c('format_3', 7, sorted_ids, format=3),
        c('format_3_flag_ignored', 7, sorted_ids, format=3, flags=2),
        c('format_4_narrow', 7, sorted_ids, format=4),
        c('format_4_other_flags', 7, sorted_ids, format=4, flags=0xfd),
        c('format_4_wide', 7, sorted_ids, format=4, flags=2),
        c('format_5_wide', 3, sorted_ids, format=5, flags=0x03),
        c('format_255_wide', 9, sorted_ids, format=0xff, flags=0xff),
        c('format_0', 3, sorted_ids, format=0),
        c('narrow_offset_high_bit', 3, [(3, 0)], offsets=[0x80000010]),
        c('narrow_offset_all_ones', 3, [(3, 0)], offsets=[0xffffffff]),
        c('narrow_offset_high_bit_format_3', 3, [(3, 0)], format=3, offsets=[0x80000010]),
        c('narrow_offset_high_bit_format_4', 3, [(3, 0)], format=4, flags=0xfd, offsets=[0xfffffff0]),
        c('entry_past_count', 9, [(1, 0), (3, 1)], past_end=[(9, 0)]),
        c('entry_past_count_below', 0, [(1, 0), (3, 1)], past_end=[(0, 1)]),
        c('wide_offset_wraps', 3, [(3, 0)], format=4, flags=2, offsets=[0xffffffff00000040]),
        c('wide_offset_large', 3, [(3, 0)], format=4, flags=2, offsets=[0x123456789a0]),
        c('row_number_negative', 5, [(5, -1)]),
        c('row_number_not_in_order', 7, [(1, 4), (3, 0), (7, 2), (8, 1), (9, 3)]),
        # Index ids compare unsigned: a signed comparison would search the wrong half.
        c('unsigned_ids_left', 1, [(1, 0), (0x80000001, 1), (0x90000000, 2)]),
        c('unsigned_ids_right', 0x7ffffff0, [(1, 0), (0x7ffffff0, 1), (0x80000001, 2), (0xfffffff0, 0)]),
        c('duplicate_ids', 5, [(5, 0), (5, 1), (5, 2)]),
        # Which of equal ids is found depends on where the search splits.
        c('duplicate_ids_even', 5, [(5, 0), (5, 1), (5, 2), (5, 3)]),
        c('size_not_aligned', 5, sorted_ids, size=0x6d),
        c('size_padded', 5, sorted_ids, size=0x80),
        c('index_before_header', 3, [(1, 0), (3, 1), (5, 0)], backwards=True),
        c('slot_not_loaded', 5, sorted_ids, loaded=0),
        c('slot_loaded_other_value', 5, sorted_ids, loaded=0x100),
        c('slot_without_holder', 5, holder=False),
        c('negative_id', -1, sorted_ids),
        c('most_negative_id', -0x80000000, sorted_ids),
        # A negative id returns before the repository is read, so a missing one is fine.
        build(address, slot, 'negative_id_no_repository', -5, repository=False),
    ]
    for key in (1, 11, 1491, 2991, 1500, 2995, 1371):
        out.append(c(f'large_table_{key}', key, big))
    if address == LOOKUPS['param_find_row_item_lot'][0]:
        out += [
            c('version_0', 5, sorted_ids, version=0),
            c('version_1', 5, sorted_ids, version=1),
            c('version_2', 5, sorted_ids, version=2),
            c('version_256', 5, sorted_ids, version=0x100),  # compared as 16 bits
            c('version_65535', 5, sorted_ids, version=0xffff),
        ]
    for case in out:
        # The DungeonFeatureParam lookup's published word starts as a known value.
        case['memory'].append({'addr': DUNGEON_FEATURE_WORD, 'bytes': u32(0x13572468).hex()})
    if address == LOOKUPS['param_find_row_npc'][0]:
        # Versions 2 and older are reported (the report is stubbed) and searched anyway.
        old = [{'address': FATAL_ERROR, 'ret': 0, 'argc': 3}]
        for cid, id, version, stubs in (('version_0', 5, 0, old), ('version_2', 7, 2, old),
                                        ('version_2_missing', 4, 2, old), ('version_3', 5, 3, []),
                                        ('version_258', 5, 0x102, []), ('version_65535', 9, 0xffff, [])):
            case = c(cid, id, sorted_ids, version=version, word_06=0x8001)
            case['stubs'] = stubs
            out.append(case)
        out.append(c('word_06_zero', 3, sorted_ids, word_06=0))
    if address == LOOKUPS['param_find_row_dungeon_feature'][0]:
        out += [
            c('word_06_zero', 4, sorted_ids, word_06=0),
            c('word_06_high', 5, sorted_ids, word_06=0xffff),
        ]
    return out


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, slot = LOOKUPS[name]
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    all_cases = cases(address, slot)
    for case in all_cases:
        with open(os.path.join(target, f"{case['id']}.json"), 'w') as f:
            json.dump(case, f, indent=1)
    print(f'{len(all_cases)} cases in {target}')
    return 0
