#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the item select dialog's direction readers (game/menu/gaitem_select_direction.cpp),
for tools/verify.py run.

usage: gaitem_select_direction_cases.py OUT_DIR keys_20|keys_24

The key assign's key check (0x01391c60) is a scripted stub, compared with the key assign pointer
and the key, answering each call in turn. Every combination of the four answers is covered up to
the first held key, with held answers 1, 2 and 0xff (any nonzero byte is held; right's answer
becomes x as it is) and junk in the upper bits of the answer register. keys_24 also runs without
a key assign (null): up and down are skipped and left and right are asked with a null pointer.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import sys

ADDRESSES = {'keys_20': '0x01ee6080', 'keys_24': '0x01ee61f0'}
FIRST_KEY = {'keys_20': 0x20, 'keys_24': 0x24}
KEY_HELD = '0x01391c60'
KEYS = 0x00007b0000002000  # the key assign: only passed on to the stubbed check


def case(variant, cid, answers, null_keys=False):
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'i64',
         'buffers': {'holder': {'size': 8, 'bytes': ('00' * 8) if null_keys else KEYS.to_bytes(8, 'little').hex()},
                     'reader': {'size': 0x10, 'bytes': '3c' * 0x10}},
         'args': {'rdi': 'buf:reader', 'rsi': 'buf:holder'}, 'memory': [], 'imports': [], 'stubs': []}
    for answer in answers:
        c['stubs'].append({'address': KEY_HELD, 'argc': 2, 'argi': [0], 'argu32': [1], 'ret': hex(answer)})
    return c


def cases(variant):
    out = []
    # answers for up, down, left, right; the reader stops at the first held key.
    for held in (1, 2, 0xff, 0xffffff01):
        tag = f'{held:x}'
        out.append(case(variant, f'up_{tag}', [held]))
        out.append(case(variant, f'down_{tag}', [0, held]))
        out.append(case(variant, f'left_{tag}', [0, 0, held]))
        out.append(case(variant, f'right_{tag}', [0, 0, 0, held]))
    out.append(case(variant, 'nothing_held', [0, 0, 0, 0]))
    out.append(case(variant, 'nothing_held_upper_junk', [0xffffff00, 0x1200, 0xab00, 0x7f00]))
    if variant == 'keys_24':
        for held in (1, 2, 0xff):
            out.append(case(variant, f'no_key_assign_left_{held:x}', [held], null_keys=True))
            out.append(case(variant, f'no_key_assign_right_{held:x}', [0, held], null_keys=True))
        out.append(case(variant, 'no_key_assign_nothing_held', [0, 0], null_keys=True))
    return out


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
