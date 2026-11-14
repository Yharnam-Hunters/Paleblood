#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the finished entries, their queue's post and the queue's report
(game/loading/finished_entry.cpp), for tools/verify.py run.

usage: finished_entry_cases.py OUT_DIR MODE

- construct (0x02aa4a30): an entry filled with a pattern, four request words, a code, a word and a
  manager. The result base's constructor (0x024bcfa0), the manager's lookup (0x024c1630, compared
  with the four words it takes by value on the stack; it returns an info or null) and the handle
  constructor (0x024bd470) are scripted stubs.
- copy (0x02aa4ab0): an entry and a source; the base copy (0x024bcfe0) and the handle assign
  (0x024bd310) are stubs.
- destroy (0x02aa4b40): the handle destructor (0x024bd4c0) is a stub; the base destructor
  is empty and runs as it is.
- post (0x02a6b0e0): a queue (passed at an address that may not be 8-aligned, so the list's
  alignment matters) with its manager at +0x10 and its lock at +0x58 (a fake vtable); the entry
  constructor and destructor and the list's push_back (0x02a6b1a0) are stubs.
- report (0x02a692d0): a queue whose first vtable slot is a stub, request words and a word.
"""
import json
import os
import struct
import sys

ADDRESSES = {'construct': '0x02aa4a30', 'copy': '0x02aa4ab0', 'destroy': '0x02aa4b40', 'post': '0x02a6b0e0',
             'report': '0x02a692d0'}
RESULT_CONSTRUCT, RESULT_COPY = '0x024bcfa0', '0x024bcfe0'
FIND_INFO, HANDLE_CONSTRUCT, HANDLE_DESTROY, HANDLE_ASSIGN = '0x024c1630', '0x024bd470', '0x024bd4c0', '0x024bd310'
ENTRY_CONSTRUCT, ENTRY_DESTROY, PUSH_BACK = '0x02aa4a30', '0x02aa4b40', '0x02a6b1a0'
LOCK, UNLOCK, REPORT_SLOT = '0x00500200', '0x00500240', '0x00500280'
MASK = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


class Case:
    def __init__(self, mode, cid, returns='void'):
        self.mode, self.cid, self.returns = mode, cid, returns
        self.raw, self.memory, self.stubs, self.args = {}, [], [], {}

    def stub(self, address, argc, **fields):
        self.stubs.append({'address': address, 'argc': argc, **fields})

    def json(self):
        return {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': self.returns,
                'args': self.args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
                'memory': self.memory, 'imports': [], 'stubs': self.stubs}


def words(seed):
    return bytearray(b''.join(struct.pack('<Q', (seed * 0x0101010101010101 + i) & MASK) for i in range(4)))


def construct(cid, code=1, word=0x2020, found=True, seed=0x11):
    c = Case('construct', cid)
    c.raw.update({'entry': bytearray([0xa5]) * 0x48, 'words': words(seed), 'manager': bytearray(0x10),
                  'info': bytearray(0x20)})
    c.args.update({'rdi': 'buf:entry', 'rsi': 'buf:words', 'rdx': hex(code & 0xffffffff), 'rcx': hex(word & MASK),
                   'r8': 'buf:manager'})
    c.stub(RESULT_CONSTRUCT, 4, argi=[0, 1, 3], argu32=[2])
    c.stub(FIND_INFO, 1, argi=[0, 6, 7, 8, 9], ret='buf:info' if found else '0x0')
    c.stub(HANDLE_CONSTRUCT, 2)
    return c


def copy(cid):
    c = Case('copy', cid)
    c.raw.update({'entry': bytearray([0xa5]) * 0x48, 'source': bytearray([0x5a]) * 0x48})
    c.args.update({'rdi': 'buf:entry', 'rsi': 'buf:source'})
    c.stub(RESULT_COPY, 2)
    c.stub(HANDLE_ASSIGN, 2)
    return c


def destroy(cid, fill=0xa5):
    c = Case('destroy', cid)
    c.raw['entry'] = bytearray([fill]) * 0x48
    c.args['rdi'] = 'buf:entry'
    c.stub(HANDLE_DESTROY, 1)
    return c


def post(cid, skew=0, code=-0xf8, word=0x2020):
    c = Case('post', cid, returns='i8')
    c.raw.update({'queue': bytearray([0x51]) * 0x70, 'lock_vtable': bytearray(0x30), 'words': words(0x22),
                  'manager': bytearray(0x10)})
    c.memory += [{'addr': f'buf:queue+{skew + 0x10}', 'pointer': 'manager+0'},
                 {'addr': f'buf:queue+{skew + 0x58}', 'pointer': 'lock_vtable+0'},
                 {'addr': 'buf:lock_vtable+24', 'guest': LOCK}, {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]
    c.args.update({'rdi': f'buf:queue+{skew}' if skew else 'buf:queue', 'rsi': 'buf:words',
                   'rdx': hex(code & 0xffffffff), 'rcx': hex(word & MASK)})
    c.stub(LOCK, 1, argu32=[1])
    c.stub(ENTRY_CONSTRUCT, 5, argi=[0, 1, 3, 4], argu32=[2])
    c.stub(PUSH_BACK, 2)
    c.stub(ENTRY_DESTROY, 1)
    c.stub(UNLOCK, 1)
    return c


def report(cid, word):
    c = Case('report', cid)
    c.raw.update({'queue': bytearray(0x10), 'vtable': bytearray(8), 'words': words(0x33)})
    c.memory += [{'addr': 'buf:queue+0', 'pointer': 'vtable+0'}, {'addr': 'buf:vtable+0', 'guest': REPORT_SLOT}]
    c.args.update({'rdi': 'buf:queue', 'rsi': 'buf:words', 'rdx': hex(word & MASK), 'rcx': '0x7777'})
    c.stub(REPORT_SLOT, 4)
    return c


def cases(mode):
    if mode == 'construct':
        return [construct('found'), construct('not_found', found=False), construct('negative_code', code=-0xfa),
                construct('code_zero_word_max', code=0, word=MASK), construct('other_words', seed=0x7f, code=0x7fffffff)]
    if mode == 'copy':
        return [copy('copy')]
    if mode == 'destroy':
        return [destroy('pattern'), destroy('zero', fill=0)]
    if mode == 'post':
        return [post(f'skew{skew}', skew=skew) for skew in range(8)] + [post('code1', code=1, word=0),
                                                                        post('code_max', code=-1, word=MASK)]
    return [report('word', 0x2020), report('zero', 0), report('max', MASK)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases(sys.argv[2])
    for c in generated:
        with open(os.path.join(out, f'{c.cid}.json'), 'w') as f:
            json.dump(c.json(), f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
