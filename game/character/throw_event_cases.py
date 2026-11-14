#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for character_throw_event_01e19e20 (game/character/throw_event.cpp), for
tools/verify.py run.

usage: throw_event_cases.py OUT_DIR

- the manager-ready global clear (nothing is read); a missing manager (reported by name; the
  report's stub stores a manager into the global through its name argument, so the second read
  finds one); a missing table holder or table;
- the lookup (stubbed) giving -1, INT_MIN, 0, 1 and INT_MAX; a missing behavior or graph with a
  known name (the lookup still runs);
- rings with room, with the write index at the capacity (wraps to 0), full (grown to twice the
  capacity by the stubbed reserve, which here also moves the events), empty (grown to 8), over
  full and with a negative count (no growth: the compare is signed); a grown ring whose write
  index was at the old capacity (the capacity is read again after growing, so no wrap).
The events buffers are filled with 0xee so every written byte shows.

The four padding bytes of each queued event come from the stack below the harness's call (the
original never writes them; the replacement's entry passes the same bytes on). The harness
leaves part of a stack address there, which differs between the two sides' processes under
address-space randomisation, so run these cases with it off:
    setarch x86_64 -R python3 tools/verify.py run --function character_throw_event_01e19e20 ...
"""
import json
import os
import struct
import sys

ADDRESS = '0x01e19e20'
READY = 0x05940210
MANAGER = 0x059401f8
MANAGER_NAME = 0x04d35c22
LOOKUP, RESERVE, FATAL_ERROR = '0x00d4a2c0', '0x029bc380', '0x024b55b0'
EVENT_SIZE = 0x18
EVENTS = 16
GROWN_EVENTS = 32
NAME = b'event_name\0'


def i32(v):
    return struct.pack('<i', v).hex()


def case(cid, ready=True, manager=True, holder=True, table=True, event_id=3, behavior=True, graph=True,
         capacity=8, write_index=2, count=2, grow_to=None, report_sets_manager=False):
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
         'args': {'rdi': 'buf:anim', 'rsi': 'buf:name'},
         'buffers': {'anim': {'size': 0x20}, 'name': {'size': len(NAME), 'bytes': NAME.hex()},
                     'manager': {'size': 0x10}, 'holder': {'size': 0x8}, 'table': {'size': 0x60},
                     'behavior': {'size': 0x20}, 'graph': {'size': 0xc0},
                     'ring': {'size': 0x18, 'bytes': '00' * 0xc + 'cdcdcdcd' + '00' * 8},
                     'events': {'size': EVENTS * EVENT_SIZE, 'bytes': 'ee' * EVENTS * EVENT_SIZE},
                     'grown': {'size': GROWN_EVENTS * EVENT_SIZE, 'bytes': 'ee' * GROWN_EVENTS * EVENT_SIZE}},
         'memory': [{'addr': f'{READY:#010x}', 'bytes': ('01' if ready else '00') + '00' * 7},
                    {'addr': f'{MANAGER:#010x}', **({'pointer': 'manager'} if manager else {'bytes': '00' * 8})},
                    {'addr': 'buf:manager+0x8', **({'pointer': 'holder'} if holder else {'bytes': '00' * 8})},
                    {'addr': 'buf:holder', **({'pointer': 'table'} if table else {'bytes': '00' * 8})},
                    {'addr': 'buf:anim+0x10', **({'pointer': 'behavior'} if behavior else {'bytes': '00' * 8})},
                    {'addr': 'buf:behavior+0x18', **({'pointer': 'graph'} if graph else {'bytes': '00' * 8})},
                    {'addr': 'buf:graph+0xb8', 'pointer': 'ring'},
                    {'addr': 'buf:ring', 'pointer': 'events'},
                    {'addr': 'buf:ring+0x8', 'bytes': i32(capacity)},
                    {'addr': 'buf:ring+0x10', 'bytes': i32(write_index)},
                    {'addr': 'buf:ring+0x14', 'bytes': i32(count)}],
         'imports': [], 'stubs': []}
    if not ready:
        return c
    if not manager:
        report = {'address': FATAL_ERROR, 'argc': 4}
        if report_sets_manager:
            report['writes'] = [{'arg': 3, 'offset': MANAGER - MANAGER_NAME, 'pointer': 'manager'}]
        c['stubs'].append(report)
        if not report_sets_manager:
            return c
    if not holder or not table:
        return c
    c['stubs'].append({'address': LOOKUP, 'argc': 2, 'ret': hex(event_id & 0xffffffff)})
    if behavior and graph and event_id >= 0 and grow_to is not None:
        c['stubs'].append({'address': RESERVE, 'argc': 2, 'argu32': [1],
                           'writes': [{'arg': 0, 'pointer': 'grown'},
                                      {'arg': 0, 'offset': 8, 'bytes': i32(grow_to)}]})
    return c


CASES = [
    case('not_ready', ready=False),
    case('manager_missing_reported', manager=False, report_sets_manager=True),
    case('holder_missing', holder=False),
    case('table_missing', table=False),
    case('unknown_name', event_id=-1),
    case('id_int_min', event_id=-0x80000000),
    case('id_zero', event_id=0),
    case('id_one', event_id=1),
    case('id_int_max', event_id=0x7fffffff),
    case('behavior_missing', behavior=False),
    case('graph_missing', graph=False),
    case('behavior_missing_unknown_name', behavior=False, event_id=-1),
    case('room_first_slot', capacity=8, write_index=0, count=0),
    case('room_middle', capacity=8, write_index=5, count=3),
    case('room_last_slot', capacity=8, write_index=7, count=7),
    case('index_at_capacity_wraps', capacity=8, write_index=8, count=3),
    case('index_past_capacity', capacity=4, write_index=5, count=1),
    case('full_grows_double', capacity=4, write_index=4, count=4, grow_to=8),
    case('full_grows_index_mid', capacity=4, write_index=1, count=4, grow_to=8),
    case('empty_grows_to_eight', capacity=0, write_index=0, count=0, grow_to=8),
    case('over_full_grows', capacity=4, write_index=2, count=6, grow_to=8),
    case('full_grow_keeps_capacity', capacity=4, write_index=4, count=4, grow_to=4),
    case('negative_count_no_growth', capacity=4, write_index=1, count=-1),
    case('negative_capacity_grows', capacity=-2, write_index=0, count=0, grow_to=8),
    case('full_sixteen', capacity=16, write_index=16, count=16, grow_to=32),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
