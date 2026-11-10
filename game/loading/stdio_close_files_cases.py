#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_stdio_close_files (0x02e67d20).

usage: stdio_close_files_cases.py OUT_DIR

Every combination of the three FILE pointers being set or null (the order of the fclose calls
and the cleared fields are compared), and fclose failing (its result is ignored). Generated
fixtures, not inputs recorded in a game run.
"""
import json
import os
import sys

ADDRESS = '0x02e67d20'


def case(cid, present, result=0):
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:holder+0x8'},
         'buffers': {'holder': {'size': 0x28, 'bytes': 'b8' * 0x28}}, 'memory': [], 'imports': []}
    for slot in range(3):
        if present[slot]:
            c['buffers'][f'file{slot}'] = {'size': 0x10}
            c['memory'].append({'addr': f'buf:holder+{0x8 + 8 * slot}', 'pointer': f'file{slot}'})
        else:
            c['memory'].append({'addr': f'buf:holder+{0x8 + 8 * slot}', 'bytes': '00' * 8})
    for slot in (2, 1, 0):
        if present[slot]:
            c['imports'].append({'name': 'fclose', 'argc': 1, 'ret': hex(result)})
    return c


CASES = []
for bits in range(8):
    present = [bool(bits >> slot & 1) for slot in range(3)]
    CASES.append(case(f'files_{bits:03b}', present))
CASES.append(case('fclose_fails', [True, True, True], result=0xffffffff))


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in CASES:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(CASES)} synthetic cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
