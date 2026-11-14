#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the record selector translations (game/render/record_translation.cpp),
for tools/verify.py run.

usage: record_translation_cases.py OUT_DIR secondary|tertiary

Every selector from -2 to 0x40, the edges of the signed range, and large codes whose low byte
falls inside a group (the whole value is compared). The output word starts as a pattern so a
missing store shows.
"""
import json
import os
import sys

ADDRESSES = {'secondary': '0x02ab7720', 'tertiary': '0x02ab78a0'}


def case(variant, selector):
    selector &= 0xffffffff
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': f'selector_{selector:#010x}', 'returns': 'i32',
            'args': {'rdi': 'buf:value+0x0', 'rsi': selector},
            'buffers': {'value': {'size': 8, 'bytes': 'a5a5a5a5a5a5a5a5'}},
            'memory': [], 'imports': [], 'stubs': []}


def cases(variant):
    selectors = list(range(-2, 0x41)) + [0x7fffffff, -0x80000000, 0x113, 0x10000035, 0x80000027]
    return [case(variant, s) for s in selectors]


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
