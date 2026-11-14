#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the resource manager's release of a resource info
(game/loading/resource_manager_release.cpp), for tools/verify.py run.

usage: resource_manager_release_cases.py OUT_DIR

Each case gives a manager buffer (its flags byte, the lock object at +0x648 whose vtable's lock
and unlock slots are scripted stubs) and an info buffer (flags at +0x31, the file load queue at
+0x68, null or a fake queue). The error report, the file queue's removal (0x02a6af30), the
condition signal's set (0x0247e780) and the manager's next step (0x024c3260) are scripted stubs.
"""
import json
import os
import sys

ADDRESS = '0x024c1530'
FATAL_ERROR, QUEUE_REMOVE, SIGNAL_SET, REMOVE_RELEASED = '0x024b55b0', '0x02a6af30', '0x0247e780', '0x024c3260'
LOCK, UNLOCK = '0x00500200', '0x00500300'
MANAGER, LOCK_OFFSET, INFO = 0x6e8, 0x648, 0x98
FLAGS, FILE_QUEUE = 0x31, 0x68


def case(cid, manager_flags=1, info_flags=0, queue=True, fill=0x5a):
    manager = bytearray([fill]) * MANAGER
    manager[0] = manager_flags
    info = bytearray([fill ^ 0xff]) * INFO
    info[FLAGS] = info_flags
    buffers = {'manager': {'size': MANAGER, 'bytes': manager.hex()},
               'info': {'size': INFO, 'bytes': info.hex()},
               'lock_vtable': {'size': 0x30}, 'queue': {'size': 0x10}}
    memory = [{'addr': f'buf:manager+{LOCK_OFFSET}', 'pointer': 'lock_vtable+0'},
              {'addr': 'buf:lock_vtable+24', 'guest': LOCK},
              {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK},
              {'addr': f'buf:info+{FILE_QUEUE}', **({'pointer': 'queue+0'} if queue else {'bytes': '00' * 8})}]
    stubs = []
    if not manager_flags & 1:
        stubs.append({'address': FATAL_ERROR, 'argc': 3, 'argmem': [{'arg': 2, 'size': 16}]})
    if not info_flags & 0x40:
        if queue:
            stubs.append({'address': QUEUE_REMOVE, 'argc': 2})
        stubs.append({'address': SIGNAL_SET, 'argc': 1, 'argu8': [1], 'ret': '0x1234'})
    stubs += [{'address': LOCK, 'argc': 1, 'argu32': [1], 'argmem': [{'arg': 0, 'size': 8}]},
              {'address': REMOVE_RELEASED, 'argc': 2, 'argmem': [{'arg': 1, 'offset': FLAGS, 'size': 1}], 'ret': '0x1'},
              {'address': UNLOCK, 'argc': 1}]
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:manager', 'rsi': 'buf:info'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def cases():
    return [
        case('waiting_in_queue'),
        case('no_queue', queue=False),
        case('flag_0x40_skips_queue', info_flags=0x40),
        case('flag_0x40_no_queue', info_flags=0x40, queue=False),
        case('already_released', info_flags=0x80),
        case('all_other_flags', info_flags=0xbf, fill=0),
        case('all_flags', info_flags=0xff, fill=0xff),
        case('not_initialized', manager_flags=0),
        case('not_initialized_flag_0x40', manager_flags=0xfe, info_flags=0x40),
        case('initialized_other_bits', manager_flags=0xff, info_flags=0x3f),
        case('initialized_bit_only', manager_flags=0x01, info_flags=0x10, queue=False),
    ]


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
