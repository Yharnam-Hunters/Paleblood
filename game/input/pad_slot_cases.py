#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the pad slot constructor (game/input/pad_slot.cpp), for tools/verify.py run.

usage: pad_slot_cases.py OUT_DIR

The slot's memory starts filled with 0x00, 0xff and other patterns, so every byte the
constructor writes (and the three it leaves at +0x21) shows. The wide string constructor
(0x02bc0e00) is a scripted stub, compared with its target as it stands then and its text
pointer. Generated fixtures, not recorded inputs.
"""
import json
import os
import sys

ADDRESS, WIDE_STRING_CONSTRUCT = '0x013914a0', '0x02bc0e00'


def case(cid, fill):
    data = bytes(fill[i % len(fill)] for i in range(0x98))
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:slot'},
            'buffers': {'slot': {'size': len(data), 'bytes': data.hex()}}, 'memory': [], 'imports': [],
            'stubs': [{'address': WIDE_STRING_CONSTRUCT, 'ret': 0, 'argc': 2, 'argmem': [{'arg': 0, 'size': 0x38}],
                       'writes': [{'arg': 0, 'offset': 0x18, 'bytes': '0000000000000000'}]}]}


def cases():
    return [case('zeros', b'\x00'), case('ones', b'\xff'), case('pattern', bytes(range(1, 256, 7))),
            case('a5', b'\xa5\x5a')]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
