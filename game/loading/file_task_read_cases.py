#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the file task's read start (game/loading/file_task_read.cpp), for tools/verify.py
run.

usage: file_task_read_cases.py OUT_DIR

Every case gives a file task (0x338 bytes, filled with a pattern, then the fields the case sets):
the chunk and result flags, the state, the close status, the read object, the flags at +0x58, the
buffer words, the path (inline or on the heap), the request's byte +0x41, texts (inline or on the
heap), data word and +0x138, an info with its read flags, and the reader, which the task may point
at with an address that is not 8-aligned. The shared job lock (0x05aa4578) and the reader's lock
point at fake vtables whose lock and unlock are scripted stubs, so their order is compared; the
read queue's push_back (0x024d37d0) is a stub that records the request it is given, field by field,
leaving out the padding and the request's extra words when the original leaves them unset (bit 0
of +0x41 with +0x138 zero). The condition signal's set (0x0247e780) is a stub. Generated fixtures,
not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESS = '0x024d8470'
PUSH_BACK, SIGNAL_SET = '0x024d37d0', '0x0247e780'
JOB_LOCK = '0x05aa4578'
LOCK, UNLOCK, READER_LOCK, READER_UNLOCK = '0x00500200', '0x00500240', '0x00500280', '0x005002c0'
TASK, REQUEST = 0x338, 0x198
READER, READER_LOCK_OFFSET = 0x120, 0x110


def put(buf, offset, fmt, value):
    struct.pack_into(fmt, buf, offset, value)


def case(cid, chunk=0, result=0, state=6, close=0, read_object=0, flags=0, path_heap=False,
         extra_bit=0, extra_value=0, texts_heap=False, read_flags=0xff, reader_offset=0,
         size_left=0x1000, buffer=(0x7000, 0x400, 0x100), fill=0xa5):
    task = bytearray([fill]) * TASK
    put(task, 0x12a, '<H', chunk)
    put(task, 0x158, '<H', result)
    put(task, 0x60, '<i', state)
    put(task, 0x78, '<i', close)
    put(task, 0x100, '<Q', read_object)
    put(task, 0x58, '<B', flags)
    struct.pack_into('<QQQ', task, 0x170, *buffer)
    put(task, 0xe8, '<Q', 8 if path_heap else 7)
    put(task, REQUEST + 0x41, '<B', extra_bit)
    put(task, REQUEST + 0xa0 + 0x20, '<Q', 0x20 if texts_heap else 3)
    put(task, REQUEST + 0xd8 + 0x20, '<Q', 0x10 if texts_heap else 0)
    put(task, REQUEST + 0x118, '<Q', 0xda7a)
    put(task, REQUEST + 0x138, '<i', extra_value)
    info = bytearray([fill ^ 0xff]) * 0x98
    info[0x32] = read_flags
    buffers = {'task': task, 'info': info, 'reader': bytearray(b'\x3c' * READER),
               'job_vtable': bytearray(0x30), 'reader_vtable': bytearray(0x30)}
    memory = [{'addr': 'buf:task+16', 'pointer': 'info+0'},
              {'addr': 'buf:task+48', 'pointer': f'reader+{reader_offset}'},
              {'addr': f'buf:reader+{reader_offset + READER_LOCK_OFFSET}', 'pointer': 'reader_vtable+0'},
              {'addr': JOB_LOCK, 'pointer': 'job_vtable+0'},
              {'addr': 'buf:job_vtable+24', 'guest': LOCK}, {'addr': 'buf:job_vtable+40', 'guest': UNLOCK},
              {'addr': 'buf:reader_vtable+24', 'guest': READER_LOCK},
              {'addr': 'buf:reader_vtable+40', 'guest': READER_UNLOCK}]
    if path_heap:
        buffers['path'] = bytearray(b'\x50\x00' * 8)
        memory.append({'addr': 'buf:task+208', 'pointer': 'path+0'})
    if texts_heap:
        buffers['texts'] = bytearray(0x40)
        memory += [{'addr': f'buf:task+{REQUEST + 0xa8}', 'pointer': 'texts+0'},
                   {'addr': f'buf:task+{REQUEST + 0xe0}', 'pointer': 'texts+32'}]

    holds_read_object = read_object and not chunk & 4 and close != 4
    stubs = []
    if not holds_read_object:
        stubs += [{'address': LOCK, 'argc': 1, 'argu32': [1]}, {'address': UNLOCK, 'argc': 1}]
    request = [{'arg': 1, 'offset': 0, 'size': 0x6c}, {'arg': 1, 'offset': 0x90, 'size': 0x1b}]
    if not (extra_bit & 1 and extra_value == 0):
        request.append({'arg': 1, 'offset': 0x70, 'size': 0x1c})
    stubs += [{'address': READER_LOCK, 'argc': 1, 'argu32': [1]},
              {'address': PUSH_BACK, 'argc': 2, 'argmem': request},
              {'address': SIGNAL_SET, 'argc': 1, 'argu8': [1], 'ret': '0x1'},
              {'address': READER_UNLOCK, 'argc': 1}]
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:task', 'rsi': f'0x{size_left:x}'},
            'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in buffers.items()},
            'memory': memory, 'imports': [], 'stubs': stubs}


def cases():
    out = [case('plain'), case('zero_fill', fill=0), case('ones_fill', fill=0xff, size_left=0)]
    for chunk in (0, 4, 0xfffb, 0xffff):
        for result in (0, 0x400, 0xfbff, 0xffff):
            out.append(case(f'chunk{chunk:x}_result{result:x}', chunk=chunk, result=result))
    for state in (0, 14, 15, 16, -1):
        out.append(case(f'small_state{state & 0xff:x}', chunk=4, result=0x400, state=state))
    for read_object in (0, 0x1234):
        for chunk in (0, 4, 0xfffb):
            for close in (0, 1, 3, 4, 5):
                out.append(case(f'object{int(bool(read_object))}_chunk{chunk:x}_close{close}',
                                read_object=read_object, chunk=chunk, close=close))
    for read_flags in (0xff, 0xfe, 0x01, 0x00, 0x03, 0x80):
        out.append(case(f'read_flags{read_flags:x}', read_flags=read_flags, close=2))
        out.append(case(f'read_flags{read_flags:x}_object', read_flags=read_flags, read_object=8, close=2))
    for flags in (0, 1, 0xfe, 0xff):
        out.append(case(f'flags{flags:x}', flags=flags))
    for extra_bit in (0, 1, 0xfe, 0xff):
        for extra_value in (0, 1, -1):
            out.append(case(f'extra{extra_bit:x}_value{extra_value & 0xff:x}', extra_bit=extra_bit,
                            extra_value=extra_value))
    out.append(case('extra_texts_heap', extra_bit=1, extra_value=5, texts_heap=True))
    out.append(case('extra_texts_heap_clear', extra_bit=0, texts_heap=True))
    out.append(case('path_heap', path_heap=True))
    for offset in range(1, 8):
        out.append(case(f'reader_offset{offset}', reader_offset=offset))
    out.append(case('buffer_full', buffer=(0x9000, 0x200, 0x200)))
    out.append(case('buffer_done', buffer=(0x9000, 0x200, 0)))
    out.append(case('buffer_wraps', buffer=(0x10, 0x10, 0x40), size_left=0xffffffffffffffff))
    out.append(case('everything', chunk=4, result=0x400, read_object=1, close=4, flags=1, extra_bit=1,
                    extra_value=2, texts_heap=True, path_heap=True, read_flags=1, reader_offset=5))
    return out


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
