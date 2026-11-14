#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for Dantelion's text input dialog object (game/menu/system_text_dialog.cpp), for
tools/verify.py run.

usage: system_text_dialog_cases.py OUT_DIR set_up|open|status|take_text

Writes OUT_DIR/*.json for one function. The dialog object (0xd0 bytes) is filled with a pattern
and its open flag set per case; the dialog backend singleton (0x05acd098) is set or null (then its
creation, 0x02d03720, is stubbed and returns a backend), and its vtable points its status, text and
open slots at stubbed fake addresses. Every callee also gets a spare scripted call, so a call that
should not happen is recorded rather than run unseen.
- set_up (menu_system_text_dialog_set_up_02d03500): closed (the settings copied) and open (nothing).
- open (menu_system_text_dialog_open_02d03450): already open; closed with the backend opening or
  refusing, with the backend present or made on first use.
- status (menu_system_text_dialog_status_02d03420): closed (-1); open with the backend reporting
  0, 1, 2, 3, -1, 6 and the most negative status, the backend present or made.
- take_text (menu_system_text_dialog_take_text_02d03490): closed; open with a status of 0, 1, -1,
  -2, 6 and 0x80000002 (not finished: nothing changes), and 2 and 3 (the object closed and the
  text asked for, the backend answering true or false), the backend present or made (twice).
"""
import json
import os
import struct
import sys

ADDRESSES = {'set_up': '0x02d03500', 'open': '0x02d03450', 'status': '0x02d03420', 'take_text': '0x02d03490'}
FUNCTIONS = {k: f'menu_system_text_dialog_{k}_{v[2:]}' for k, v in ADDRESSES.items()}
RETURNS = {'set_up': 'i8', 'open': 'i8', 'status': 'i32', 'take_text': 'i8'}
INSTANCE, CREATE, ASSIGN = '0x05acd098', '0x02d03720', '0x02d03520'
STATUS, TAKE_TEXT, OPEN = '0x00500200', '0x00500300', '0x00500400'   # spare addresses for the vtable slots
SIZE = 0xd0


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


class Case:
    def __init__(self, mode, cid, is_open, backend=True):
        self.mode = mode
        self.c = {'schema': 1, 'address': ADDRESSES[mode], 'id': cid, 'returns': RETURNS[mode],
                  'args': {'rdi': 'buf:dialog'},
                  'buffers': {'dialog': {'size': SIZE, 'bytes': '5c' * SIZE},
                              'settings': {'size': 0xd0, 'bytes': '3a' * 0xd0},
                              'backend': {'size': 0x20, 'bytes': 'c6' * 0x20},
                              'vtable': {'size': 0x30, 'bytes': '6c' * 0x30},
                              'text': {'size': 0x38, 'bytes': '7e' * 0x38}},
                  'memory': [{'addr': 'buf:dialog', 'bytes': '01' if is_open else '00'},
                             {'addr': 'buf:backend', 'pointer': 'vtable'},
                             {'addr': 'buf:vtable+0x10', 'guest': STATUS},
                             {'addr': 'buf:vtable+0x18', 'guest': TAKE_TEXT},
                             {'addr': 'buf:vtable+0x20', 'guest': OPEN},
                             {'addr': INSTANCE, 'pointer': 'backend'} if backend else
                             {'addr': INSTANCE, 'bytes': q(0)}],
                  'imports': [], 'stubs': []}
        self.backend = backend

    def get_backend(self):
        if not self.backend:
            self.c['stubs'].append({'address': CREATE, 'argc': 0, 'ret': 'buf:backend'})

    def call(self, address, ret, argc=1, **kw):
        s = {'address': address, 'argc': argc, 'ret': ret}
        s.update(kw)
        self.c['stubs'].append(s)

    def done(self):
        for address, argc in ((CREATE, 0), (ASSIGN, 2), (STATUS, 1), (TAKE_TEXT, 2), (OPEN, 2)):
            self.c['stubs'].append({'address': address, 'argc': argc, 'ret': 0})
        return self.c


def set_up_cases():
    out = []
    for is_open in (False, True):
        c = Case('set_up', f'{"open" if is_open else "closed"}', is_open)
        c.c['args']['rsi'] = 'buf:settings'
        if not is_open:
            c.call(ASSIGN, 0, argc=2)
        out.append(c.done())
    c = Case('set_up', 'closed_null_settings', False)
    c.c['args']['rsi'] = 0
    c.call(ASSIGN, 0, argc=2)
    out.append(c.done())
    return out


def open_cases():
    out = []
    out.append(Case('open', 'already_open', True).done())
    for backend in (True, False):
        for answer in (1, 0):
            c = Case('open', f'{"backend" if backend else "made"}_answer_{answer}', False, backend)
            c.get_backend()
            c.call(OPEN, answer, argc=2)
            out.append(c.done())
    return out


def status_cases():
    out = []
    out.append(Case('status', 'closed', False).done())
    for backend in (True, False):
        for status in (0, 1, 2, 3, -1, 6, -0x80000000):
            c = Case('status', f'{"backend" if backend else "made"}_status_{status & 0xffffffff:x}', True, backend)
            c.get_backend()
            c.call(STATUS, status & 0xffffffff)
            out.append(c.done())
    return out


def take_text_cases():
    out = []

    def add(cid, is_open=True, backend=True, status=None, answer=None):
        c = Case('take_text', cid, is_open, backend)
        c.c['args']['rsi'] = 'buf:text'
        if is_open:
            c.get_backend()
            c.call(STATUS, status & 0xffffffff)
            if answer is not None:
                c.get_backend()
                c.call(TAKE_TEXT, answer, argc=2)
        out.append(c.done())

    add('closed', is_open=False)
    for status in (0, 1, -1, -2, 6, 0x80000002, 0x80000003):
        add(f'not_finished_{status & 0xffffffff:x}', status=status)
    for backend in (True, False):
        for status in (2, 3):
            for answer in (1, 0):
                add(f'{"backend" if backend else "made"}_status_{status}_answer_{answer}', backend=backend,
                    status=status, answer=answer)
    add('made_not_finished', backend=False, status=1)
    return out


GENERATORS = {'set_up': set_up_cases, 'open': open_cases, 'status': status_cases, 'take_text': take_text_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in GENERATORS:
        sys.exit(__doc__)
    out_dir, mode = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    cases = GENERATORS[mode]()
    for c in cases:
        with open(os.path.join(out_dir, c['id'] + '.json'), 'w') as f:
            json.dump(c, f, indent=1)
            f.write('\n')
    print(f'{len(cases)} cases written to {out_dir}')


if __name__ == '__main__':
    main()
