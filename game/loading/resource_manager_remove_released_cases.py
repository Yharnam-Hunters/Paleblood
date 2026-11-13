#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the resource manager's removal of a released info
(game/loading/resource_manager_release.cpp), for tools/verify.py run.

usage: resource_manager_remove_released_cases.py OUT_DIR

Each case gives a manager (passed at an address that may not be 8-aligned; its lock at +0x648 with
scripted lock and unlock slots), its slot array (stored at +0xdb8 aligned up to 8: 0x60-byte slots
holding the info or a decoy, in use or not) and its free slot array (at +0xde8 aligned up to 8:
full or not, or all null), and an info whose flags (+0x31) the case sets. The name's assign
(0x02a2a720), the free slot array's growth (0x024c3160) and the info's unload (0x024bdb80) are
scripted stubs; the growth leaves the array as it was, so the free slot buffer has room past its
capacity for the appended slot.
"""
import json
import os
import struct
import sys

ADDRESS = '0x024c3260'
ASSIGN, GROW, UNLOAD = '0x02a2a720', '0x024c3160', '0x024bdb80'
LOCK, UNLOCK = '0x00500200', '0x00500300'
MANAGER, LOCK_OFFSET, SLOTS, FREE_SLOTS = 0xe20, 0x648, 0xdb8, 0xde8
INFO, FLAGS = 0x98, 0x31
SLOT, SLOT_INFO, SLOT_FLAGS, SLOT_NAME = 0x60, 0x18, 0x20, 0x28
EMPTY_TEXT = '0x04dd85f0'


def align8(value):
    return (value + 7) & ~7


def case(cid, info_flags=0xc0, slots=(), target=None, free_count=0, free_capacity=4, free_null=False, skew=0):
    """slots: per slot, its flags byte; target: the index of the slot holding the info (the others
    hold a decoy), or None."""
    buffers = {'manager': bytearray([0x4d]) * (MANAGER + skew), 'info': bytearray([0x49]) * INFO,
               'decoy': bytearray(8), 'lock_vtable': bytearray(0x30),
               'slots': bytearray([0x53]) * (SLOT * max(1, len(slots))),
               'free': bytearray([0x46]) * (8 * (free_capacity + 1))}
    buffers['info'][FLAGS] = info_flags
    memory = [{'addr': f'buf:manager+{skew + LOCK_OFFSET}', 'pointer': 'lock_vtable+0'},
              {'addr': 'buf:lock_vtable+24', 'guest': LOCK},
              {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]
    slot_array = align8(skew + SLOTS)
    memory += [{'addr': f'buf:manager+{slot_array + 8}', 'pointer': 'slots+0'},
               {'addr': f'buf:manager+{slot_array + 16}', 'pointer': f'slots+{SLOT * len(slots)}'}]
    for k, flags in enumerate(slots):
        memory.append({'addr': f'buf:slots+{SLOT * k + SLOT_INFO}', 'pointer': 'info+0' if k == target else 'decoy+0'})
        buffers['slots'][SLOT * k + SLOT_FLAGS] = flags
    free_array = align8(skew + FREE_SLOTS)
    for word, offset in ((1, 0), (2, 8 * free_count), (3, 8 * free_capacity)):
        place = f'buf:manager+{free_array + 8 * word}'
        if free_null:
            memory.append({'addr': place, 'bytes': '00' * 8})
        else:
            memory.append({'addr': place, 'pointer': f'free+{offset}'})
    stubs = []
    flagged = (info_flags & 0xc0) == 0xc0
    if flagged:
        stubs.append({'address': LOCK, 'argc': 1, 'argu32': [1]})
        if target is not None and slots[target] & 1:
            stubs.append({'address': ASSIGN, 'argc': 3})
            if free_null or free_count == free_capacity:
                stubs.append({'address': GROW, 'argc': 2})
            stubs.append({'address': UNLOAD, 'argc': 1})
        stubs.append({'address': UNLOCK, 'argc': 1})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i8',
            'args': {'rdi': f'buf:manager+{skew}' if skew else 'buf:manager', 'rsi': 'buf:info'},
            'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in buffers.items()},
            'memory': memory, 'imports': [], 'stubs': stubs}


def cases():
    out = [case(f'flags_{flags:02x}', info_flags=flags, slots=(1,), target=0) for flags in (0, 0x40, 0x80, 0x3f, 0xbf, 0x7f)]
    out += [
        case('no_slots', slots=()),
        case('not_found', slots=(1, 1, 1)),
        case('first_in_use', slots=(1, 1, 1), target=0),
        case('last_in_use', slots=(1, 0, 1), target=2),
        case('middle_not_in_use', slots=(1, 0, 1), target=1),
        case('flags_ff_slot_fe', info_flags=0xff, slots=(0xfe,), target=0),
        case('flags_ff_slot_ff', info_flags=0xff, slots=(0xff,), target=0),
        case('free_full_grows', slots=(1, 1), target=1, free_count=4, free_capacity=4),
        case('free_full_of_one', slots=(1,), target=0, free_count=1, free_capacity=1),
        case('free_full_of_nine', slots=(1,), target=0, free_count=9, free_capacity=9),
        case('free_empty_room', slots=(1,), target=0, free_count=0, free_capacity=2),
        case('free_null_grows', slots=(1,), target=0, free_null=True),
        case('skew_3', slots=(0, 1), target=1, free_count=2, skew=3),
        case('skew_5_full', slots=(1,), target=0, free_count=4, skew=5),
        case('skew_1_not_found', slots=(1,), skew=1),
    ]
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases()
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
