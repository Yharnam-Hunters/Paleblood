#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/temporary_heap_create.cpp, for tools/verify.py run.

usage: temporary_heap_create_cases.py OUT_DIR

Each case gives the thread a pointer (`tls`, with the pointer itself at gs:[0]) and, below it, the
thread's heap slot and size setting at the offsets the game's globals hold. The quit query and the
default allocator singletons exist or not; their creation functions, the default allocator's
allocation (through a fake vtable) and the error report are scripted stubs. The heap object is
written into `memory`, which the allocation returns (or 0).
"""
import json
import os
import struct
import sys

CREATE, FATAL_ERROR = '0x0247d250', '0x024b55b0'
HEAP_OFFSET_GLOBAL, SIZE_OFFSET_GLOBAL = '0x057e4568', '0x057e4600'
QUIT_QUERY, QUIT_QUERY_CREATE = '0x05aa54f8', '0x024e2550'
DEFAULT_ALLOCATOR, DEFAULT_ALLOCATOR_CREATE = '0x05940670', '0x02fa2b10'
ALLOCATE = '0x02484110'         # any game function: the fake vtable points at it, stubbed
THREAD = 0x100                  # the thread pointer's offset in `tls`
HEAP_SLOT, SIZE_SLOT = -0x80, -0x48


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, setting=0, has_heap=False, quit_query=True, allocator=True, allocation=True):
    memory = [{'addr': HEAP_OFFSET_GLOBAL, 'bytes': u64(HEAP_SLOT)},
              {'addr': SIZE_OFFSET_GLOBAL, 'bytes': u64(SIZE_SLOT)},
              {'addr': f'buf:tls+{THREAD}', 'pointer': f'tls+{THREAD}'},
              {'addr': f'buf:tls+{THREAD + SIZE_SLOT}', 'bytes': u64(setting)},
              {'addr': 'buf:backing+0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+80', 'guest': ALLOCATE}]
    if has_heap:
        memory.append({'addr': f'buf:tls+{THREAD + HEAP_SLOT}', 'pointer': 'existing'})
    else:
        memory.append({'addr': f'buf:tls+{THREAD + HEAP_SLOT}', 'bytes': u64(0)})
    memory.append({'addr': QUIT_QUERY, 'pointer': 'query'} if quit_query else {'addr': QUIT_QUERY, 'bytes': u64(0)})
    memory.append({'addr': DEFAULT_ALLOCATOR, 'pointer': 'backing'} if allocator
                  else {'addr': DEFAULT_ALLOCATOR, 'bytes': u64(0)})
    stubs = []
    if not has_heap:
        if setting and setting < 0x38:
            stubs.append({'address': FATAL_ERROR, 'argc': 3})
        if not quit_query:
            stubs.append({'address': QUIT_QUERY_CREATE, 'argc': 0, 'ret': 'buf:query'})
        if not allocator:
            stubs.append({'address': DEFAULT_ALLOCATOR_CREATE, 'argc': 0, 'ret': 'buf:backing'})
        stubs.append({'address': ALLOCATE, 'argc': 2, 'ret': 'buf:memory' if allocation else 0})
        if not allocation:
            stubs.append({'address': FATAL_ERROR, 'argc': 3})
    return {'schema': 1, 'address': CREATE, 'id': cid, 'returns': 'void', 'args': {},
            'gs': f'buf:tls+{THREAD}',
            'buffers': {'tls': {'size': THREAD + 8}, 'backing': {'size': 8}, 'vtable': {'size': 0x60},
                        'query': {'size': 8}, 'existing': {'size': 8},
                        'memory': {'size': 0x40, 'bytes': '6b' * 0x40}},
            'memory': memory, 'stubs': stubs, 'imports': []}


CASES = [
    case('default_size'),
    case('setting_used', setting=0x8000),
    case('setting_exactly_header', setting=0x38),
    case('setting_too_small', setting=0x37),
    case('setting_one', setting=1),
    case('setting_huge', setting=0xffffffffffff0000),
    case('already_has_heap', has_heap=True),
    case('already_has_heap_nothing_else', has_heap=True, quit_query=False, allocator=False),
    case('no_quit_query', quit_query=False),
    case('no_default_allocator', allocator=False),
    case('no_singletons', quit_query=False, allocator=False, setting=0x100),
    case('out_of_memory', allocation=False),
    case('out_of_memory_small_setting', allocation=False, setting=0x10),
]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for c in CASES:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases in {out}')


if __name__ == '__main__':
    main()
