#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the per-thread temporary heap's operations (game/engine/temporary_heap_operations.cpp),
for tools/verify.py run.

usage: temporary_heap_cases.py OUT_DIR FUNCTION
  FUNCTION: destroy, set_size, ensure, ensure_copy, allocator, mark, free_until, allocate,
            allocate_aligned, allocate_top, allocate_aligned_top, free

The heap operations are the linear heap's steps, so their cases are linear_heap_cases.py's, moved
onto the thread: the heap object (`heap`, 0x38 bytes: the linear heap's four words, then the rest of
the object, filled) is what the thread's slot points at (`tls`, with the thread pointer at gs:[0]
and the slot below it, at the offset the game's global holds). The allocator methods get a dummy
interface pointer first; the rewind takes its mark first. Thread heap creation (0x0247d250), the
quit query and default allocator creation, the default allocator's free (through a fake vtable)
and the error report are scripted stubs. A creation stub cannot fill the slot, so a thread without
a heap is covered where the original reads no further than the slot (ensure, allocator, destroy).
"""
import json
import os
import struct
import sys

import linear_heap_cases as lh

ADDRESSES = {
    'destroy': '0x0247d3a0', 'set_size': '0x0247d430', 'ensure': '0x0247d590', 'ensure_copy': '0x0247dd30',
    'allocator': '0x0247d470', 'mark': '0x0247d5b0', 'free_until': '0x0247d5f0', 'allocate': '0x0247d4b0',
    'allocate_aligned': '0x0247d510', 'allocate_top': '0x0247dd60', 'allocate_aligned_top': '0x0247dde0',
    'free': '0x0247e1a0',
}
FATAL_ERROR, CREATE = '0x024b55b0', '0x0247d250'
HEAP_OFFSET_GLOBAL, SIZE_OFFSET_GLOBAL = '0x057e4568', '0x057e4600'
QUIT_QUERY, QUIT_QUERY_CREATE = '0x05aa54f8', '0x024e2550'
DEFAULT_ALLOCATOR, DEFAULT_ALLOCATOR_CREATE = '0x05940670', '0x02fa2b10'
FREE = '0x02484110'             # any game function: the fake vtable points at it, stubbed
THREAD = 0x100                  # the thread pointer's offset in `tls`
HEAP_SLOT, SIZE_SLOT = -0x80, -0x48
INTERFACE = 0x7777000000001234


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def on_thread(fixture, function, cid, has_heap=True, setting=0x5555):
    """Moves a linear heap case (or a bare fixture) onto the calling thread."""
    fixture = dict(fixture)
    fixture['address'], fixture['id'] = ADDRESSES[function], cid
    fixture['gs'] = f'buf:tls+{THREAD}'
    buffers = dict(fixture.get('buffers', {}))
    buffers['heap'] = {'size': 0x38, 'bytes': '4e' * 0x38}
    buffers['tls'] = {'size': THREAD + 8}
    fixture['buffers'] = buffers
    memory = list(fixture.get('memory', []))
    memory += [{'addr': HEAP_OFFSET_GLOBAL, 'bytes': u64(HEAP_SLOT)},
               {'addr': SIZE_OFFSET_GLOBAL, 'bytes': u64(SIZE_SLOT)},
               {'addr': f'buf:tls+{THREAD}', 'pointer': f'tls+{THREAD}'},
               {'addr': f'buf:tls+{THREAD + SIZE_SLOT}', 'bytes': u64(setting)},
               {'addr': f'buf:tls+{THREAD + HEAP_SLOT}', **({'pointer': 'heap'} if has_heap else {'bytes': u64(0)})}]
    fixture['memory'] = memory
    if not has_heap:
        fixture['stubs'] = [{'address': CREATE, 'argc': 0}] + list(fixture.get('stubs', []))
    return fixture


def bare(function, cid, returns='void', args=None, stubs=(), **kw):
    return on_thread({'schema': 1, 'returns': returns, 'args': args or {}, 'buffers': {}, 'memory': [],
                      'stubs': list(stubs), 'imports': []}, function, cid, **kw)


def moved(function, linear_function):
    """linear_heap_cases.py's cases for `linear_function`, on the thread."""
    out = []
    for c in getattr(lh, linear_function)():
        args = c['args']
        if function == 'free_until':
            c['args'] = {'rdi': args['rsi']}
        else:
            c['args'] = {'rdi': INTERFACE, **{k: v for k, v in args.items() if k != 'rdi'}}
        out.append(on_thread(c, function, c['id']))
    return out


def destroy():
    def make(cid, has_heap=True, quit_query=True, allocator=True):
        memory = [{'addr': 'buf:backing+0', 'pointer': 'vtable'}, {'addr': 'buf:vtable+112', 'guest': FREE},
                  {'addr': QUIT_QUERY, **({'pointer': 'query'} if quit_query else {'bytes': u64(0)})},
                  {'addr': DEFAULT_ALLOCATOR, **({'pointer': 'backing'} if allocator else {'bytes': u64(0)})}]
        stubs = []
        if has_heap:
            if not quit_query:
                stubs.append({'address': QUIT_QUERY_CREATE, 'argc': 0, 'ret': 'buf:query'})
            if not allocator:
                stubs.append({'address': DEFAULT_ALLOCATOR_CREATE, 'argc': 0, 'ret': 'buf:backing'})
            stubs.append({'address': FREE, 'argc': 2})
        c = {'schema': 1, 'returns': 'void', 'args': {}, 'stubs': stubs, 'imports': [], 'memory': memory,
             'buffers': {'backing': {'size': 8}, 'vtable': {'size': 0x78}, 'query': {'size': 8}}}
        c = on_thread(c, 'destroy', cid, has_heap=True)
        if not has_heap:
            c['memory'][-1] = {'addr': f'buf:tls+{THREAD + HEAP_SLOT}', 'bytes': u64(0)}
        return c
    return [make('has_heap'), make('no_heap'), make('no_quit_query', quit_query=False),
            make('no_default_allocator', allocator=False),
            make('no_singletons', quit_query=False, allocator=False),
            make('no_heap_no_singletons', has_heap=False, quit_query=False, allocator=False)]


def set_size():
    return [bare('set_size', cid, args={'rdi': size}) for cid, size in
            [('zero_is_default', 0), ('given', 0x8000), ('one', 1), ('huge', 0xffffffffffffffff),
             ('default_value', 0x20000)]]


def ensure(function):
    return [bare(function, 'has_heap'), bare(function, 'no_heap', has_heap=False)]


def allocator():
    return [bare('allocator', 'has_heap', returns='i64'),
            bare('allocator', 'no_heap_stays_null', returns='i64', has_heap=False)]


def mark():
    return [on_thread(lh.case('allocate', cid, bottom=bottom), 'mark', cid) | {'returns': 'i64', 'args': {}}
            for cid, bottom in [('bottom', 0x80), ('at_lowest', lh.LOWEST), ('at_top', 0x1f0),
                                ('outside', (lh.ABS, 0x1234))]]


WHICH = {
    'destroy': destroy, 'set_size': set_size, 'ensure': lambda: ensure('ensure'),
    'ensure_copy': lambda: ensure('ensure_copy'), 'allocator': allocator, 'mark': mark,
    'free_until': lambda: moved('free_until', 'free_until'), 'allocate': lambda: moved('allocate', 'allocate'),
    'allocate_aligned': lambda: moved('allocate_aligned', 'allocate_aligned'),
    'allocate_top': lambda: moved('allocate_top', 'allocate_top'),
    'allocate_aligned_top': lambda: moved('allocate_aligned_top', 'allocate_aligned_top'),
    'free': lambda: moved('free', 'free'),
}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in WHICH:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    made = WHICH[sys.argv[2]]()
    for c in made:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(made)} cases in {out}')


if __name__ == '__main__':
    main()
