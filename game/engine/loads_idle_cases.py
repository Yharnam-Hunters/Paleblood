#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's idle check (game/engine/loads_idle.cpp), for tools/verify.py run.

usage: loads_idle_cases.py OUT_DIR

The check reads three signed 32-bit counters (0x05978fd8, 0x05978a38, 0x05978d08) and takes no
arguments and no stubs. Each case sets the three counters:
- every combination of -1, 0 and 1 (27 cases: the first counter above 0 decides, and 0 or below
  does not);
- each counter alone at INT_MIN, 2 and INT_MAX with the other two at 0 (a signed compare: INT_MIN is
  not busy, a count of 2 and INT_MAX are);
- each counter alone at INT_MIN with the others at 1 (busy through the others), and all three at
  INT_MAX and INT_MIN.
Generated fixtures, not inputs recorded in a game run.
"""
import itertools
import json
import os
import struct
import sys

ADDRESS = '0x01ee9b40'
COUNTERS = ('0x05978fd8', '0x05978a38', '0x05978d08')
INT_MAX, INT_MIN = 0x7fffffff, 0x80000000


def case(cid, values):
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i8', 'args': {}, 'imports': [], 'stubs': [],
            'memory': [{'addr': a, 'bytes': struct.pack('<I', v & 0xffffffff).hex()} for a, v in zip(COUNTERS, values)]}


def name(values):
    return '_'.join(f'{v & 0xffffffff:x}' for v in values)


def cases():
    out = []
    seen = set()

    def add(values):
        values = tuple(values)
        if values not in seen:
            seen.add(values)
            out.append(case(f'counters_{name(values)}', values))

    for values in itertools.product((-1, 0, 1), repeat=3):
        add(values)
    for position in range(3):
        for value in (INT_MIN, 2, INT_MAX):
            values = [0, 0, 0]
            values[position] = value
            add(values)
        values = [1, 1, 1]
        values[position] = INT_MIN
        add(values)
    add((INT_MAX, INT_MAX, INT_MAX))
    add((INT_MIN, INT_MIN, INT_MIN))
    return out


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
