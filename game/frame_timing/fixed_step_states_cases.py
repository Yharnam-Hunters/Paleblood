#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the fixed-step state methods (fixed_step_states.cpp), for tools/verify.py run.

usage: fixed_step_states_cases.py CAPTURES_DIR

Writes CAPTURES_DIR/<function>/edge/*.json for all nine methods. The owner's advance method
(vtable slot +0xd0) is a stand-in game function the case stubs; its descriptor is compared, and
it leaves the busy count the case says. Covers idle and busy owners, staying busy after the
advance, and states around the 2..5 range of request_7 (with wraparound values).
"""
import json
import os
import struct
import sys

ADVANCE = '0x00400020'
METHODS = [  # (address, kind, owner field)
    ('0x01f6a9f0', 'until_idle', 8), ('0x0200d8a0', 'until_idle', 16), ('0x0200e100', 'until_idle', 8),
    ('0x0200e270', 'request_7', 8), ('0x02012610', 'until_idle', 8), ('0x02012780', 'request_10', 8),
    ('0x020128f0', 'until_idle', 8), ('0x02012a60', 'request_7', 8), ('0x02012bf0', 'until_idle', 8),
]


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def case(address, kind, field, cid, busy, busy_after=0, state=3, request=0x55):
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void' if kind == 'request_10' else 'i32',
         'args': {'rdi': 'buf:self'},
         'buffers': {'self': {'size': 24}, 'owner': {'size': 616}, 'owner_vt': {'size': 216}},
         'memory': [{'addr': f'buf:self+{field}', 'pointer': 'owner'}, {'addr': 'buf:owner', 'pointer': 'owner_vt'},
                    {'addr': 'buf:owner+604', 'bytes': u32(state)}, {'addr': 'buf:owner+608', 'bytes': u32(request)},
                    {'addr': 'buf:owner+612', 'bytes': u32(busy)}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE}],
         'stubs': [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1], 'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': 604, 'size': 12}],
                    'writes': [{'arg': 0, 'offset': 612, 'bytes': u32(busy_after)}]}],
         'imports': []}
    return c


def cases_for(address, kind, field):
    out = [case(address, kind, field, 'idle', 0), case(address, kind, field, 'busy_then_idle', 1, 0),
           case(address, kind, field, 'busy_stays_busy', 1, 1), case(address, kind, field, 'busy_negative', -1, 0),
           case(address, kind, field, 'idle_becomes_busy', 0, 5)]
    if kind == 'request_7':
        for s in (0, 1, 2, 5, 6, 0xffffffff, 0x80000002):
            out.append(case(address, kind, field, f'idle_state_{s:x}', 0, 0, state=s))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    n = 0
    for address, kind, field in METHODS:
        out = os.path.join(sys.argv[1], f'frame_timing_{kind}_{address[2:]}', 'edge')
        os.makedirs(out, exist_ok=True)
        for c in cases_for(address, kind, field):
            with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
            n += 1
    print(f'{n} cases written under {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
