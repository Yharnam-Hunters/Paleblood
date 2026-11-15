#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the sound check in hearing.cpp (ai_hearing_check_sound, 0x0169cef0):
invented data, not game recordings.

usage: hearing_check_sound_cases.py OUT_DIR

The check finds who made a sound and hands it to the hearing test (0x0169d160), which is stubbed:
its listener, sound, audible flag and size are compared. The world's characters and objects
are invented tables behind the two globals; the AiSound row lookup, the maker's team
(0x01cc2910), the listener's team (a vtable slot pointing at a stubbed game function) and the
fatal error report are stubbed too. The table for most sounds is an invented buffer behind its
global, set at the entry a case asks about (and the opposite at the transposed entry). The two
attack-hit tables are part of the executable's own data, which a case cannot change: the cases
walk many team pairs with each flag so that both kinds of entry are met, and both sides read the
same table, so no values are recorded here.
"""
import json
from pathlib import Path
import random
import struct
import sys

CHECK_SOUND = '0x0169cef0'
HEARING_TEST, FIND_ROW, CHARACTER_TEAM, FATAL = '0x0169d160', '0x02312b60', '0x01cc2910', '0x024b55b0'
LISTENER_TEAM = '0x016a2750'          # any game function: the listener's vtable slot points here, stubbed
WORLD_CHARACTERS, WORLD_OBJECTS, HEARING_TABLE = '0x0593e878', '0x0593b168', '0x057e4560'
TEAMS = 32
CHARACTER, OBJECT, OTHER = 0, 1, 2
NO_HANDLE = 0xffffffff
NAN, NEGATIVE_ZERO, INFINITY = 0x7fc12345, 0x80000000, 0x7f800000


def f32(value):
    """A float rounded to float32, or an explicit bit pattern, as 4 bytes."""
    return struct.pack('<I', value & 0xffffffff) if isinstance(value, int) else struct.pack('<f', value)


def u32(value):
    return struct.pack('<I', value & 0xffffffff).hex()


def pattern(size, byte):
    return bytearray((byte + i * 7) & 0xff for i in range(size))


def character_handle(block, slot, high=0):
    return (high << 20) | (block << 14) | slot


def object_handle(group, slot, high=0):
    return (high << 28) | (group << 20) | slot


class Case:
    def __init__(self, cid, maker, handle, sound_id=7001):
        self.cid = cid
        self.buffers = {}
        self.memory = []
        self.stubs = []
        listener, owner, owner_vt = pattern(0x28, 0x51), pattern(0x40, 0x61), pattern(0x48, 0x67)
        sound = pattern(0x40, 0x81)
        sound[0x20:0x24] = struct.pack('<I', maker & 0xffffffff)
        sound[0x24:0x28] = struct.pack('<I', handle & 0xffffffff)
        sound[0x30:0x34] = struct.pack('<i', sound_id)
        self.sound = sound
        self.buffers.update(listener=listener, owner=owner, owner_vt=owner_vt, sound=sound)
        self.memory += [{'addr': 'buf:listener+8', 'pointer': 'owner'}, {'addr': 'buf:owner+0', 'pointer': 'owner_vt'},
                        {'addr': 'buf:owner_vt+16', 'guest': LISTENER_TEAM}]

    def buffer(self, name, data):
        self.buffers[name] = data
        return data

    def pointer(self, where, target):
        self.memory.append({'addr': where, 'pointer': target})

    def hears(self):
        """The hearing test is called (the stub compares its listener, sound, flag and size)."""
        self.stubs.append({'address': HEARING_TEST, 'argc': 3, 'argi': [0, 1], 'argu32': [2], 'argf32': [0]})

    def as_json(self):
        return {
            'schema': 1, 'address': CHECK_SOUND, 'id': self.cid, 'returns': 'void',
            'args': {'rdi': 'buf:listener', 'rsi': 'buf:sound'},
            'buffers': {n: {'size': len(b), 'bytes': bytes(b).hex()} for n, b in self.buffers.items()},
            'memory': self.memory, 'imports': [], 'stubs': self.stubs,
        }


def other_case(cid, maker=OTHER, handle=0x1234):
    c = Case(cid, maker, handle)
    if maker == OTHER:
        c.hears()
    return c


def object_case(cid, handle, groups=((4, True),), group_count=None, heard=None):
    """groups: (count, slots present) per group. heard: whether the hearing test is called (None:
    worked out from the handle)."""
    c = Case(cid, OBJECT, handle)
    table = c.buffer('objects', pattern(0x30, 0x91))
    table[0x20:0x24] = struct.pack('<i', len(groups) if group_count is None else group_count)
    c.buffer('groups', pattern(0x1f8 * max(len(groups), 1), 0x95))
    c.pointer(WORLD_OBJECTS, 'objects')
    c.pointer('buf:objects+40', 'groups')
    for g, (count, present) in enumerate(groups):
        c.memory.append({'addr': f'buf:groups+{0x1f8 * g + 0xc8:#x}', 'bytes': struct.pack('<i', count).hex()})
        if present:
            c.buffer(f'slots{g}', pattern(0x2e0 * max(count, 1), 0x99 + g))
            c.pointer(f'buf:groups+{0x1f8 * g + 0xd0:#x}', f'slots{g}')
        else:
            c.memory.append({'addr': f'buf:groups+{0x1f8 * g + 0xd0:#x}', 'bytes': bytes(8).hex()})
    if heard is None:
        group, slot = (handle >> 20) & 0xff, handle & 0xfffff
        limit = len(groups) if group_count is None else group_count
        heard = handle != NO_HANDLE and group < limit and group < len(groups) and slot < groups[group][0] and (
            groups[group][1] or slot != 0)
    if heard:
        c.hears()
    return c


def character_case(cid, handle, *, size=1.25, row=None, flags=(0, 0), teams=(3, 5), table=1,
                   blocks=((2, 2),), extra_blocks=None, block_null=(), character_null=False, slots_null=False,
                   manager=True):
    """A sound made by a character. blocks: (block index, slot count) present in the world; the
    character sits at every slot of every block. row: None (no AiSound row) or useHitDamageTeam's
    byte. flags: the sound's two attack-hit table flags. teams: (listener's, maker's). table: the
    entry of the table for most sounds."""
    c = Case(cid, CHARACTER, handle)
    c.sound[0x34], c.sound[0x35] = flags
    world = c.buffer('world', pattern(0xa50, 0xa1))
    world[0x850:0xa50] = bytes(0x200)
    limit = c.buffer('limit', pattern(0x20, 0xa5))
    limit[0x18:0x1c] = struct.pack('<i', (max(b for b, _ in blocks) - 1) if extra_blocks is None else extra_blocks)
    c.pointer('buf:world+8', 'limit')
    character = c.buffer('character', pattern(0x3b8, 0xb1))
    c.buffer('modules', pattern(0x70, 0xb5))
    link = c.buffer('link', pattern(0x360, 0xb9))
    link[0x358:0x35c] = f32(size)
    c.pointer('buf:character+944', 'modules')
    c.pointer('buf:modules+104', 'link')
    for index, count in blocks:
        if index in block_null:
            continue
        block = c.buffer(f'block{index}', pattern(0x10, 0xc1 + index))
        block[0:4] = struct.pack('<i', count)
        c.pointer(f'buf:world+{0x850 + 8 * index:#x}', f'block{index}')
        if slots_null:
            block[8:16] = bytes(8)
            continue
        c.buffer(f'slots{index}', pattern(0x38 * max(count, 1), 0xd1 + index))
        c.pointer(f'buf:block{index}+8', f'slots{index}')
        for slot in range(max(count, 0)):
            if character_null:
                c.memory.append({'addr': f'buf:slots{index}+{0x38 * slot:#x}', 'bytes': bytes(8).hex()})
            else:
                c.pointer(f'buf:slots{index}+{0x38 * slot:#x}', 'character')
    if manager:
        c.pointer(WORLD_CHARACTERS, 'world')
    else:
        c.memory.append({'addr': WORLD_CHARACTERS, 'bytes': bytes(8).hex()})
        c.stubs.append({'address': FATAL, 'argc': 4, 'argi': [0, 1, 2, 3]})
        return c

    block_index, slot = (handle >> 14) & 0x3f, handle & 0x3fff
    present = dict(blocks)
    limit_count = (max(b for b, _ in blocks) - 1 if extra_blocks is None else extra_blocks) + 2
    found = (handle != NO_HANDLE and block_index < limit_count and block_index in present
             and block_index not in block_null and slot < present[block_index] and not slots_null
             and not character_null)
    if not found:
        return c

    listener_team, maker_team = teams
    find = {'address': FIND_ROW, 'argc': 2, 'argu32': [1]}
    if row is not None:
        data = c.buffer('row', pattern(0x20, 0xe1))
        data[0xc] = row
        find['writes'] = [{'arg': 0, 'offset': 8, 'pointer': 'row'}]
    c.stubs.append(find)
    c.stubs.append({'address': LISTENER_TEAM, 'argc': 2, 'argi': [1],
                    'writes': [{'arg': 0, 'offset': 8, 'bytes': u32(listener_team)}]})
    c.stubs.append({'address': CHARACTER_TEAM, 'argc': 2, 'argi': [1],
                    'writes': [{'arg': 0, 'offset': 8, 'bytes': u32(maker_team)}]})
    hearing = c.buffer('hearing_table', pattern(TEAMS * TEAMS, 0xf1))
    hearing[maker_team * TEAMS + listener_team] = 0 if table else 1
    hearing[listener_team * TEAMS + maker_team] = table
    c.pointer(HEARING_TABLE, 'hearing_table')
    c.hears()
    return c


def named_cases():
    # Sounds of the third kind are always heard, with no size; other kinds are never heard.
    yield other_case('other_maker')
    yield other_case('other_maker_no_handle', handle=NO_HANDLE)
    for maker in (3, 7, -1, -2, 0x80000000, 0x102):
        yield other_case(f'unknown_maker_{maker & 0xffffffff:x}', maker=maker)

    # Objects: heard while the slot the handle names exists.
    yield object_case('object_found', object_handle(0, 1))
    yield object_case('object_last_slot', object_handle(0, 3))
    yield object_case('object_slot_past_count', object_handle(0, 4))
    yield object_case('object_no_handle', NO_HANDLE, groups=[(4, True)] * 16, group_count=0x100)
    yield object_case('object_second_group', object_handle(1, 2), groups=((1, True), (3, True)))
    yield object_case('object_group_past_count', object_handle(1, 0), groups=((4, True), (4, True)), group_count=1)
    yield object_case('object_negative_group_count', object_handle(0, 0), group_count=-1)
    yield object_case('object_zero_count', object_handle(0, 0), groups=((0, True),))
    yield object_case('object_negative_count', object_handle(0, 0), groups=((-5, True),))
    yield object_case('object_null_slots_first', object_handle(0, 0), groups=((4, False),))
    yield object_case('object_null_slots_second', object_handle(0, 1), groups=((4, False),))
    yield object_case('object_high_bits_ignored', object_handle(0, 2, high=0xf))
    yield object_case('object_large_slot', object_handle(0, 0x80002), groups=((4, True),))
    # All eight group bits count: group 16 is past one group's count, not group 0.
    yield object_case('object_group_bit_4', object_handle(0x10, 0))
    yield object_case('object_group_bit_7', object_handle(0x80, 1))

    # Characters: found by block and slot, else nothing.
    h = character_handle
    yield character_case('character_found_table_hears', h(2, 1))
    yield character_case('character_found_table_silent', h(2, 1), table=0)
    yield character_case('character_table_any_nonzero', h(2, 0), table=0x80)
    yield character_case('character_no_handle', NO_HANDLE)
    yield character_case('character_slot_past_count', h(2, 2))
    yield character_case('character_block_at_limit', h(3, 0), blocks=((2, 2), (3, 1)), extra_blocks=1)
    yield character_case('character_block_past_limit', h(3, 0), blocks=((2, 2), (3, 1)), extra_blocks=0)
    yield character_case('character_negative_limit', h(0, 0), blocks=((0, 1),), extra_blocks=-2)
    yield character_case('character_limit_one', h(0, 0), blocks=((0, 1),), extra_blocks=-1)
    yield character_case('character_block_null', h(2, 0), blocks=((2, 2),), block_null=(2,))
    yield character_case('character_null', h(2, 1), character_null=True)
    yield character_case('character_null_slots_first', h(2, 0), slots_null=True)
    yield character_case('character_zero_count', h(2, 0), blocks=((2, 0),))
    yield character_case('character_negative_count', h(2, 0), blocks=((2, -1),))
    yield character_case('character_last_block', h(63, 0), blocks=((63, 1),))
    yield character_case('character_high_bits_ignored', h(2, 1, high=0xfff))
    yield character_case('character_slot_bits', h(2, 0x3fff), blocks=((2, 2),))
    # The character manager is required: reported when missing, then read again (a missing one
    # with a handle would be read through null on both sides; a stub cannot set the global).
    yield character_case('character_no_manager_no_handle', NO_HANDLE, manager=False)
    # The maker's size goes to the hearing test as it is.
    for name, size in (('zero', 0.0), ('negative_zero', NEGATIVE_ZERO), ('nan', NAN), ('infinity', INFINITY),
                       ('negative', -3.5), ('large', 1e30)):
        yield character_case(f'character_size_{name}', h(2, 1), size=size)
    # The AiSound row: without one, or with useHitDamageTeam off, the table for most sounds decides.
    yield character_case('row_without_hit_team', h(2, 1), row=0, table=1)
    yield character_case('row_without_hit_team_silent', h(2, 1), row=0, table=0, flags=(1, 1))
    yield character_case('no_row_ignores_flags', h(2, 1), row=None, table=0, flags=(1, 1))
    # Teams index the table for most sounds by [listener][maker].
    for teams in ((0, 0), (0, 31), (31, 0), (31, 31), (5, 3), (17, 9)):
        for table in (0, 1):
            yield character_case(f'teams_{teams[0]}_{teams[1]}_table_{table}', h(2, 1), teams=teams, table=table)
    # With useHitDamageTeam each attack-hit table counts only when the sound asks for it: every
    # listener's team against a spread of makers' teams, with each flag pair.
    for flags in ((0, 0), (1, 0), (0, 1), (1, 1), (0x80, 0x02)):
        for listener_team in range(TEAMS):
            for maker_team in range(listener_team % 4, TEAMS, 4):
                yield character_case(f'hit_teams_{flags[0]:x}{flags[1]:x}_{listener_team}_{maker_team}', h(2, 1),
                                     row=1 if flags != (0x80, 0x02) else 0x7f, flags=flags,
                                     teams=(listener_team, maker_team), table=listener_team & 1)


def random_cases(count=300, seed=0x169cef0):
    rng = random.Random(seed)
    for i in range(count):
        kind = rng.choice((CHARACTER, CHARACTER, CHARACTER, OBJECT, OTHER, 3))
        if kind == OBJECT:
            groups = [(rng.randint(-1, 5), rng.random() < 0.85) for _ in range(rng.randint(1, 3))]
            handle = NO_HANDLE if rng.random() < 0.1 else object_handle(rng.randint(0, 3), rng.randint(0, 6),
                                                                         high=rng.randint(0, 15))
            yield object_case(f'random_{i:03d}', handle, groups=groups,
                              group_count=rng.choice((None, None, len(groups) - 1, -1)))
        elif kind == CHARACTER:
            blocks = sorted({rng.randint(0, 6): None for _ in range(rng.randint(1, 3))})
            blocks = [(b, rng.randint(-1, 4)) for b in blocks]
            handle = NO_HANDLE if rng.random() < 0.08 else character_handle(rng.randint(0, 7), rng.randint(0, 5),
                                                                             high=rng.randint(0, 0xfff))
            row = rng.choice((None, 0, 1, 0x80))
            yield character_case(f'random_{i:03d}', handle, size=rng.uniform(-2.0, 8.0), row=row,
                                 flags=(rng.choice((0, 1, 0x40)), rng.choice((0, 1, 0x40))),
                                 teams=(rng.randrange(TEAMS), rng.randrange(TEAMS)), table=rng.choice((0, 1, 0xff)),
                                 blocks=blocks,
                                 extra_blocks=rng.choice((None, None, 2, 5, -1)),
                                 character_null=rng.random() < 0.05)
        else:
            yield other_case(f'random_{i:03d}', maker=kind, handle=rng.getrandbits(32))


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    directory = Path(sys.argv[1]) / CHECK_SOUND
    directory.mkdir(parents=True, exist_ok=True)
    count = 0
    for generate in (named_cases, random_cases):
        for c in generate():
            (directory / f'{c.cid}.json').write_text(json.dumps(c.as_json(), indent=1) + '\n')
            count += 1
    print(f'{count} cases')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
