#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for building the file load event queue's slots (game/loading/file_load_slots.cpp),
for tools/verify.py run.

usage: file_load_slots_cases.py OUT_DIR

Each case gives the 32-slot array (0x338 bytes a slot) synthetic contents; the slot constructor
(0x024d63f0) is a scripted stub, called once per slot with that slot's address, and in some cases
writes into the slot as the real constructor does.
"""
import json
import os
import sys

ADDRESS, SLOT_CONSTRUCT = '0x02a6b520', '0x024d63f0'
SLOT, SLOTS = 0x338, 32


def case(cid, fill, constructor_writes=False):
    stubs = []
    for i in range(SLOTS):
        stub = {'address': SLOT_CONSTRUCT, 'argc': 1, 'argmem': [{'arg': 0, 'size': 16}]}
        if constructor_writes:
            stub['writes'] = [{'arg': 0, 'offset': 0, 'bytes': (bytes([i]) * 8).hex()},
                              {'arg': 0, 'offset': SLOT, 'bytes': 'ee' * 8}]
        stubs.append(stub)
    size = SLOT * SLOTS + 8
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:slots'},
            'buffers': {'slots': {'size': size, 'bytes': (bytes([fill]) * size).hex()}},
            'stubs': stubs}


def cases():
    return [case('zeroed', 0), case('filled', 0xa5), case('constructor_writes', 0x11, True)]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases()
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
