#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the item handles (game/character/gaitem_handle.cpp), for tools/verify.py run.

usage: gaitem_handle_cases.py OUT_DIR copy_a|copy_b|release|move|resolve

- copy_a / copy_b / release / move: handles zero, unmanaged (bit 23 clear), without a slot
  (index 0xffff, also 0xffffff and all ones), naming a live slot, naming a slot that holds
  another handle (stale), slot indices 0, 1, 0xfff, 0x1000 and 0xfffe (the index is not checked
  against the slot table); reference counts 0, 1, 2, 3, negative and the largest; a missing item
  manager singleton (reported through the scripted fatal error stub, then the handle is unmanaged).
  The last release calls the item free (0x01e86e60), a scripted stub compared with the manager and
  the handle. move also covers equal handles, a zero source and a zero target.
- resolve: the three manager checks (index table missing, busy, not ready: each reported, then
  resolved anyway), unmanaged and zero handles, indices 0, 0xffe, 0xfff, 0x1000, 0xffff and the
  index table's entries 0, 0xfff, 0x1000, -1 and large negatives.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'copy_a': '0x01e8bae0', 'copy_b': '0x01e8bb70', 'release': '0x01e8bc00',
             'move': '0x01e8bcb0', 'resolve': '0x01e88da0'}
RETURNS = {'copy_a': 'void', 'copy_b': 'void', 'release': 'void', 'move': 'i64', 'resolve': 'i64'}
MANAGER_GLOBAL = '0x0593e990'
FATAL_ERROR = '0x024b55b0'
FREE_ITEM = '0x01e86e60'
SLOTS = 0x8008
INDEX_TABLE, BUSY, READY = 0x14028, 0x14030, 0x14031
MANAGED = 0x800000
# Large enough for slot index 0xfffe (the handle code does not bound the index).
MANAGER_SIZE = SLOTS + 0x10000 * 8 + 0x10


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def slot_entry(index, handle, references):
    return {'addr': f'buf:manager+{SLOTS + index * 8:#x}', 'bytes': struct.pack('<Ii', handle, references).hex()}


def managed(index, high=0x12):
    return (high << 24) | MANAGED | (0x40 << 16) | index


def with_manager(c, slots, present=True):
    c['buffers']['manager'] = {'size': MANAGER_SIZE}
    if present:
        c['memory'].append({'addr': MANAGER_GLOBAL, 'pointer': 'manager'})
    else:
        c['memory'].append({'addr': MANAGER_GLOBAL, 'bytes': '00' * 8})
        c['stubs'].append({'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 2, 3], 'argu32': [1]})
    for index, handle, references in slots:
        c['memory'].append(slot_entry(index, handle, references))


def frees(c, handle):
    c['stubs'].append({'address': FREE_ITEM, 'argc': 2, 'argi': [0], 'argu32': [1]})


# (case id, handle, slots, whether the handle's slot is live with that many references)
def handle_situations():
    out = [('zero', 0, [], None), ('unmanaged', 0x00123456, [(0x3456, 0x00123456, 5)], None),
           ('no_slot_index', managed(0xffff), [], None), ('no_slot_id_ffffff', 0x00ffffff, [], None),
           ('no_slot_all_ones', 0xffffffff, [], None), ('managed_bit_only', MANAGED, [(0, MANAGED, 1)], 1)]
    for index in (0, 1, 0x7a, 0xfff, 0x1000, 0xfffe):
        for refs in (0, 1, 2, 3, -1, 0x7fffffff, -0x80000000):
            h = managed(index)
            out.append((f'index_{index:04x}_refs_{refs & 0xffffffff:08x}', h, [(index, h, refs)], refs))
    h = managed(0x22)
    out.append(('stale_slot', h, [(0x22, h ^ 0x01000000, 4)], None))
    out.append(('stale_slot_unmanaged_copy', h, [(0x22, h & ~MANAGED, 4)], None))
    out.append(('empty_slot', h, [], None))
    return out


def copy_cases(variant):
    out = []
    for cid, handle, slots, _ in handle_situations():
        c = base(variant, cid)
        with_manager(c, slots)
        c['buffers']['source'] = {'size': 8, 'bytes': struct.pack('<II', handle, 0x9d9d9d9d).hex()}
        c['buffers']['target'] = {'size': 8, 'bytes': 'c4' * 8}
        c['args'] = {'rdi': 'buf:target', 'rsi': 'buf:source'}
        out.append(c)
    for cid, handle in (('missing_manager_unmanaged', 0x00123456), ('missing_manager_no_slot', managed(0xffff)),
                        ('missing_manager_zero', 0)):
        c = base(variant, cid)
        with_manager(c, [], present=False)
        c['buffers']['source'] = {'size': 4, 'bytes': struct.pack('<I', handle).hex()}
        c['buffers']['target'] = {'size': 4, 'bytes': 'c4' * 4}
        c['args'] = {'rdi': 'buf:target', 'rsi': 'buf:source'}
        out.append(c)
    return out


def release_cases():
    out = []
    for cid, handle, slots, refs in handle_situations():
        c = base('release', cid)
        with_manager(c, slots)
        c['buffers']['handle'] = {'size': 4, 'bytes': struct.pack('<I', handle).hex()}
        c['args'] = {'rdi': 'buf:handle'}
        if refs is not None and refs < 2:
            frees(c, handle)
        out.append(c)
    for cid, handle in (('missing_manager_unmanaged', 0x00123456), ('missing_manager_no_slot', managed(0xffff)),
                        ('missing_manager_zero', 0)):
        c = base('release', cid)
        with_manager(c, [], present=False)
        c['buffers']['handle'] = {'size': 4, 'bytes': struct.pack('<I', handle).hex()}
        c['args'] = {'rdi': 'buf:handle'}
        out.append(c)
    return out


def move_case(cid, target, source, slots, target_refs, present=True):
    c = base('move', cid)
    with_manager(c, slots, present)
    c['buffers']['handles'] = {'size': 0x10, 'bytes': struct.pack('<IIII', target, 0x5e5e5e5e, source, 0x6f6f6f6f).hex()}
    c['args'] = {'rdi': 'buf:handles', 'rsi': 'buf:handles+0x8'}
    if target_refs is not None and target_refs < 2 and target != source and target != 0:
        frees(c, target)
    return c


def move_cases():
    other = managed(0x99, 0x34)
    out = []
    for cid, handle, slots, refs in handle_situations():
        out.append(move_case(f'target_{cid}', handle, other, slots + [(0x99, other, 3)], refs))
    h = managed(0x10)
    out += [move_case('equal_handles', h, h, [(0x10, h, 1)], 1),
            move_case('equal_zero', 0, 0, [], None),
            move_case('zero_source', h, 0, [(0x10, h, 2)], 2),
            move_case('zero_source_last_reference', h, 0, [(0x10, h, 1)], 1),
            move_case('zero_target', 0, h, [(0x10, h, 1)], None),
            move_case('same_slot_other_handle', h, h ^ 0x01000000, [(0x10, h, 1)], 1),
            move_case('missing_manager_unmanaged_target', 0x00123456, h, [], None, present=False)]
    return out


def resolve_case(cid, handle, index_entries=(), table=True, busy=0, ready=1, slots=()):
    c = base('resolve', cid)
    c['buffers']['manager'] = {'size': INDEX_TABLE + 0x10}
    c['buffers']['table'] = {'size': 0x1001 * 4, 'bytes': ''}
    c['buffers']['handle'] = {'size': 4, 'bytes': struct.pack('<I', handle).hex()}
    c['buffers']['out'] = {'size': 4, 'bytes': 'e1' * 4}
    c['memory'] = [{'addr': f'buf:manager+{INDEX_TABLE:#x}', 'pointer': 'table'} if table else
                   {'addr': f'buf:manager+{INDEX_TABLE:#x}', 'bytes': '00' * 8},
                   {'addr': f'buf:manager+{BUSY:#x}', 'bytes': bytes([busy, ready]).hex()}]
    for index, value in index_entries:
        c['memory'].append({'addr': f'buf:table+{index * 4:#x}', 'bytes': struct.pack('<i', value).hex()})
    # Sentinels just inside and just past the slot table, which no in-range lookup may return.
    for slot, value in [(0, 0x0badf00d), (0x1000, 0x0deadbee)] + list(slots):
        c['memory'].append({'addr': f'buf:manager+{SLOTS + slot * 8:#x}', 'bytes': struct.pack('<II', value, 7).hex()})
    reports = (0 if table else 1) + (1 if busy else 0) + (0 if ready else 1)
    c['stubs'] = [{'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 2], 'argu32': [1]}] * reports
    c['args'] = {'rdi': 'buf:out', 'rsi': 'buf:manager', 'rdx': 'buf:handle'}
    return c


def resolve_cases():
    h = managed(0x123)
    out = [resolve_case('zero', 0), resolve_case('unmanaged', 0x00654321),
           resolve_case('unmanaged_high_bits', 0xff7fffff),
           resolve_case('all_ones', 0xffffffff), resolve_case('id_ffffff', 0x00ffffff),
           resolve_case('index_ffff', managed(0xffff))]
    for index in (0, 1, 0x123, 0xffe, 0xfff):
        for value in (0, 1, 0x456, 0xffe, 0xfff, 0x1000, 0x10005, 0x7fffffff, -1, -0xfffb, -0x80000000):
            out.append(resolve_case(f'index_{index:04x}_slot_{value & 0xffffffff:08x}', managed(index),
                                    [(index, value)], slots=[(value, 0xabcd0000 | index)] if 0 <= value < 0x1000 else []))
    out += [resolve_case('index_1000', managed(0x1000), [(0x1000, 5)], slots=[(5, 0x11112222)]),
            resolve_case('index_fffe', managed(0xfffe), slots=[(0, 0x33334444)]),
            resolve_case('slot_holds_zero', h, [(0x123, 9)]),
            resolve_case('missing_index_table_unmanaged', 0x00654321, table=False),
            resolve_case('missing_index_table_zero', 0, table=False),
            resolve_case('busy', h, [(0x123, 9)], busy=1, slots=[(9, 0x55556666)]),
            resolve_case('busy_byte_ff', h, [(0x123, 9)], busy=0xff, slots=[(9, 0x55556666)]),
            resolve_case('not_ready', h, [(0x123, 9)], ready=0, slots=[(9, 0x55556666)]),
            resolve_case('ready_byte_80', h, [(0x123, 9)], ready=0x80, slots=[(9, 0x55556666)]),
            resolve_case('busy_and_not_ready', 0x00654321, busy=1, ready=0),
            resolve_case('all_three_reports', 0, table=False, busy=1, ready=0)]
    return out


def cases(variant):
    if variant in ('copy_a', 'copy_b'):
        return copy_cases(variant)
    if variant == 'release':
        return release_cases()
    if variant == 'move':
        return move_cases()
    return resolve_cases()


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
