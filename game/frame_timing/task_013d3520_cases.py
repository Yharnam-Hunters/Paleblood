#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for frame_timing_task_013d3520 (0x013d3520), for tools/verify.py run.

usage: task_013d3520_cases.py OUT_DIR

Parts at +0x10, +0x18 and +0x70 present or missing, and frame times including 0, NaN, a
signalling NaN (the Uncap patch's division quiets it) and negative values.
"""
import json
import os
import struct
import sys

STEP_VTABLE = '0x056efd40'


def case(cid, seconds, parts=(0x10, 0x18, 0x70)):
    raw = seconds if isinstance(seconds, str) else struct.pack('<f', seconds).hex()
    memory = [{'addr': 'buf:step', 'guest': STEP_VTABLE}, {'addr': 'buf:step+8', 'bytes': raw}]
    stubs = [{'address': '0x01456eb0', 'argc': 1}]
    for off in (0x10, 0x18, 0x70):
        memory.append({'addr': f'buf:self+{off}', 'pointer': f'part{off}'} if off in parts
                      else {'addr': f'buf:self+{off}', 'bytes': '00' * 8})
    if 0x70 in parts:
        stubs.append({'address': '0x01456c80', 'argc': 1})
    if 0x18 in parts:
        stubs.append({'address': '0x013daac0', 'argc': 2, 'argmem': [{'arg': 1, 'size': 12}]})
    # 0x013d5440 is scripted always: the uncapped variant calls it even without the part.
    stubs.append({'address': '0x013d5440', 'argc': 1, 'argf32': [0]})
    stubs += [{'address': '0x01456ed0', 'argc': 1}, {'address': '0x01456ef0'}]
    return {'schema': 1, 'address': '0x013d3520', 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:self', 'rsi': 'buf:step'},
            'buffers': {'self': {'size': 120}, 'step': {'size': 16}, 'part16': {'size': 8},
                        'part24': {'size': 8}, 'part112': {'size': 8}},
            'memory': memory, 'stubs': stubs, 'imports': []}


CASES = [
    case('all_parts_30', 1 / 30),
    case('all_parts_60', 1 / 60),
    case('zero', 0.0),
    case('negative', -0.5),
    case('quiet_nan', '0000c07f'),
    case('signalling_nan', '0100a07f'),
    case('no_parts', 1 / 30, ()),
    case('only_10', 1 / 30, (0x10,)),
    case('only_18', 1 / 30, (0x18,)),
    case('only_70', 1 / 30, (0x70,)),
    case('no_10', 0.25, (0x18, 0x70)),
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
