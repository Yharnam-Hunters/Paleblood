#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for frame_timing_frame_step (0x02418d20), for tools/verify.py run.

usage: frame_step_cases.py OUT_DIR

Objects are small buffers whose vtable slot points at a game function the case stubs (any
function works as a stand-in target; real runs record the real ones). Covers: steady frames,
not running, quit requested, the quit gate open, closed and missing, the quit query created on
demand, and the first frame that allocates and constructs the SprjFlipper.

The `dt_*` cases preset the flipper's frame interval (+0x18) and measured frame time (+0x264):
the original ignores them, the frame-rate patches (and BB_TARGET_FPS) clamp the task update's
frame time with them. `tools/verify.py run --patch PATCHES.xml:"60 FPS++" --env BB_TARGET_FPS=60`
checks the option against the patch on these cases.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02418d20'
# Stand-in vtable targets: game functions that only stubs will answer in these cases.
WINDOW_FN, GATE_FN, QUERY_FN, ALLOC_FN = '0x00400020', '0x004000a0', '0x00400300', '0x004003c0'


def obj(case, name, singleton, offset, target, ret, argc=1):
    case['buffers'][name] = {'size': 8}
    case['buffers'][name + '_vt'] = {'size': offset + 8}
    case['memory'] += [{'addr': singleton, 'pointer': name},
                       {'addr': f'buf:{name}', 'pointer': name + '_vt'},
                       {'addr': f'buf:{name}_vt+{offset}', 'guest': target}]
    case['stubs'].append({'address': target, 'argc': argc, 'ret': ret})


def f32(v):
    return struct.pack('<f', v).hex()


def case(cid, running=1, quitting=0, gate=None, query_exists=True, first_frame=False, dt=None):
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i8', 'args': {},
         'buffers': {'flipper': {'size': 0x2c8}, 'task': {'size': 8}}, 'memory': [], 'stubs': [], 'imports': []}
    obj(c, 'window', '0x05940500', 0x18, WINDOW_FN, running)
    if first_frame:
        c['memory'].append({'addr': '0x059404f8', 'bytes': '00' * 8})
        obj(c, 'allocator', '0x05940408', 0x58, ALLOC_FN, 'buf:flipper', argc=3)
        c['stubs'].append({'address': '0x02434520', 'argc': 1})
    else:
        c['memory'].append({'addr': '0x059404f8', 'pointer': 'flipper'})
    if dt is not None:
        interval, measured = dt
        c['memory'] += [{'addr': 'buf:flipper+24', 'bytes': interval if isinstance(interval, str) else f32(interval)},
                        {'addr': 'buf:flipper+612', 'bytes': measured if isinstance(measured, str) else f32(measured)}]
    c['stubs'].append({'address': '0x02434770', 'argc': 1})
    c['memory'].append({'addr': '0x05940510', 'pointer': 'task'})
    c['stubs'].append({'address': '0x024512a0', 'argi': [1], 'argmem': [{'arg': 1, 'size': 12}]})
    if gate is None:
        c['memory'].append({'addr': '0x05a9fa30', 'bytes': '00' * 8})
    else:
        obj(c, 'gate', '0x05a9fa30', 0x28, GATE_FN, gate)
    if query_exists:
        obj(c, 'query', '0x05aa54f8', 0x98, QUERY_FN, quitting)
    else:
        c['memory'].append({'addr': '0x05aa54f8', 'bytes': '00' * 8})
        c['buffers']['query'] = {'size': 8}
        c['buffers']['query_vt'] = {'size': 0x98 + 8}
        c['memory'] += [{'addr': 'buf:query', 'pointer': 'query_vt'},
                        {'addr': 'buf:query_vt+152', 'guest': QUERY_FN}]
        c['stubs'] += [{'address': '0x024e2550', 'ret': 'buf:query'}, {'address': QUERY_FN, 'argc': 1, 'ret': quitting}]
    return c


CASES = [
    case('steady'),
    case('not_running', running=0),
    case('quit_requested', quitting=1),
    case('running_value_2', running=2),
    case('quit_value_2', quitting=2),
    case('gate_open', gate=1),
    case('gate_closed', gate=0),
    case('query_created', query_exists=False),
    case('first_frame', first_frame=True),
    case('first_frame_quit', first_frame=True, quitting=1, gate=1, query_exists=False),
    # Interval exactly 1/30 (float bits 0x3d088889): no clamp even with the patch.
    case('dt_interval_30', dt=('8988083d', 0.05)),
    case('dt_60_below', dt=(1 / 60, 0.0125)),
    case('dt_60_inside', dt=(1 / 60, 0.02)),
    case('dt_60_above', dt=(1 / 60, 0.05)),
    case('dt_60_nan', dt=(1 / 60, '0000c07f')),
    case('dt_60_negative', dt=(1 / 60, -1.0)),
    case('dt_zero', dt=(0.0, 0.0)),
    case('dt_interval_nan', dt=('0000c07f', 0.02)),
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
