#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the item handle assignments (game/character/gaitem_handle_assign.cpp), for
tools/verify.py run.

usage: gaitem_handle_assign_cases.py OUT_DIR copy_a|copy_b|value

copy_a and copy_b are the two copies of the copy assignment (target and source both in memory);
value is the assignment from a handle value (the new handle in a register). The handle shapes,
slot indices and reference counts are those of gaitem_handle_cases.py.

- target_*: the handle the target held, in every shape, replaced by a live handle. A live
  target counts one less; a count below 2 is cleared and the item free (0x01e86e60, a scripted
  stub compared with the manager and the handle) is called.
- source_*: the new handle in every shape, replacing a live handle. A live source counts one
  more.
- Equal handles (live, unmanaged, zero, and one place given as both): nothing changes, not even
  a count of 1 or 0. A zero target, a zero source, and a stale handle on the slot of a live one.
- A missing item manager: one report for a nonzero source and one for a nonzero target, none for
  equal handles. In manager_appears_* the report's stub stores the manager, so what the code does
  with its second read of the singleton shows; in *_changed_by_the_report it also stores another
  handle over the one being looked at, which is read again after the report.
- Handles stored where the assignment itself writes: the target as the count the source raises
  (it is read again for the release), the source as the count it raises or the release lowers
  (it is read again for the store), the source changed by the item free, the target changed by
  the item free (the store comes last), the count changed by the item free (it is cleared
  before the call).
- value only: register bits above the handle.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

from gaitem_handle_cases import (FATAL_ERROR, FREE_ITEM, MANAGER_GLOBAL, MANAGER_SIZE, SLOTS, handle_situations,
                                 managed, slot_entry)

ADDRESSES = {'copy_a': '0x01e8bd80', 'copy_b': '0x01e8bed0', 'value': '0x01e8c020'}
# The source file name the singleton check reports with: the report's first argument, which the
# stub of manager_appears_* reaches the manager singleton from.
SINGLETON_CHECK_FILE = 0x04d3b369
REPORT = {'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 2, 3], 'argu32': [1]}
STORE_MANAGER = {'arg': 0, 'offset': int(MANAGER_GLOBAL, 16) - SINGLETON_CHECK_FILE, 'pointer': 'manager'}
REPORT_STORING_MANAGER = dict(REPORT, writes=[STORE_MANAGER])
# A place for a handle in the image, next to the singleton, where the report's stub reaches it too.
IMAGE_HANDLE = int(MANAGER_GLOBAL, 16) + 0x10


def free_stub(writes=None):
    """The item free, compared with the manager and the handle; writes: what it stores."""
    stub = {'address': FREE_ITEM, 'argc': 2, 'argi': [0], 'argu32': [1]}
    if writes:
        stub['writes'] = writes
    return stub


def word(value):
    return struct.pack('<I', value).hex()


def report_storing(handle):
    """The report, storing the manager into the singleton and `handle` at IMAGE_HANDLE."""
    return dict(REPORT, writes=[STORE_MANAGER, {'arg': 0, 'offset': IMAGE_HANDLE - SINGLETON_CHECK_FILE,
                                                'bytes': word(handle)}])


def place(c, register, where, handle):
    """Passes `where` (a buffer offset or an image address) in a register and stores the handle
    there."""
    c['args'][register] = where if isinstance(where, str) else f'guest:{where:#010x}'
    c['memory'].append({'addr': where if isinstance(where, str) else f'{where:#010x}', 'bytes': word(handle)})


def count_of(index):
    """Where the reference count of a slot is."""
    return f'buf:manager+{SLOTS + index * 8 + 4:#x}'


def assign_case(variant, cid, target, source, slots=(), stubs=(), manager=True, target_at=None, source_at=None,
                register_high=0):
    """target, source: the two handles. target_at / source_at: where a handle is stored when
    it is not in the buffer of its own, a buffer offset or an image address (written there
    after the slots). stubs: the calls expected, in order."""
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'i64', 'args': {},
         'buffers': {'manager': {'size': MANAGER_SIZE}}, 'memory': [], 'imports': [], 'stubs': list(stubs)}
    c['memory'].append({'addr': MANAGER_GLOBAL, 'pointer': 'manager'} if manager else
                       {'addr': MANAGER_GLOBAL, 'bytes': '00' * 8})
    for index, handle, references in slots:
        c['memory'].append(slot_entry(index, handle, references))
    c['buffers']['handles'] = {'size': 0x10,
                               'bytes': struct.pack('<IIII', target, 0x5e5e5e5e, source, 0x6f6f6f6f).hex()}
    place(c, 'rdi', target_at or 'buf:handles', target)
    if variant == 'value':
        c['args']['rsi'] = (register_high << 32) | source
    else:
        place(c, 'rsi', source_at or 'buf:handles+0x8', source)
    return c


def freed(handle, references):
    """The item free of a released handle whose live slot has that many references."""
    return [free_stub()] if handle != 0 and references is not None and references < 2 else []


def shared_cases(variant):
    other = managed(0x99, 0x34)
    out = []
    for cid, handle, slots, references in handle_situations():
        out.append(assign_case(variant, f'target_{cid}', handle, other, slots + [(0x99, other, 3)],
                               freed(handle, references)))
        out.append(assign_case(variant, f'source_{cid}', other, handle, slots + [(0x99, other, 3)]))
    h, stale = managed(0x10), managed(0x10) ^ 0x01000000
    out += [
        assign_case(variant, 'equal_handles', h, h, [(0x10, h, 2)]),
        assign_case(variant, 'equal_handles_last_reference', h, h, [(0x10, h, 1)]),
        assign_case(variant, 'equal_handles_no_reference', h, h, [(0x10, h, 0)]),
        assign_case(variant, 'equal_unmanaged', 0x00123456, 0x00123456),
        assign_case(variant, 'equal_zero', 0, 0),
        assign_case(variant, 'zero_target', 0, h, [(0x10, h, 1)]),
        assign_case(variant, 'zero_source', h, 0, [(0x10, h, 2)]),
        assign_case(variant, 'zero_source_last_reference', h, 0, [(0x10, h, 1)], [free_stub()]),
        assign_case(variant, 'stale_source_on_the_target_slot', h, stale, [(0x10, h, 1)], [free_stub()]),
        assign_case(variant, 'stale_target_on_the_source_slot', stale, h, [(0x10, h, 1)]),
        assign_case(variant, 'missing_manager_both_unmanaged', 0x00123456, 0x00654321, stubs=[REPORT, REPORT],
                    manager=False),
        assign_case(variant, 'missing_manager_both_no_slot', managed(0xffff), 0x00ffffff, stubs=[REPORT, REPORT],
                    manager=False),
        assign_case(variant, 'missing_manager_zero_target', 0, 0x00654321, stubs=[REPORT], manager=False),
        assign_case(variant, 'missing_manager_zero_source', 0x00123456, 0, stubs=[REPORT], manager=False),
        assign_case(variant, 'missing_manager_equal_handles', h, h, manager=False),
        assign_case(variant, 'missing_manager_equal_zero', 0, 0, manager=False),
        # The report stores the manager: the read after it finds one, and the second handle needs
        # no report.
        assign_case(variant, 'manager_appears_for_the_source', 0, h, [(0x10, h, 1)], [REPORT_STORING_MANAGER],
                    manager=False),
        assign_case(variant, 'manager_appears_for_the_target', h, 0, [(0x10, h, 1)],
                    [REPORT_STORING_MANAGER, free_stub()], manager=False),
        assign_case(variant, 'manager_appears_once_for_both', h, other, [(0x10, h, 3), (0x99, other, 1)],
                    [REPORT_STORING_MANAGER], manager=False),
    ]
    # The target is the count of the source's slot: raised by one before the release, it names
    # another slot (0x41, its last reference) than the handle it held (slot 0x40).
    source, held = managed(0x30), managed(0x40)
    out.append(assign_case(variant, 'target_is_the_count_the_source_raises', held, source,
                           [(0x30, source, held), (0x40, held, 3), (0x41, held + 1, 1)], [free_stub()],
                           target_at=count_of(0x30)))
    # The item free writes over the target; the new handle is stored after it.
    out.append(assign_case(variant, 'target_changed_by_the_free', h, other, [(0x10, h, 1), (0x99, other, 3)],
                           [free_stub([{'arg': 0, 'offset': 0, 'bytes': 'a1a2a3a4'}])], target_at='buf:manager'))
    # The item free writes the count of the slot it frees, which was cleared before the call.
    out.append(assign_case(variant, 'count_changed_by_the_free', h, other, [(0x10, h, 1), (0x99, other, 3)],
                           [free_stub([{'arg': 0, 'offset': SLOTS + 0x10 * 8 + 4, 'bytes': word(7)}])]))
    # The report stores the manager and another handle over the target: that one is released.
    out.append(assign_case(variant, 'target_changed_by_the_report', h, 0, [(0x10, h, 3), (0x99, other, 1)],
                           [report_storing(other), free_stub()], manager=False, target_at=IMAGE_HANDLE))
    return out


def copy_cases(variant):
    h, other = managed(0x10), managed(0x99, 0x34)
    out = shared_cases(variant)
    same_place = assign_case(variant, 'one_place_as_both', h, h, [(0x10, h, 1)])
    same_place['args']['rsi'] = same_place['args']['rdi']
    out.append(same_place)
    # The source is the count of its own slot, which holds the handle as its count.
    own = managed(0x30)
    out.append(assign_case(variant, 'source_is_the_count_it_raises', 0, own, [(0x30, own, own)],
                           source_at=count_of(0x30)))
    # The source is the count of the target's slot: an unmanaged handle (5, then 1) that the
    # release lowers or clears before the store.
    held = managed(0x50)
    out.append(assign_case(variant, 'source_is_the_count_the_release_lowers', held, 5, [(0x50, held, 5)],
                           source_at=count_of(0x50)))
    out.append(assign_case(variant, 'source_is_the_count_the_release_clears', held, 1, [(0x50, held, 1)],
                           [free_stub()], source_at=count_of(0x50)))
    # The item free writes another handle over the source.
    out.append(assign_case(variant, 'source_changed_by_the_free', h, other, [(0x10, h, 1), (0x99, other, 3)],
                           [free_stub([{'arg': 0, 'offset': 0, 'bytes': word(0x00777777)}])],
                           source_at='buf:manager'))
    # The report stores the manager and another handle over the source: that one is counted and
    # stored.
    out.append(assign_case(variant, 'source_changed_by_the_report', 0, h, [(0x10, h, 3), (0x99, other, 3)],
                           [report_storing(other)], manager=False, source_at=IMAGE_HANDLE))
    return out


def value_cases():
    h = managed(0x10)
    out = shared_cases('value')
    out += [
        assign_case('value', 'register_high_bits', 0, h, [(0x10, h, 1)], register_high=0xdeadbeef),
        assign_case('value', 'register_high_bits_equal_handles', h, h, [(0x10, h, 1)], register_high=0xffffffff),
        assign_case('value', 'register_high_bits_zero_handle', h, 0, [(0x10, h, 1)], [free_stub()],
                    register_high=0x00000001),
        # Equal handles are left alone whatever the bits above: no report without a manager.
        assign_case('value', 'register_high_bits_equal_handles_missing_manager', 0x00123456, 0x00123456,
                    manager=False, register_high=0xffffffff),
    ]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = value_cases() if sys.argv[2] == 'value' else copy_cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
