#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the loading queue helpers (game/loading/queue_helpers.cpp), for tools/verify.py run.

usage: queue_helpers_cases.py OUT_DIR free|string|construct|destroy|cancel

- free: a null block (nothing called); data sizes 0, small, at and around the 32-bit wrap once the
  header is added, with a nonzero header word (cleared) and junk in the header's last word.
- string: words 0 and 1 (left alone), 2 and other even words (cleared, nothing freed), odd words
  (freed through the thread allocator, one below the word, then cleared), words with the top
  bit set (an unsigned compare), and one owned string freed through the real headed free.
- construct / destroy: a null info and an info, over a handle filled with junk; the reference
  calls (0x02a3a300, 0x024bd930) are stubbed.
- cancel: every state from 0 to 0xf and states with high bits, with and without the flag, and
  flag arguments with junk above the low byte.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

import fios_fixtures as fixtures

ADDRESSES = {'free': '0x00829a90', 'string': '0x00836840', 'construct': '0x024bd470',
             'destroy': '0x024bd4c0', 'cancel': '0x024dd8a0'}
RETURNS = {'free': 'void', 'string': 'void', 'construct': 'void', 'destroy': 'void', 'cancel': 'i8'}
FREE_SIZED_FN = fixtures.FREE_SIZED_FN
ADD_REFERENCE = '0x02a3a300'
RELEASE = '0x024bd930'
HEADER = 0x10
STATE = 0x160


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def block(size, word=0x1111111111111111, tail=0x5d5d5d5d):
    data = bytearray(b'\xa7' * 0x30)
    struct.pack_into('<QII', data, 0, word, size, tail)
    return {'size': len(data), 'bytes': data.hex()}


def free_case(cid, size=0x20, word=0x1111111111111111, tail=0x5d5d5d5d, null=False):
    c = base('free', cid)
    c['buffers'] = {'allocator': {'size': 0x10}, 'allocator_vt': {'size': 0x20}}
    c['memory'] = [{'addr': 'buf:allocator', 'pointer': 'allocator_vt'},
                   {'addr': 'buf:allocator_vt+0x18', 'guest': FREE_SIZED_FN}]
    c['args'] = {'rdi': 'buf:allocator', 'rsi': 0 if null else f'buf:block+{HEADER:#x}'}
    if not null:
        c['buffers']['block'] = block(size, word, tail)
        c['stubs'] = [{'address': FREE_SIZED_FN, 'argc': 3, 'ret': '0x77'}]
    return c


def free_cases():
    out = [free_case('null_block', null=True),
           free_case('ordinary'),
           free_case('size_zero', size=0),
           free_case('header_word_zero', word=0),
           free_case('header_tail_zero', tail=0),
           free_case('header_tail_ones', tail=0xffffffff),
           free_case('size_large', size=0x7ffffff0)]
    for size in (0xffffffe0, 0xffffffef, 0xfffffff0, 0xfffffff1, 0xffffffff):
        out.append(free_case(f'size_{size:08x}', size=size))
    return out


def string_case(cid, word=None, owned=False, real_free=False):
    c = base('string', cid)
    c['buffers'] = {'string': {'size': 0x10, 'bytes': 'c3' * 0x10}}
    c['args'] = {'rdi': 'buf:string+0x4'}
    fixtures.thread_allocator(c)
    if owned:
        c['buffers']['block'] = block(0x28)
        c['memory'].append({'addr': 'buf:string+0x4', 'pointer': f'block+{HEADER + 1:#x}'})
    else:
        c['memory'].append({'addr': 'buf:string+0x4', 'bytes': struct.pack('<Q', word).hex()})
    frees = owned or (word >= 2 and word & 1)
    if frees and real_free:
        c['stubs'].append({'address': FREE_SIZED_FN, 'argc': 3})
    elif frees:
        c['stubs'].append({'address': fixtures.FREE_HEADED, 'argc': 2})
    return c


def string_cases():
    out = [string_case('owned_pointer', owned=True),
           string_case('owned_pointer_real_free', owned=True, real_free=True)]
    for word in (0, 1, 2, 3, 4, 5, 0x1000, 0x1001, 0x7fffffffffffffff, 0x7ffffffffffffffe,
                 0x8000000000000000, 0x8000000000000001, 0xfffffffffffffffe, 0xffffffffffffffff):
        out.append(string_case(f'word_{word:016x}', word=word))
    return out


def handle_case(variant, cid, info=True, vtable=0x1111111111111111):
    c = base(variant, cid)
    data = bytearray(b'\x5e' * 0x20)
    struct.pack_into('<Q', data, 0, vtable)
    c['buffers'] = {'handle': {'size': 0x20, 'bytes': data.hex()}, 'info': {'size': 0x40, 'bytes': '9b' * 0x40}}
    c['args'] = {'rdi': 'buf:handle'}
    if variant == 'construct':
        c['args']['rsi'] = 'buf:info' if info else 0
        if info:
            c['stubs'] = [{'address': ADD_REFERENCE, 'argc': 1, 'ret': '0x55'}]
    else:
        c['memory'] = [{'addr': 'buf:handle+0x8', 'pointer': 'info'} if info else
                       {'addr': 'buf:handle+0x8', 'bytes': '00' * 8}]
        if info:
            c['stubs'] = [{'address': RELEASE, 'argc': 1, 'ret': '0x55'}]
    return c


def handle_cases(variant):
    return [handle_case(variant, 'null_info', info=False),
            handle_case(variant, 'with_info'),
            handle_case(variant, 'null_vtable_with_info', vtable=0),
            handle_case(variant, 'null_vtable_null_info', info=False, vtable=0)]


def cancel_case(state, flag):
    c = base('cancel', f'state_{state:08x}_flag_{flag:x}')
    data = bytearray(b'\x6c' * 0x170)
    struct.pack_into('<I', data, STATE, state)
    c['buffers'] = {'task': {'size': len(data), 'bytes': data.hex()}}
    c['args'] = {'rdi': 'buf:task', 'rsi': flag}
    return c


def cancel_cases():
    states = list(range(0x10)) + [0xfffffffb, 0xfffffff8, 0xffffffff, 0x80000000, 0x7ffffff0]
    out = [cancel_case(s, f) for s in states for f in (0, 1)]
    out += [cancel_case(0, 0xff00), cancel_case(0, 0xff01), cancel_case(0x10, 0xffffffffffffff01)]
    return out


def cases(variant):
    if variant == 'free':
        return free_cases()
    if variant == 'string':
        return string_cases()
    if variant in ('construct', 'destroy'):
        return handle_cases(variant)
    return cancel_cases()


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
