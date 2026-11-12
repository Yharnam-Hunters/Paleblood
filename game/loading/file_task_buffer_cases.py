#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the file task's buffer, end, reset and the cancel (game/loading/file_task_buffer.cpp),
for tools/verify.py run.

usage: file_task_buffer_cases.py OUT_DIR MODE

Every case gives a file task (0x338 bytes, filled with a pattern, then the fields the case sets).
- prepare (0x024d8210): an old buffer (owned or not) with its allocator, the manager (+0x18) with
  or without its two buffer callbacks, the no-buffer flag, the path (inline or on the heap), the
  close status, the chunk and result flags, the read size, the task's allocator (+0x08), the
  request's alignment word and a dependent task. The allocator's allocate and free, both callbacks,
  the path check (0x02a34320) and the assertion (0x024b55b0) are scripted stubs; the empty check
  the original asks first runs as it is and returns false.
- end (0x024d7e50): the result flags, the info, the cancel bits, the no-buffer flag, the queue and
  the request's path word. The resource manager's lock (a fake vtable written into the static at
  0x05aa4590 + 0x648), its hand-over (0x024c3260), the queue's report (0x02a692d0) and the
  assertion are scripted stubs.
- reset (0x024d6850): the info and result flags (the end slot, vtable +0x58, is a stub) and a
  chain of 0 to 3 objects, each destroyed by a stub and freed through the given allocator's stub.
- cancel (0x024dd8a0): the cancel bits and the argument.
"""
import json
import os
import struct
import sys

ADDRESSES = {'prepare': '0x024d8210', 'end': '0x024d7e50', 'reset': '0x024d6850', 'cancel': '0x024dd8a0'}
FATAL, CHECK = '0x024b55b0', '0x02a34320'
REMOVE, REPORT = '0x024c3260', '0x02a692d0'
ALLOCATE, FREE, ALLOWED, RELEASED = '0x00500200', '0x00500240', '0x00500280', '0x005002c0'
LOCK, UNLOCK, END, DESTROY = '0x00500300', '0x00500340', '0x00500380', '0x005003c0'
MANAGER_LOCK = 0x05aa4590 + 0x648   # the static is 8-aligned
TASK = 0x338
MASK = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


def put(buf, offset, fmt, value):
    struct.pack_into(fmt, buf, offset, value)


class Case:
    def __init__(self, mode, cid, returns='i8'):
        self.mode, self.cid, self.returns = mode, cid, returns
        self.raw = {'task': bytearray([0xa5]) * TASK}
        self.memory, self.stubs, self.args = [], [], {'rdi': 'buf:task'}

    def stub(self, address, argc, **fields):
        self.stubs.append({'address': address, 'argc': argc, **fields})

    def pointer(self, offset, target):
        self.memory.append({'addr': f'buf:task+{offset}', 'pointer': target})

    def word(self, offset, value):
        self.memory.append({'addr': f'buf:task+{offset}', 'bytes': u64(value)})

    def json(self):
        return {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': self.returns,
                'args': self.args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
                'memory': self.memory, 'imports': [], 'stubs': self.stubs}


def allocator(c, name):
    c.raw.update({name: bytearray(0x10), f'{name}_vtable': bytearray(0x78)})
    c.memory += [{'addr': f'buf:{name}+0', 'pointer': f'{name}_vtable+0'},
                 {'addr': f'buf:{name}_vtable+88', 'guest': ALLOCATE},
                 {'addr': f'buf:{name}_vtable+112', 'guest': FREE}]


def prepare(cid, old=None, manager='both', no_buffer=False, heap_path=False, checked=False, close=0,
            chunk=0, result=0, read_size=0x4000, task_allocator=True, alignment=0, allowed=True,
            allocated=True, dependent=False, capacity=0x20):
    """old: None, 'owned' or 'borrowed'; manager: None, 'none' (no callbacks), 'allowed', 'released', 'both'."""
    c = Case('prepare', cid)
    t = c.raw['task']
    put(t, 0x58, '<B', 0xff if no_buffer else 0xfe)
    put(t, 0x78, '<i', close)
    put(t, 0x12a, '<H', chunk)
    put(t, 0x158, '<H', result)
    put(t, 0x120, '<Q', read_size)
    put(t, 0x1d0, '<Q', alignment)
    put(t, 0xe8, '<Q', capacity if heap_path else 7)
    if heap_path:
        c.raw['path_text'] = bytearray(0x40)
        c.pointer(0xd0, 'path_text+0')
    if manager:
        m = bytearray(0x738)
        c.raw['manager'] = m
        c.pointer(0x18, 'manager+0')
        put(m, 0x720, '<Q', 0x720720)
        put(m, 0x730, '<Q', 0x730730)
        if manager in ('allowed', 'both'):
            c.memory.append({'addr': 'buf:manager+1816', 'guest': ALLOWED})
        if manager in ('released', 'both'):
            c.memory.append({'addr': 'buf:manager+1832', 'guest': RELEASED})
    else:
        c.word(0x18, 0)
    has_allowed = manager in ('allowed', 'both')
    has_released = manager in ('released', 'both')
    if old:
        allocator(c, 'old_heap')
        c.raw['old_buffer'] = bytearray(0x10)
        c.pointer(0x170, 'old_buffer+0')
        c.pointer(0x188, 'old_heap+0')
        put(t, 0x178, '<Q', 0x1234)
        put(t, 0x190, '<B', 1 if old == 'owned' else 0)
        if old == 'owned':
            c.stub(FREE, 2)
            if has_released:
                c.stub(RELEASED, 3)
    else:
        c.word(0x170, 0)
    if dependent:
        c.raw['dependent'] = bytearray([0x77]) * 0x168
        c.pointer(0x88, 'dependent+0')
    else:
        c.word(0x88, 0)
    if task_allocator:
        allocator(c, 'heap')
        c.pointer(0x8, 'heap+0')
    else:
        c.word(0x8, 0)
    if no_buffer:
        return c
    c.stub(CHECK, 1, ret='0x1' if checked else '0x0')
    if checked and close not in (0, 1):
        return c
    if not chunk & 4 and result & 0x400 and close != 4:
        if read_size != 0x2c:
            c.stub(FATAL, 3)
        return c
    if not task_allocator:
        c.stub(FATAL, 3)
    if has_allowed:
        c.stub(ALLOWED, 4, ret='0x1' if allowed else '0x0')
        if not allowed:
            return c
    if task_allocator:
        c.raw['new_buffer'] = bytearray(0x10)
        c.stub(ALLOCATE, 3, ret='buf:new_buffer' if allocated else '0x0')
        if not allocated and has_released:
            c.stub(RELEASED, 3)
    return c


def prepare_cases():
    out = []
    for old in (None, 'owned', 'borrowed'):
        for manager in (None, 'none', 'released', 'both'):
            out.append(prepare(f'old_{old}_manager_{manager}', old=old, manager=manager))
    out.append(prepare('no_buffer', no_buffer=True, old='owned'))
    out.append(prepare('no_buffer_no_old', no_buffer=True))
    for checked in (True, False):
        for close in (0, 1, 2, 4, -1):
            out.append(prepare(f'checked{int(checked)}_close{close & 0xff}', checked=checked, close=close))
    out.append(prepare('checked_heap_path', checked=True, close=3, heap_path=True))
    out.append(prepare('heap_path', heap_path=True))
    out.append(prepare('heap_path_capacity_8', heap_path=True, capacity=8))
    for chunk in (0, 4, 0xfffb):
        for result in (0x400, 0, 0xfbff, 0xffff):
            for close in (0, 4):
                out.append(prepare(f'small_chunk{chunk:x}_res{result:x}_close{close}', chunk=chunk, result=result,
                                   close=close, read_size=0x2c))
    out.append(prepare('small_wrong_size', result=0x400, read_size=0x2d))
    out.append(prepare('small_size_zero', result=0x400, read_size=0))
    out.append(prepare('no_allocator_refused', task_allocator=False, manager='allowed', allowed=False))
    for alignment in (0, 0x100, 1):
        out.append(prepare(f'align_{alignment:x}', alignment=alignment, manager='both'))
    for manager in (None, 'none', 'allowed', 'released', 'both'):
        for allocated in (True, False):
            out.append(prepare(f'allocate_{manager}_{int(allocated)}', manager=manager, allocated=allocated,
                               result=0x1234))
    out.append(prepare('refused', manager='both', allowed=False))
    out.append(prepare('dependent', dependent=True, manager='both'))
    out.append(prepare('dependent_not_allocated', dependent=True, manager='both', allocated=False))
    out.append(prepare('old_owned_then_small', old='owned', manager='both', result=0x400, read_size=0x2c))
    return out


def end(cid, result=0, info=True, cancel=4, no_buffer=False, queue=True, path=True, info_fill=0x3c):
    c = Case('end', cid, returns='void')
    t = c.raw['task']
    put(t, 0x158, '<H', result)
    put(t, 0x160, '<I', cancel)
    put(t, 0x58, '<B', 0xff if no_buffer else 0xfe)
    put(t, 0x198 + 0x20, '<Q', 0x2020202020)
    put(t, 0x198 + 0x80, '<Q', 0x8080 if path else 0)
    if queue:
        c.raw['queue'] = bytearray(0x10)
        c.pointer(0x168, 'queue+0')
    else:
        c.word(0x168, 0)
    if not info:
        c.word(0x10, 0)
        return c
    c.raw.update({'info': bytearray([info_fill]) * 0x98, 'lock_vtable': bytearray(0x30)})
    c.pointer(0x10, 'info+0')
    c.memory += [{'addr': f'0x{MANAGER_LOCK:08x}', 'pointer': 'lock_vtable+0'},
                 {'addr': 'buf:lock_vtable+24', 'guest': LOCK}, {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK}]
    if result & 0x200:
        return c
    c.stub(LOCK, 1, argu32=[1])
    c.stub(REMOVE, 2)
    c.stub(UNLOCK, 1)
    if (cancel & 5) == 4 and not no_buffer:
        if not queue:
            c.stub(FATAL, 3)
        if not path:
            c.stub(FATAL, 3)
        c.stub(REPORT, 3)
    return c


def end_cases():
    out = [end('ended', result=0x200), end('ended_no_info', result=0x200, info=False), end('no_info', info=False),
           end('ended_all_flags', result=0xffff)]
    for cancel in (0, 1, 4, 5, 6, 7, 0xfffffffc):
        for no_buffer in (False, True):
            out.append(end(f'cancel{cancel:x}_nobuf{int(no_buffer)}', cancel=cancel, no_buffer=no_buffer, result=0xfdff))
    out += [end(f'info_ff_cancel{cancel}', cancel=cancel, info_fill=0xff) for cancel in (0, 4)]
    out += [end('no_queue', queue=False), end('no_path', path=False), end('neither', queue=False, path=False)]
    return out


def reset(cid, info=True, result=0, chain=0):
    c = Case('reset', cid)
    t = c.raw['task']
    put(t, 0x158, '<H', result)
    c.raw.update({'vtable': bytearray(0x60), 'alloc': bytearray(0x10), 'alloc_vtable': bytearray(0x78)})
    c.pointer(0, 'vtable+0')
    c.memory += [{'addr': 'buf:vtable+88', 'guest': END}, {'addr': 'buf:alloc+0', 'pointer': 'alloc_vtable+0'},
                 {'addr': 'buf:alloc_vtable+112', 'guest': FREE}]
    c.args['rsi'] = 'buf:alloc'
    if info:
        c.raw['info'] = bytearray(0x20)
        c.pointer(0x10, 'info+0')
    else:
        c.word(0x10, 0)
    if info and not result & 0x200:
        c.stub(END, 1)
    c.raw['object_vtable'] = bytearray(8)
    c.memory.append({'addr': 'buf:object_vtable+0', 'guest': DESTROY})
    previous = None
    for i in range(chain):
        name = f'object{i}'
        c.raw[name] = bytearray([0x40 + i]) * 0x150
        c.memory.append({'addr': f'buf:{name}+0', 'pointer': 'object_vtable+0'})
        if previous:
            c.memory.append({'addr': f'buf:{previous}+328', 'pointer': f'{name}+0'})
        else:
            c.pointer(0x2e0, f'{name}+0')
        previous = name
        c.stub(DESTROY, 1)
        c.stub(FREE, 2)
    if previous:
        c.memory.append({'addr': f'buf:{previous}+328', 'bytes': u64(0)})
    else:
        c.word(0x2e0, 0)
    return c


def reset_cases():
    out = []
    for info in (True, False):
        for result in (0, 0x200, 0xfdff, 0xffff):
            out.append(reset(f'info{int(info)}_res{result:x}', info=info, result=result))
    out += [reset(f'chain_{n}', chain=n, result=0x200) for n in (1, 2, 3)]
    out.append(reset('chain_2_with_end', chain=2))
    return out


def cancel(cid, bits, with_flag):
    c = Case('cancel', cid)
    put(c.raw['task'], 0x160, '<I', bits)
    c.args['rsi'] = hex(with_flag)
    return c


def cancel_cases():
    out = []
    for bits in (0, 1, 2, 3, 4, 5, 0xfffffffb, 0xffffffff, 0x100):
        for with_flag in (0, 1):
            out.append(cancel(f'bits{bits:x}_flag{with_flag}', bits, with_flag))
    out.append(cancel('flag_upper_bits', 0, 0x1fe))   # only the low byte is the argument
    out.append(cancel('flag_upper_bits_set', 0, 0x301))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = {'prepare': prepare_cases, 'end': end_cases, 'reset': reset_cases, 'cancel': cancel_cases}[sys.argv[2]]()
    for c in generated:
        with open(os.path.join(out, f'{c.cid}.json'), 'w') as f:
            json.dump(c.json(), f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
