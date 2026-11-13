#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the per-frame task's helpers (game/frame_timing/task_parts.cpp), for
tools/verify.py run.

usage: task_parts_cases.py OUT_DIR start|stop|frame_end|items|listeners

- start / stop: the profiling switch off, 1, 0x80 and 0xff, with the first, a middle and the
  last stopwatch (idle stopwatches for start, running ones for stop).
- frame_end: the switch off and on; a first call that clears the switch (the switch is read
  once) and one that replaces a later table entry (each entry is read just before its call).
- items: empty, one and three items; a first item that moves the list end in (the ends are
  read once).
- listeners: empty, one and three listeners; a preparation that moves the end (the list is read
  after it); a first listener that shrinks, grows or replaces the list (it is read again after
  every call).
"""
import json
import os
import sys

ADDRESSES = {'start': '0x01456eb0', 'stop': '0x01456ed0', 'frame_end': '0x01456ef0',
             'items': '0x01456c80', 'listeners': '0x013daac0'}
SWITCH = 0x058b8718
TABLE = 0x058b86c0
STOPWATCHES = 10
STOPWATCH_START_SECONDS = 0x38   # game/frame_timing/stopwatch.cpp
ONE_SECOND = '0000803f'
STOPWATCH_START, STOPWATCH_STOP, STOPWATCH_FRAME_END = '0x01448580', '0x01448620', '0x014486d0'
ITEM_UPDATE = '0x013d4810'
LISTENER_PREPARE = '0x013dab20'
ON_FRAME = '0x00500200'   # fake address in the listener vtable's third slot
SWITCH_STATES = (0, 1, 0x80, 0xff)


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def profile_setup(c, switch, entries=None):
    """Ten stopwatch buffers in the table (or the given entries) and the switch byte."""
    c['buffers'].update({f'watch{i}': {'size': 0x60} for i in range(STOPWATCHES)})
    entries = entries or {}
    for i in range(STOPWATCHES):
        entry = entries.get(i, {'pointer': f'watch{i}'})
        c['memory'].append({'addr': f'{TABLE + 8 * i:#010x}', **entry})
    c['memory'].append({'addr': f'{SWITCH:#010x}', 'bytes': f'{switch:02x}'})


def single_case(variant, switch, index):
    c = base(variant, f'switch_{switch:#04x}_watch_{index}')
    c['args'] = {'rdi': index}
    profile_setup(c, switch)
    if variant == 'stop':
        # Running stopwatches, so a stop made with the switch off would change one.
        c['memory'] += [{'addr': f'buf:watch{i}+{STOPWATCH_START_SECONDS:#x}', 'bytes': ONE_SECOND}
                        for i in range(STOPWATCHES)]
    if switch:
        c['stubs'] = [{'address': STOPWATCH_START if variant == 'start' else STOPWATCH_STOP, 'argc': 1}]
    return c


def frame_end_case(cid, switch, entries=None, first_writes=None):
    c = base('frame_end', cid)
    profile_setup(c, switch, entries)
    if switch:
        c['stubs'] = [{'address': STOPWATCH_FRAME_END, 'argc': 1} for _ in range(STOPWATCHES)]
        if first_writes:
            c['stubs'][0]['writes'] = first_writes
    return c


def frame_end_cases():
    out = [frame_end_case(f'switch_{s:#04x}', s) for s in SWITCH_STATES]
    # The first "stopwatch" overlaps the table's end, so its tick can clear the switch.
    out.append(frame_end_case('first_tick_clears_switch', 1, {0: {'guest': f'{SWITCH - 0x18:#010x}'}},
                              [{'arg': 0, 'offset': 0x18, 'bytes': '00'}]))
    # The first "stopwatch" is the table itself, so its tick can replace entry 3.
    out.append(frame_end_case('first_tick_replaces_entry_3', 1, {0: {'guest': f'{TABLE:#010x}'}},
                              [{'arg': 0, 'offset': 0x18, 'pointer': 'watch0'}]))
    return out


def items_case(cid, count, first_moves_end_to=None):
    """The list owner doubles as its first item when that item moves the end."""
    c = base('items', cid)
    c['args'] = {'rdi': 'buf:list'}
    c['buffers'] = {'list': {'size': 0x20}, 'items': {'size': 0x20}}
    c['buffers'].update({f'item{i}': {'size': 0x20} for i in range(4)})
    for i in range(4):
        target = 'list' if i == 0 and first_moves_end_to is not None else f'item{i}'
        c['memory'].append({'addr': f'buf:items+{8 * i:#x}', 'pointer': target})
    c['memory'] += [{'addr': 'buf:list+0x8', 'pointer': 'items'},
                    {'addr': 'buf:list+0x10', 'pointer': f'items+{8 * count:#x}'}]
    c['stubs'] = [{'address': ITEM_UPDATE, 'argc': 1} for _ in range(count)]
    if first_moves_end_to is not None and count:
        c['stubs'][0]['writes'] = [{'arg': 0, 'offset': 0x10, 'pointer': f'items+{8 * first_moves_end_to:#x}'}]
    return c


def listeners_case(cid, count, calls=None, prepare_end=None, first_writes=None):
    """The listener array holds up to four listeners and a second array 'other' two. When the
    first listener rewrites the list it is the list owner's +0x10 (its vtable pointer there)."""
    c = base('listeners', cid)
    c['args'] = {'rdi': 'buf:list', 'rsi': 'buf:frame_time'}
    c['buffers'] = {'list': {'size': 0x48}, 'array': {'size': 0x20}, 'other': {'size': 0x10},
                    'vtable': {'size': 0x18}, 'frame_time': {'size': 0x10}}
    c['buffers'].update({f'listener{i}': {'size': 0x30} for i in range(6)})
    c['memory'] = [{'addr': 'buf:vtable+0x10', 'guest': ON_FRAME},
                   {'addr': 'buf:frame_time+0x8', 'bytes': '89888808'}]
    for i in range(6):
        c['memory'].append({'addr': f'buf:listener{i}', 'pointer': 'vtable'})
    owner_first = first_writes is not None
    if owner_first:
        c['memory'].append({'addr': 'buf:list+0x10', 'pointer': 'vtable'})
    for i in range(4):
        target = 'list+0x10' if i == 0 and owner_first else f'listener{i}'
        c['memory'].append({'addr': f'buf:array+{8 * i:#x}', 'pointer': target})
    c['memory'] += [{'addr': 'buf:other', 'pointer': 'listener4'}, {'addr': 'buf:other+0x8', 'pointer': 'listener5'},
                    {'addr': 'buf:list+0x20', 'pointer': 'array'},
                    {'addr': 'buf:list+0x28', 'pointer': f'array+{8 * count:#x}'}]
    prepare = {'address': LISTENER_PREPARE, 'argc': 1}
    if prepare_end is not None:
        prepare['writes'] = [{'arg': 0, 'offset': 0x28, 'pointer': f'array+{8 * prepare_end:#x}'}]
    c['stubs'] = [prepare] + [{'address': ON_FRAME, 'argc': 2} for _ in range(count if calls is None else calls)]
    if first_writes:
        c['stubs'][1]['writes'] = first_writes
    return c


def listeners_cases():
    end = lambda n: [{'arg': 0, 'offset': 0x18, 'pointer': f'array+{8 * n:#x}'}]
    return [listeners_case('empty', 0), listeners_case('one', 1), listeners_case('three', 3),
            listeners_case('prepare_empties', 3, calls=0, prepare_end=0),
            listeners_case('prepare_fills_four', 0, calls=4, prepare_end=4),
            listeners_case('first_keeps_list', 3, first_writes=[]),
            listeners_case('first_shrinks_to_one', 3, calls=1, first_writes=end(1)),
            listeners_case('first_shrinks_to_two', 3, calls=2, first_writes=end(2)),
            listeners_case('first_grows_to_four', 2, calls=4, first_writes=end(4)),
            # A new array of two: index 1 of it (listener5) is the only further call.
            listeners_case('first_replaces_array', 3, calls=2,
                           first_writes=[{'arg': 0, 'offset': 0x10, 'pointer': 'other'},
                                         {'arg': 0, 'offset': 0x18, 'pointer': 'other+0x10'}])]


def cases(variant):
    if variant in ('start', 'stop'):
        return [single_case(variant, s, i) for s in SWITCH_STATES for i in (0, 4, STOPWATCHES - 1)]
    if variant == 'frame_end':
        return frame_end_cases()
    if variant == 'items':
        return [items_case('empty', 0), items_case('one', 1), items_case('three', 3), items_case('four', 4),
                items_case('first_moves_end_in', 3, first_moves_end_to=1),
                items_case('first_moves_end_to_start', 4, first_moves_end_to=0)]
    return listeners_cases()


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
