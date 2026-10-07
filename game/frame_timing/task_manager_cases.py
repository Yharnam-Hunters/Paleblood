#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the task manager's frame functions (game/frame_timing/task_manager.cpp).

usage: task_manager_cases.py OUT_ROOT

Writes OUT_ROOT/<function>/edge/*.json for each function: frame_timing_task_run_all_01388c60
(manager and frame pointers passed on with -1 as the group mask, including null and high
values) and frame_timing_task_set_frame_value_0143f9f0 (the float copied bit for bit: zero,
-0, NaN with a payload, a subnormal, infinity, an ordinary frame time).
"""
import json
import os
import struct
import sys


def run_all(cid, manager, frame):
    return {'schema': 1, 'address': '0x01388c60', 'id': cid, 'returns': 'void',
            'args': {'rdi': hex(manager), 'rsi': hex(frame)}, 'buffers': {}, 'memory': [],
            'stubs': [{'address': '0x01388c70', 'argc': 3}], 'imports': []}


def frame_value(cid, bits):
    return {'schema': 1, 'address': '0x0143f9f0', 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:frame'},
            'buffers': {'frame': {'size': 16}},
            'memory': [{'addr': 'buf:frame+0', 'bytes': 'a5' * 8}, {'addr': 'buf:frame+8', 'bytes': struct.pack('<I', bits).hex()},
                       {'addr': 'buf:frame+12', 'bytes': '5a5a5a5a'},
                       {'addr': '0x058b7e00', 'bytes': '11' * 16}],
            'stubs': [], 'imports': []}


CASES = {
    'frame_timing_task_run_all_01388c60': [
        run_all('typical', 0x1004a0b000, 0x7ffd1234), run_all('null_both', 0, 0),
        run_all('high_bits', 0xffffffff00000000, 0x8000000000000001), run_all('low_word_only', 0xffffffff, 0x1)],
    'frame_timing_task_set_frame_value_0143f9f0': [
        frame_value('frame_time', 0x3d088889), frame_value('zero', 0), frame_value('negative_zero', 0x80000000),
        frame_value('nan_payload', 0x7fc00123), frame_value('signalling_nan', 0x7f800001),
        frame_value('subnormal', 0x00000001), frame_value('infinity', 0x7f800000)],
}


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    for function, cases in CASES.items():
        d = os.path.join(sys.argv[1], function, 'edge')
        os.makedirs(d, exist_ok=True)
        for c in cases:
            with open(os.path.join(d, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
        print(f'{len(cases)} cases written to {d}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
