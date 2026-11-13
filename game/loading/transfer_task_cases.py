#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the transfer tasks (game/loading/transfer_task.cpp), for tools/verify.py run.

usage: transfer_task_cases.py OUT_DIR MODE

Modes and what each case gives:
- initialize (0x024dd540): a task filled with a pattern, a manager whose two heap pointers sit at
  +0x5d0 aligned up to 8 (the manager may be passed at an address that is not 8-aligned), an info,
  a word and the request's flags byte. The disk device getter (0x0246be70) and its locate
  (0x0246e3d0, compared with the preferred flag it is given) are scripted stubs.
- file_initialize (0x024d6730): a task whose request copy's vtable has a scripted validity slot,
  a request (heap word, path pointer, flags byte, words +0x20 and +0x140), the object with the
  fallback heap word at +0x490 and the last argument on the stack. The request assignment
  (0x024be630, writing the heap word it copies) and the base initialization (0x024dd540, compared
  with its arguments) are scripted stubs.
- check_read (0x024dde50), check_close (0x024ddf20), file_check_read (0x024d6e20) and file_start
  (0x024d7460): a task with the status words, state, flags and cancel bits the case sets; the
  vtable slots (start, finish, close), the read object's destroy, the allocator lookup (0x0247b720)
  and free, the release of the owned object (0x02479710), the file task's prepare (0x024d8210)
  and read start (0x024d8470) and the assertion (0x024b55b0) are scripted stubs.
- update (0x024d6950): a file task with its lock (+0x2e8, scripted lock and unlock) and handler
  table entries written into the image at 0x057b4d70 (plain, virtual through a fake vtable, with
  a `this` adjustment); each handler is a scripted stub that writes the next state.
"""
import json
import os
import struct
import sys

ADDRESSES = {'initialize': '0x024dd540', 'file_initialize': '0x024d6730', 'check_read': '0x024dde50',
             'check_close': '0x024ddf20', 'file_check_read': '0x024d6e20', 'file_start': '0x024d7460',
             'update': '0x024d6950'}
DEVICE, LOCATE, ASSIGN, INIT = '0x0246be70', '0x0246e3d0', '0x024be630', '0x024dd540'
ALLOCATOR_OF, RELEASE, PREPARE, BEGIN_READ, FATAL = '0x0247b720', '0x02479710', '0x024d8210', '0x024d8470', '0x024b55b0'
VALID, START, FINISH, CLOSE, DESTROY, FREE = '0x00500200', '0x00500240', '0x00500280', '0x005002c0', '0x00500300', '0x00500340'
LOCK, UNLOCK = '0x00500380', '0x005003c0'
HANDLERS = 0x057b4d70
TASK = 0x2f0
MASK = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def put(buf, offset, fmt, value):
    struct.pack_into(fmt, buf, offset, value)


class Case:
    def __init__(self, mode, cid, returns='i8'):
        self.mode, self.cid, self.returns = mode, cid, returns
        self.raw = {'task': bytearray([0xa5]) * TASK}
        self.memory, self.stubs, self.args, self.stack = [], [], {'rdi': 'buf:task'}, None

    def stub(self, address, argc, ret=None, writes=None):
        s = {'address': address, 'argc': argc}
        if ret is not None:
            s['ret'] = ret
        if writes:
            s['writes'] = writes
        self.stubs.append(s)

    def json(self):
        c = {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': self.returns,
             'args': self.args,
             'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
             'memory': self.memory, 'imports': [], 'stubs': self.stubs}
        if self.stack is not None:
            c['stack'] = self.stack
        return c


def initialize(cid, flags=0xff, location=0, skew=0, word=0x1122334455667788):
    c = Case('initialize', cid)
    manager = bytearray([0x6d]) * 0x728
    slot = (skew + 0x5d0 + 7) & ~7
    c.raw.update({'manager': manager, 'info': bytearray([0x3c]) * 0x20, 'path': bytearray(0x38),
                  'device': bytearray(0x10), 'heap1': bytearray(0x10), 'heap2': bytearray(0x10)})
    c.memory += [{'addr': f'buf:manager+{slot}', 'pointer': 'heap1+0'},
                 {'addr': f'buf:manager+{slot + 8}', 'pointer': 'heap2+0'}]
    c.args.update({'rsi': 'buf:path', 'rdx': f'buf:manager+{skew}' if skew else 'buf:manager',
                   'rcx': 'buf:info', 'r8': hex(word), 'r9': hex(flags)})
    c.stub(DEVICE, 0, ret='buf:device')
    c.stub(LOCATE, 3, ret=hex(location & MASK))
    return c


def file_initialize(cid, heap_word=0x5000, old_word=0x7777, valid=True, flag=1, flags_byte=0x40,
                    request_flags=0xff):
    c = Case('file_initialize', cid)
    task = c.raw['task']
    put(task, 0x58, '<B', flags_byte)
    put(task, 0x198 + 0x28, '<Q', old_word)
    request = bytearray([0x52]) * 0x150
    put(request, 0x28, '<Q', heap_word)
    put(request, 0x40, '<B', request_flags)
    put(request, 0x20, '<Q', 0x2020202020)
    put(request, 0x140, '<Q', 0x1401401401)
    c.raw.update({'request': request, 'request_vtable': bytearray(0x18), 'queue': bytearray(0x10),
                  'heapsrc': bytearray([0x48]) * 0x498, 'manager': bytearray(0x10), 'info': bytearray(0x20),
                  'path': bytearray(0x38)})
    c.memory += [{'addr': 'buf:task+408', 'pointer': 'request_vtable+0'},
                 {'addr': 'buf:request_vtable+16', 'guest': VALID},
                 {'addr': 'buf:request+128', 'pointer': 'path+0'},
                 {'addr': 'buf:heapsrc+1168', 'bytes': u64(0x4904904904)}]
    c.args.update({'rsi': 'buf:queue', 'rdx': 'buf:heapsrc', 'rcx': 'buf:manager', 'r8': 'buf:info',
                   'r9': 'buf:request'})
    c.stack = [hex(flag)]
    c.stub(ASSIGN, 2, ret='buf:task', writes=[{'arg': 0, 'offset': 0x28, 'bytes': u64(heap_word)}])
    c.stub(VALID, 1, ret='0x1' if valid else '0x0')
    if valid:
        c.stub(INIT, 6, ret='0x1')
    return c


def task_case(mode, cid, state=0, read_status=0, open_status=0xe, close_status=0, cancel=0, result=0,
              read_object=False, chunk_flags=0, pending=0, left=0x1000, steps=(0, 0), ready=1,
              stopping=False, chunk_size=0, prepared=True):
    c = Case(mode, cid)
    t = c.raw['task']
    put(t, 0x60, '<i', state)
    put(t, 0x68, '<i', steps[0])
    put(t, 0x6c, '<i', steps[1])
    put(t, 0x70, '<i', read_status)
    put(t, 0x74, '<i', open_status)
    put(t, 0x78, '<i', close_status)
    put(t, 0x108, '<Q', chunk_size)
    put(t, 0x12a, '<H', chunk_flags)
    put(t, 0x130, '<Q', left)
    put(t, 0x138, '<Q', pending)
    put(t, 0x158, '<I', result)
    put(t, 0x15c, '<i', ready)
    put(t, 0x160, '<I', cancel)
    c.raw['vtable'] = bytearray(0x48)
    c.memory += [{'addr': 'buf:task+0', 'pointer': 'vtable+0'},
                 {'addr': 'buf:vtable+0', 'guest': START}, {'addr': 'buf:vtable+8', 'guest': FINISH},
                 {'addr': 'buf:vtable+64', 'guest': CLOSE}]
    if read_object:
        c.raw.update({'object': bytearray(0x10), 'object_vtable': bytearray(8), 'alloc': bytearray(8),
                      'alloc_vtable': bytearray(0x78)})
        c.memory += [{'addr': 'buf:task+256', 'pointer': 'object+0'},
                     {'addr': 'buf:object+0', 'pointer': 'object_vtable+0'},
                     {'addr': 'buf:object_vtable+0', 'guest': DESTROY},
                     {'addr': 'buf:alloc+0', 'pointer': 'alloc_vtable+0'},
                     {'addr': 'buf:alloc_vtable+112', 'guest': FREE}]
    else:
        c.memory.append({'addr': 'buf:task+256', 'bytes': u64(0)})
    if mode == 'file_start':
        c.raw['manager'] = bytearray(0x720)
        put(c.raw['manager'], 0x714, '<B', 0xff if stopping else 0xfe)
        c.memory.append({'addr': 'buf:task+24', 'pointer': 'manager+0'})
    return c


def shifts(state):
    return state in (4, 8, 10, 11)


def check_read_cases():
    out = []
    for state in (0, 3, 4, 5, 8, 10, 11):
        for cancel in (0, 1, 3):
            c = task_case('check_read', f'submitted_state{state}_cancel{cancel}', state=state, read_status=0xc,
                          cancel=cancel, result=0x40)
            if not cancel & 1 and shifts(state):
                c.stub(FATAL, 3)
            out.append(c)
    for status in (0x10, 0x11, 0x12, 0xe, 0, 0xf, 0x13, -1):
        out.append(task_case('check_read', f'open_status_{status & 0xffffffff:x}', state=3, read_status=0xd,
                             open_status=status, result=0x8001))
    return out


def check_close_cases():
    out = []
    for read_status in (0xd, 0xc):
        for obj in (False, True):
            for close in (4, 1):
                for cancel in (0, 1):
                    for result in (0, 4):
                        c = task_case('check_close', f'read{read_status:x}_obj{int(obj)}_close{close}_cancel{cancel}_res{result}',
                                      state=7, read_status=read_status, close_status=close, cancel=cancel,
                                      result=result | 0x100, read_object=obj)
                        if read_status == 0xd:
                            if obj:
                                c.stub(ALLOCATOR_OF, 1, ret='buf:alloc')
                                c.stub(DESTROY, 1)
                                c.stub(FREE, 2)
                            if close == 4 and not cancel:
                                c.stub(CLOSE, 1)
                            elif not result & 4:
                                c.stub(FINISH, 1)
                                c.stub(RELEASE, 1)
                        out.append(c)
    return out


def tail_variants():
    return [dict(read_object=False, chunk_flags=8, close_status=1), dict(read_object=True, chunk_flags=0, close_status=1),
            dict(read_object=True, chunk_flags=8, close_status=4), dict(read_object=True, chunk_flags=0xfff7, close_status=4),
            dict(read_object=True, chunk_flags=8, close_status=1)]


def file_check_read_cases():
    out = []
    for status in (0x10, 0x11, 0x12):
        out.append(task_case('file_check_read', f'open_error_{status:x}', open_status=status, state=5, cancel=1))
    out.append(task_case('file_check_read', 'cancelled', state=5, cancel=1))
    out.append(task_case('file_check_read', 'cancelled_3', state=5, cancel=3, open_status=0))
    for pending, steps in ((0, (0, 0)), (0x300, (0xb, 0xb)), (0x300, (0xb, 0)), (0x300, (0, 0xb)), (0x2000, (0xb, 0xb))):
        for result in (0, 0x80):
            for state in (5, 8, 4, 11):
                c = task_case('file_check_read', f'pending{pending:x}_steps{steps[0]:x}{steps[1]:x}_res{result:x}_state{state}',
                              state=state, pending=pending, steps=steps, result=result | 0x1000, open_status=0)
                proceeds = pending == 0 or steps == (0xb, 0xb)
                if proceeds and result & 0x80 and shifts(state):
                    c.stub(FATAL, 3)
                out.append(c)
    for i, tail in enumerate(tail_variants()):
        out.append(task_case('file_check_read', f'tail_{i}', state=5, pending=0, **tail))
    return out


def file_start_cases():
    out = [task_case('file_start', 'manager_stopping', stopping=True, cancel=1, state=3),
           task_case('file_start', 'cancelled', cancel=1, state=3),
           task_case('file_start', 'not_ready', ready=0, state=3)]
    c = task_case('file_start', 'not_prepared', state=3)
    c.stub(PREPARE, 1, ret='0x0')
    out.append(c)
    for chunk in (0, 0x8000):
        for state in (3, 4, 8, 10, 11, 6):
            c = task_case('file_start', f'start_chunk{chunk:x}_state{state}', state=state, chunk_size=chunk,
                          left=0x12345, ready=2)
            c.stub(PREPARE, 1, ret='0x1')
            c.stub(BEGIN_READ, 2)
            if shifts(state):
                c.stub(FATAL, 3)
            c.stub(START, 1)
            out.append(c)
    for i, tail in enumerate(tail_variants()):
        c = task_case('file_start', f'tail_{i}', state=3, **tail)
        c.stub(PREPARE, 1, ret='0x1')
        c.stub(BEGIN_READ, 2)
        c.stub(START, 1)
        out.append(c)
    return out


def update_case(cid, state, steps, adjust=0, virtual=False):
    """steps: the states the handlers write, in order; a handler runs for each state reached."""
    c = Case('update', cid)
    put(c.raw['task'], 0x60, '<i', state)
    c.raw.update({'lock_vtable': bytearray(0x30), 'handler_vtable': bytearray(0x40)})
    c.memory += [{'addr': 'buf:task+744', 'pointer': 'lock_vtable+0'},
                 {'addr': 'buf:lock_vtable+24', 'guest': LOCK}, {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]
    c.stub(LOCK, 2)
    if state in (9, 14):
        c.stub(FATAL, 3)
    current, used = state, set()
    for nxt in steps:
        handler = f'0x{0x00500400 + 0x40 * current:08x}'
        if current not in used:
            used.add(current)
            entry = HANDLERS + 16 * current
            if virtual:
                slot = 8 * len(used)   # each handler its own vtable slot
                c.memory += [{'addr': f'0x{entry:08x}', 'bytes': u64(slot + 1)},
                             {'addr': f'buf:task+{adjust}', 'pointer': 'handler_vtable+0'},
                             {'addr': f'buf:handler_vtable+{slot}', 'guest': handler}]
            else:
                c.memory.append({'addr': f'0x{entry:08x}', 'guest': handler})
            c.memory.append({'addr': f'0x{entry + 8:08x}', 'bytes': u64(adjust)})
        c.stub(handler, 1, writes=[{'arg': 0, 'offset': 0x60 - adjust, 'bytes': u32(nxt)}])
        if nxt == current or nxt == 0 or nxt in (9, 12, 13, 14):
            break
        current = nxt
    c.stub(UNLOCK, 1)
    return c


def update_cases():
    out = [update_case(f'stop_state_{s}', s, []) for s in (0, 9, 12, 13, 14)]
    out += [update_case('one_step_to_idle', 1, [0]), update_case('unchanged', 3, [3]),
            update_case('chain_to_9', 1, [2, 5, 9]), update_case('chain_to_12', 4, [8, 13 - 1]),
            update_case('chain_back_and_forth', 2, [6, 2, 6, 6]), update_case('to_14', 11, [14]),
            update_case('adjusted', 5, [7, 0], adjust=0x10), update_case('virtual', 6, [10, 10], virtual=True),
            update_case('virtual_adjusted', 7, [1, 13], adjust=0x20, virtual=True)]
    return out


def cases(mode):
    if mode == 'initialize':
        out = []
        for flags in (0xff, 0x00, 0x02, 0x03, 0xfd, 0xfe):
            for location in (0, 1):
                out.append(initialize(f'flags{flags:02x}_loc{location}', flags, location))
        out += [initialize('loc_2', 0, 2), initialize('loc_minus1', 0, -1), initialize('skew3', 0, 1, skew=3),
                initialize('skew7', 2, 0, skew=7), initialize('skew4_loc1', 0xff, 1, skew=4)]
        return out
    if mode == 'file_initialize':
        return [file_initialize('heap_word', heap_word=0x5000), file_initialize('no_heap_word', heap_word=0),
                file_initialize('invalid', valid=False), file_initialize('invalid_no_heap_word', heap_word=0, valid=False),
                file_initialize('flag0', flag=0, flags_byte=0xff), file_initialize('flag1_byte_odd', flag=1, flags_byte=0x81),
                file_initialize('flag_wide', flag=0x1ff, flags_byte=0x10), file_initialize('request_flags_02', request_flags=0x02),
                file_initialize('no_heap_word_old_zero', heap_word=0, old_word=0)]
    return {'check_read': check_read_cases, 'check_close': check_close_cases, 'file_check_read': file_check_read_cases,
            'file_start': file_start_cases, 'update': update_cases}[mode]()


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
