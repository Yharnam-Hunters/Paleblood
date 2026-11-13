#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the file load queue's dispatch and its two starts of a file task
(game/loading/file_load_queue.cpp), for tools/verify.py run.

usage: file_load_queue_cases.py OUT_DIR MODE

- start (0x02a6a9e0): a queue (manager +0x10, transfer task manager +0x18), an event (its info word
  and request) and the transfer tasks: their lock (+0x108) and the file task pool (+0x120) with its
  lock, heap, blocks and free list, all locks with fake vtables. With a free slot it is taken;
  without one a new block (0x6708 bytes) comes from the allocation stub (0x0247c4e0), the clear
  (0x02fbe688) and the slots' constructor (0x02a6b520) are stubs. The file task's initialization
  (0x024d6730) and the list's push_back (0x02a6b340) are stubs. Not covered: no transfer tasks or
  a failed block allocation, after which the original faults (a null pointer) before reporting.
- start_no_buffer (0x02a6ad50): the same cases; the initialization's flag argument is 1.
- dispatch (0x02a6a7f0): a queue (passed at an address that may not be 8-aligned) whose events (a
  pattern above each kind's low 32 bits) have kinds 0 to 5; the three handlers (0x02a6a9e0,
  0x02a6abc0, 0x02a6ad50), the assertion (0x024b55b0) and the erase (0x02a6b6b0, compared on the
  array and the two iterators' element pointers; the iterators' other words are left unset by the
  original) are stubs.
"""
import json
import os
import struct
import sys

ADDRESSES = {'start': '0x02a6a9e0', 'start_no_buffer': '0x02a6ad50', 'dispatch': '0x02a6a7f0'}
FATAL, ALLOCATE, MEMSET, SLOTS = '0x024b55b0', '0x0247c4e0', '0x02fbe688', '0x02a6b520'
INIT, PUSH_BACK = '0x024d6730', '0x02a6b340'
START, KIND_3, KIND_4, ERASE = '0x02a6a9e0', '0x02a6abc0', '0x02a6ad50', '0x02a6b6b0'
LOCK, UNLOCK = '0x00500200', '0x00500240'
EVENT = 0x158
MASK = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


class Case:
    def __init__(self, mode, cid, returns='void'):
        self.mode, self.cid, self.returns = mode, cid, returns
        self.raw, self.memory, self.stubs, self.args = {'lock_vtable': bytearray(0x30)}, [], [], {}
        self.memory += [{'addr': 'buf:lock_vtable+24', 'guest': LOCK}, {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]

    def stub(self, address, argc, **fields):
        self.stubs.append({'address': address, 'argc': argc, **fields})

    def at(self, place, target=None, value=None):
        if target is not None:
            self.memory.append({'addr': f'buf:{place}', 'pointer': target})
        else:
            self.memory.append({'addr': f'buf:{place}', 'bytes': u64(value)})

    def lock(self):
        self.stub(LOCK, 1, argu32=[1])

    def json(self):
        return {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': self.returns,
                'args': self.args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
                'memory': self.memory, 'imports': [], 'stubs': self.stubs}


def start(cid, free=1, initialized=True, blocks=True, mode='start'):
    """free: how many free slots are chained (0 makes a new block)."""
    c = Case(mode, cid)
    c.raw.update({'queue': bytearray([0x51]) * 0x30, 'event': bytearray([0x45]) * EVENT,
                  'tasks': bytearray([0x54]) * 0x128, 'pool': bytearray([0x50]) * 0x30, 'heap': bytearray(0x10),
                  'manager': bytearray(0x10), 'task_manager': bytearray(0x10), 'info': bytearray(0x20)})
    c.at('queue+16', 'manager+0')
    c.at('queue+24', 'task_manager+0')
    c.at('event+8', 'info+0')
    c.at('tasks+264', 'lock_vtable+0')
    c.at('tasks+288', 'pool+0')
    c.at('pool+0', 'lock_vtable+0')
    c.at('pool+24', 'heap+0')
    if blocks:
        c.raw['old_block'] = bytearray(0x10)
        c.at('pool+32', 'old_block+0')
    else:
        c.at('pool+32', value=0)
    c.args.update({'rdi': 'buf:queue', 'rsi': 'buf:event', 'rdx': 'buf:tasks'})
    c.lock()
    if free:
        for i in range(free):
            c.raw[f'slot{i}'] = bytearray([0x60 + i]) * 0x338
            if i + 1 < free:
                c.at(f'slot{i}+816', f'slot{i + 1}+0')
            else:
                c.at(f'slot{i}+816', value=0)
        c.at('pool+40', 'slot0+0')
    else:
        c.at('pool+40', value=0)
        c.raw['block'] = bytearray([0x42]) * 0x6708
        c.stub(ALLOCATE, 3, ret='buf:block')
        c.stub(MEMSET, 3)
        c.stub(SLOTS, 1)
    c.stub(UNLOCK, 1)
    c.stub(INIT, 6, ret='0x1' if initialized else '0x0', argu8=[6])
    if initialized:
        c.lock()
        c.stub(PUSH_BACK, 2, argmem=[{'arg': 1, 'size': 8}])
        c.stub(UNLOCK, 1)
    return c


def dispatch(cid, kinds, skew=0, empty_tasks=False):
    c = Case('dispatch', cid)
    queue = bytearray([0x51]) * (0xa8 + skew)
    c.raw.update({'queue': queue, 'events': bytearray([0x45]) * max(1, len(kinds)) * EVENT})
    if not empty_tasks:
        c.raw['tasks'] = bytearray(0x10)
        c.at(f'queue+{skew + 0x28}', 'tasks+0')
    else:
        c.at(f'queue+{skew + 0x28}', value=0)
    c.at(f'queue+{skew + 0xa0}', 'lock_vtable+0')
    array = (skew + 0x70 + 7) & ~7
    c.at(f'queue+{array + 8}', 'events+0')
    c.at(f'queue+{array + 16}', f'events+{len(kinds) * EVENT}')
    for i, kind in enumerate(kinds):
        c.memory.append({'addr': f'buf:events+{i * EVENT}', 'bytes': struct.pack('<I', kind & 0xffffffff).hex()})
    c.args['rdi'] = f'buf:queue+{skew}' if skew else 'buf:queue'
    c.lock()
    for kind in kinds:
        if kind > 4 or kind < 0:
            c.stub(FATAL, 3)
        elif kind == 0:
            c.stub(START, 3)
        elif kind == 3:
            c.stub(KIND_3, 3, argi=[1, 2])
        elif kind == 4:
            c.stub(KIND_4, 3)
    c.stub(ERASE, 2, argi=[0, 1, 8, 11])
    c.stub(UNLOCK, 1)
    return c


def cases(mode):
    if mode in ('start', 'start_no_buffer'):
        return [start('one_free', mode=mode), start('two_free', free=2, mode=mode),
                start('three_free_not_initialized', free=3, initialized=False, mode=mode),
                start('new_block', free=0, mode=mode), start('new_block_no_blocks', free=0, blocks=False, mode=mode),
                start('new_block_not_initialized', free=0, initialized=False, mode=mode)]
    return [dispatch('empty', []), dispatch('one_each', [0, 1, 2, 3, 4]), dispatch('invalid_5', [5]),
            dispatch('invalid_negative', [-1, 0]), dispatch('mixed', [4, 4, 0, 3, 2, 1, 0, 7, 3]),
            dispatch('skew3', [0, 3], skew=3), dispatch('skew5_empty', [], skew=5),
            dispatch('skew1_no_tasks', [0, 4], skew=1, empty_tasks=True), dispatch('big_kind', [0x100000000 >> 1, 1])]


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
