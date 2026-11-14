#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/segregated_bi_heap_reset.cpp, for tools/verify.py run.

usage: segregated_bi_heap_reset_cases.py OUT_DIR

Each case fills a heap object (`heap`, a little larger than its state) with a byte pattern; the
reset must clear exactly the bytes from the set-up flag to the end of the state.
"""
import json
import os
import sys

RESET = '0x02487dd0'
SIZE = 0xd0


def case(cid, fill):
    return {'schema': 1, 'address': RESET, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:heap'},
            'buffers': {'heap': {'size': SIZE, 'bytes': fill}}, 'memory': [], 'stubs': [], 'imports': []}


CASES = [
    case('pattern', 'a5' * SIZE),
    case('all_ones', 'ff' * SIZE),
    case('counting', ''.join(f'{i & 0xff:02x}' for i in range(1, SIZE + 1))),
    case('already_zero', '00' * SIZE),
]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for c in CASES:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases in {out}')


if __name__ == '__main__':
    main()
