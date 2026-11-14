#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the task manager's task add and debug menu entry
(game/frame_timing/task_registration.cpp), for tools/verify.py run.

usage: task_registration_cases.py OUT_DIR add|debug_menu

- add: both wait objects set (state 2, handed to the wait call, flags untouched); only the first
  or only the second set, and neither (queued); groups 0, 3, 7 and 9 (three bits kept, the queue
  index unmasked); flags bytes 0x00, 0xff and 0xe3 around the group bits, and nonzero bytes after
  them (+0x7a to +0x7c, which the original rewrites unchanged); with and without a task to order
  after.
- debug_menu: no parent; a menu node already there; a new node; the node call returning null
  (the rest still runs on it).
The queue, wait and debug menu calls are scripted stubs; the original's last call, to an empty
leaf family member, runs as the game's own code. Generated fixtures, not recorded inputs.
"""
import json
import os
import struct
import sys

ADDRESSES = {'add': '0x01381580', 'debug_menu': '0x013894f0'}
WAIT, QUEUE_ADD, ORDER_AFTER = '0x013e7170', '0x01435b80', '0x01435cc0'
ADD_NODE, ADD_SWITCH = '0x0140a410', '0x01407f60'


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def add_case(cid, group=3, first=False, second=False, after=False, flags=0x00, tail=b'\x00\x00\x00'):
    c = base('add', cid)
    manager = bytearray(b'\x4d' * (0xd8 + 8 * 10))
    c['buffers']['manager'] = {'size': len(manager), 'bytes': manager.hex()}
    for g in range(10):
        c['buffers'][f'queue{g}'] = {'size': 0x10, 'bytes': f'{g:02x}' * 0x10}
        c['memory'].append({'addr': f'buf:manager+{0xd8 + 8 * g}', 'pointer': f'queue{g}'})
    task = bytearray(b'\x7e' * 0x80)
    task[0x78], task[0x79] = 1, flags
    task[0x7a:0x7d] = tail
    c['buffers']['task'] = {'size': len(task), 'bytes': task.hex()}
    for name, wanted in (('first', first), ('second', second), ('after', after)):
        if wanted:
            c['buffers'][name] = {'size': 0x10, 'bytes': '00' * 0x10}
    if first and second:
        c['stubs'].append({'address': WAIT, 'ret': 0, 'argc': 3})
    else:
        c['stubs'].append({'address': QUEUE_ADD, 'ret': 0, 'argc': 2, 'argmem': [{'arg': 1, 'size': 0x80}]})
        if after:
            c['stubs'].append({'address': ORDER_AFTER, 'ret': 0, 'argc': 3})
    c['args'] = {'rdi': 'buf:manager', 'rsi': '0x5151515151515151', 'rdx': 'buf:task',
                 'rcx': 'buf:first' if first else 0, 'r8': 'buf:second' if second else 0, 'r9': f'{group:#x}'}
    c['stack'] = ['buf:after' if after else 0]
    return c


def add_cases():
    return [add_case('wait_both', first=True, second=True, after=True, flags=0xff),
            add_case('first_only', first=True), add_case('second_only', second=True), add_case('neither'),
            add_case('group_0_flags_ff', group=0, flags=0xff), add_case('group_7_flags_00', group=7),
            add_case('group_7_flags_e3', group=7, flags=0xe3), add_case('group_3_flags_1c', flags=0x1c),
            add_case('group_9_unmasked_queue', group=9, flags=0x5a), add_case('after_set', after=True),
            add_case('after_set_group_0', group=0, after=True, flags=0x80),
            add_case('tail_bytes_kept', flags=0x41, tail=b'\xaa\xbb\xcc'),
            add_case('wait_first_and_after_only', first=True, after=True)]


def menu_case(cid, parent=True, existing=False, node_null=False, byte=0x3c):
    c = base('debug_menu', cid)
    owner = bytearray([byte]) * 0x60
    struct.pack_into('<Q', owner, 0x58, 0)
    c['buffers']['owner'] = {'size': len(owner), 'bytes': owner.hex()}
    c['buffers']['manager'] = {'size': 0x10, 'bytes': '00' * 0x10}
    c['memory'].append({'addr': 'buf:owner+0x30', 'pointer': 'manager'})
    if existing:
        c['buffers']['old_node'] = {'size': 0x10, 'bytes': '00' * 0x10}
        c['memory'].append({'addr': 'buf:owner+0x58', 'pointer': 'old_node'})
    for name in ('parent', 'task_node', 'settings_node'):
        c['buffers'][name] = {'size': 0x10, 'bytes': '00' * 0x10}
    if parent and not existing:
        c['stubs'] += [{'address': ADD_NODE, 'ret': 0 if node_null else 'buf:task_node', 'argc': 2},
                       {'address': ADD_NODE, 'ret': 'buf:settings_node', 'argc': 2},
                       {'address': ADD_SWITCH, 'ret': 0, 'argc': 4, 'argu32': [3], 'argi': [0, 1, 2]}]
    c['args'] = {'rdi': 'buf:owner', 'rsi': 'buf:parent' if parent else 0}
    return c


def menu_cases():
    return [menu_case('no_parent', parent=False), menu_case('already_added', existing=True),
            menu_case('added'), menu_case('added_other_bytes', byte=0x00),
            menu_case('node_null', node_null=True), menu_case('no_parent_existing', parent=False, existing=True)]


def cases(variant):
    return add_cases() if variant == 'add' else menu_cases()


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
