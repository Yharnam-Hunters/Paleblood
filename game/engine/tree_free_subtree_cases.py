#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the copies of the tree subtree release (game/engine/tree_free_subtree.cpp), for
tools/verify.py run: the cases of the set of 64-bit keys (set_free_subtree_cases.py, which
describes them), aimed at one copy.

usage: tree_free_subtree_cases.py OUT_DIR ADDRESS   (one of the 104 copies, e.g. 0x016b7550)

The release reads a node's links and nil flag and the tree's allocator, which every copy keeps in
the same places, and nothing else. So the set's 0x28-byte nodes stand for the nodes of any copy,
whatever it keeps after the links.
"""
import json
import os
import sys

from set_free_subtree_cases import CASES

COPIES = [
    '0x016b7550', '0x016dfc80', '0x016dfce0', '0x016f1c10', '0x017c4d20', '0x017c8f00',
    '0x017d22c0', '0x017dd280', '0x018904d0', '0x01900d00', '0x01957700', '0x01957760',
    '0x01964c70', '0x01995650', '0x019b7410', '0x01bd2650', '0x01c6d380', '0x01c6d3e0',
    '0x01c7cab0', '0x01c7cb10', '0x01c7cb70', '0x01c93820', '0x01db3a20', '0x01db3de0',
    '0x01dcfbf0', '0x01dcfc50', '0x01e20ec0', '0x01e20f20', '0x01e20f80', '0x01e20fe0',
    '0x01e21040', '0x01ea2860', '0x020b7160', '0x020c4700', '0x020f01b0', '0x020f2e20',
    '0x02112df0', '0x021d4ee0', '0x022ae740', '0x022aee90', '0x023261a0', '0x0232c8d0',
    '0x0232f6d0', '0x02366060', '0x023660c0', '0x0236f1f0', '0x0237d410', '0x0237dda0',
    '0x023fa1b0', '0x02455e80', '0x0248fa70', '0x02492ad0', '0x024a8360', '0x024d3200',
    '0x02500e20', '0x02537970', '0x025379d0', '0x0255af40', '0x02622d30', '0x0265c090',
    '0x0265d100', '0x0268d060', '0x026f4500', '0x0277bd00', '0x0277bd60', '0x0277c950',
    '0x0277d500', '0x02a4f1b0', '0x02a50620', '0x02a50680', '0x02a506e0', '0x02a73900',
    '0x02a74300', '0x02b30bf0', '0x02bbb6c0', '0x02bc9ea0', '0x02be6b80', '0x02beec40',
    '0x02bef8f0', '0x02c26b50', '0x02c27500', '0x02d2c540', '0x02d65fa0', '0x02d67d90',
    '0x02d75dc0', '0x02d78fc0', '0x02d79a20', '0x02d7a4f0', '0x02d83c30', '0x02d84600',
    '0x02d852f0', '0x02da03d0', '0x02daa780', '0x02db1dc0', '0x02db1e20', '0x02db4240',
    '0x02dbb9b0', '0x02dbd250', '0x02dcf0b0', '0x02dd88f0', '0x02ddef00', '0x02e5f850',
    '0x02f0ab00', '0x02f81de0',
]


def cases(address):
    """The set's cases with the copy at `address` as the function under test."""
    return [dict(fixture, address=address) for fixture in CASES]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in COPIES:
        print(__doc__, file=sys.stderr)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    fixtures = cases(sys.argv[2])
    for fixture in fixtures:
        with open(os.path.join(out, fixture['id'] + '.json'), 'w') as stream:
            json.dump(fixture, stream, indent=1)
            stream.write('\n')
    print(f'{len(fixtures)} synthetic cases written to {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
