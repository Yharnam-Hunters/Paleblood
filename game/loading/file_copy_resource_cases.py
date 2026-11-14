#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/loading/file_copy_resource.cpp, for tools/verify.py run.

usage: file_copy_resource_cases.py OUT_DIR ADDRESS

ADDRESS is one of the twelve functions (three classes, four functions each) or the resource
base's constructor (0x013e6ff0) or destructor (0x013e7090). Every case lays out
an invented 0x98-byte resource with a handle to an invented loaded file (bytes in the file, or in
a first part), the job lock with a fake vtable whose lock and unlock are scripted stubs, and the
heaps (fake vtables, scripted allocate / free). The destructor's DLNew release reads a thread
pointer (`tls`, the pointer itself at gs:[0]) and the per-thread state at the offset its global
holds. The resource base's constructor and destructor, the heap lookup, the unload, the memory
copy, operator delete and the fatal error report are scripted too. The resource base's own
cases script the object base's constructor and destructor and the file handle's release and
destructor, each with a spare scripted call, so an unexpected call is recorded. No game data is
used.
"""
import json
import os
import struct
import sys

CONSTRUCT, DESTROY, DELETE, COPY = 'construct', 'destroy', 'delete', 'copy'
FUNCTIONS = {
    '0x0237e640': CONSTRUCT, '0x0237f3f0': CONSTRUCT, '0x023801a0': CONSTRUCT,
    '0x0237e780': DESTROY, '0x0237f530': DESTROY, '0x023802e0': DESTROY,
    '0x0237e6d0': DELETE, '0x0237f480': DELETE, '0x02380230': DELETE,
    '0x0237e820': COPY, '0x0237f5d0': COPY, '0x02380380': COPY,
    '0x013e6ff0': 'base_construct', '0x013e7090': 'base_destroy',
}
OBJECT_CONSTRUCT, OBJECT_DESTROY, HANDLE_RELEASE, HANDLE_DESTROY = '0x01384700', '0x01384750', '0x024bd360', '0x024bd4c0'

JOB_LOCK, RESOURCE_HEAP, FILE_COPY_HEAP = '0x05aa4578', '0x05940408', '0x05940410'
NEW_STATE_OFFSET_GLOBAL = '0x057e44a8'
BASE_CONSTRUCT, BASE_DESTROY = '0x013e6ff0', '0x013e7090'
HEAP_OWNING, UNLOAD, MEMCPY, FATAL_ERROR = '0x0247b720', '0x024bdb80', '0x02a1a1b0', '0x024b55b0'
# Fake vtable targets (any address: the case stubs them).
LOCK, UNLOCK, ALLOCATE, FREE = '0x00500100', '0x00500200', '0x00500300', '0x00500400'

THREAD = 0x100           # the thread pointer's offset in `tls`
STATE_OFFSET = -0x60     # the per-thread state's offset from the thread pointer
RELEASE_HEAP = 0x10      # its release heap field
UNLOAD_WHEN_FREE = 0x02


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def word(memory, addr, value):
    if isinstance(value, str):
        memory.append({'addr': addr, 'pointer': value})
    else:
        memory.append({'addr': addr, 'bytes': u64(value)})


def common(memory, buffers, resource_fill):
    buffers['resource'] = {'size': 0xa0, 'bytes': f'{resource_fill:02x}' * 0xa0}
    buffers['lock'] = {'size': 8}
    buffers['lock_vtable'] = {'size': 0x30}
    buffers['heap'] = {'size': 8}
    buffers['heap_vtable'] = {'size': 0x80}
    word(memory, JOB_LOCK, 'lock_vtable')
    memory.append({'addr': 'buf:lock_vtable+0x18', 'guest': LOCK})
    memory.append({'addr': 'buf:lock_vtable+0x28', 'guest': UNLOCK})
    word(memory, 'buf:heap+0', 'heap_vtable')
    memory.append({'addr': 'buf:heap_vtable+0x50', 'guest': ALLOCATE})
    memory.append({'addr': 'buf:heap_vtable+0x70', 'guest': FREE})


def lock_stubs():
    return [{'address': LOCK, 'argc': 2, 'argu32': [1], 'argi': [0]}, {'address': UNLOCK, 'argc': 1}]


def fixture(address, cid, buffers, memory, stubs, imports=None, gs=None):
    f = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:resource'},
         'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': imports or []}
    if gs:
        f['gs'] = gs
    return f


def construct_case(address, cid, heap=True, fill=0xa5):
    memory, buffers = [], {}
    common(memory, buffers, fill)
    word(memory, RESOURCE_HEAP, 'heap' if heap else 0)
    stubs = [{'address': BASE_CONSTRUCT, 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 0x18}]}]
    return fixture(address, cid, buffers, memory, stubs)


def destroy_case(address, cid, deleting, copy=True, thread_heap=False, owner=True, base_writes=False,
                 fill=0xa5):
    memory, buffers = [], {}
    common(memory, buffers, fill)
    buffers['tls'] = {'size': THREAD + 8}
    buffers['copy'] = {'size': 0x20, 'bytes': '3c' * 0x20}
    buffers['thread_heap'] = {'size': 8}
    word(memory, NEW_STATE_OFFSET_GLOBAL, STATE_OFFSET)
    word(memory, f'buf:tls+{THREAD}', f'tls+{THREAD}')
    word(memory, 'buf:thread_heap+0', 'heap_vtable')
    word(memory, f'buf:tls+{THREAD + STATE_OFFSET + RELEASE_HEAP}', 'thread_heap' if thread_heap else 0)
    word(memory, 'buf:resource+0x88', 'copy' if copy else 0)
    stubs = []
    if copy:
        if not thread_heap:
            stubs.append({'address': HEAP_OWNING, 'argc': 1, 'ret': 'buf:heap' if owner else 0})
            if not owner:
                stubs.append({'address': FATAL_ERROR, 'argc': 3})
        if thread_heap or owner:
            stubs.append({'address': FREE, 'argc': 2})
    base = {'address': BASE_DESTROY, 'argc': 1, 'argmem': [{'arg': 0, 'size': 0x98}]}
    if base_writes:
        base['writes'] = [{'arg': 0, 'offset': 0, 'bytes': u64(0x1122334455667788)}]
    stubs.append(base)
    imports = [{'name': '_ZdlPv', 'argc': 1}] if deleting else []
    return fixture(address, cid, buffers, memory, stubs, imports, gs=f'buf:tls+{THREAD}')


def copy_case(address, cid, file=True, parts=0, data=True, size=0x40, holders=0, flags=0,
              allocation=True, copy_over_handle=False, fill=0xa5):
    """copy_over_handle: the allocation returns the handle's file field and the memory copy clears
    it, so the file is gone when the copy reads the handle again before letting go."""
    memory, buffers = [], {}
    common(memory, buffers, fill)
    buffers['file'] = {'size': 0x90, 'bytes': '5a' * 0x90}
    buffers['part'] = {'size': 0x20, 'bytes': '96' * 0x20}
    buffers['part_list'] = {'size': 8}
    buffers['bytes'] = {'size': 0x40, 'bytes': ''.join(f'{i:02x}' for i in range(0x40))}
    buffers['copy'] = {'size': 0x40, 'bytes': 'c3' * 0x40}
    word(memory, FILE_COPY_HEAP, 'heap')
    word(memory, 'buf:resource+0x70', 'file' if file else 0)
    holder = 'part' if parts else 'file'
    base = 0x10 if parts else 0x18
    word(memory, f'buf:{holder}+{base}', 'bytes' if data else 0)
    word(memory, f'buf:{holder}+{base + 8}', size)
    word(memory, 'buf:part_list+0', 'part')
    word(memory, 'buf:file+0x80', 'part_list')
    word(memory, 'buf:file+0x88', parts)
    memory.append({'addr': 'buf:file+0x30', 'bytes': f'{holders & 0xff:02x}{flags:02x}'})
    stubs = []
    if file:
        for step in (1, 2, 3):   # hold the file, read its size, let go of it: one lock each
            lock, unlock = lock_stubs()
            stubs.append(lock)
            if step == 3 and (flags & UNLOAD_WHEN_FREE) and holders & 0xff == 0:
                stubs.append({'address': UNLOAD, 'argc': 1})
            stubs.append(unlock)
            if step == 2 and data and size:
                copy = 'buf:resource+0x70' if copy_over_handle else 'buf:copy'
                stubs.append({'address': ALLOCATE, 'argc': 2, 'ret': copy if allocation else 0})
                if allocation:
                    memcpy = {'address': MEMCPY, 'argc': 3}
                    if copy_over_handle:
                        memcpy['writes'] = [{'arg': 0, 'offset': 0, 'bytes': u64(0)}]
                    stubs.append(memcpy)
            if step == 2 and copy_over_handle:
                break
    return fixture(address, cid, buffers, memory, stubs)


def base_case(address, cid, fill=0xa5, heap_word=0x1122334455667788, base_writes=False):
    buffers = {'resource': {'size': 0xa0, 'bytes': f'{fill:02x}' * 0xa0},
               'heap_reference': {'size': 0x18, 'bytes': '3c' * 0x10 + u64(heap_word)}}
    spares = [{'address': OBJECT_CONSTRUCT, 'argc': 2}, {'address': OBJECT_DESTROY, 'argc': 1},
              {'address': HANDLE_RELEASE, 'argc': 1}, {'address': HANDLE_DESTROY, 'argc': 1}]
    rewrite = [{'arg': 0, 'offset': 0, 'bytes': u64(0x5566778899aabbcc)}]
    if address == '0x013e6ff0':
        made = {'address': OBJECT_CONSTRUCT, 'argc': 2, 'argi': [0, 1], 'argmem': [{'arg': 1, 'size': 0x18}]}
        if base_writes:
            made['writes'] = rewrite + [{'arg': 0, 'offset': 0x78, 'bytes': '1122334455667788' + u64(0)}]
        stubs = [made]
    else:
        release = {'address': HANDLE_RELEASE, 'argc': 1, 'argmem': [{'arg': 0, 'size': 0x10}]}
        if base_writes:   # the vtable is written before the handle goes
            release['writes'] = [{'arg': 0, 'offset': -0x68, 'bytes': u64(0x5566778899aabbcc)}]
        stubs = [release, {'address': HANDLE_DESTROY, 'argc': 1},
                 {'address': OBJECT_DESTROY, 'argc': 1, 'argmem': [{'arg': 0, 'size': 0x98}]}]
    f = fixture(address, cid, buffers, [], stubs + spares)
    f['args']['rsi'] = 'buf:heap_reference'
    return f


def cases_for(address):
    kind = FUNCTIONS[address]
    if kind in ('base_construct', 'base_destroy'):
        return [base_case(address, 'ordinary'), base_case(address, 'zero_filled', fill=0, heap_word=0),
                base_case(address, 'all_bits', fill=0xff, heap_word=0xffffffffffffffff),
                base_case(address, 'base_writes', base_writes=True)]
    if kind == CONSTRUCT:
        return [construct_case(address, 'ordinary'),
                construct_case(address, 'no_resource_heap', heap=False),
                construct_case(address, 'zero_filled', fill=0),
                construct_case(address, 'all_bits', fill=0xff)]
    if kind in (DESTROY, DELETE):
        d = kind == DELETE
        return [destroy_case(address, 'owner_heap_frees', d),
                destroy_case(address, 'thread_heap_frees', d, thread_heap=True),
                destroy_case(address, 'no_copy', d, copy=False),
                destroy_case(address, 'no_copy_thread_heap_kept', d, copy=False, thread_heap=True),
                destroy_case(address, 'base_rewrites_vtable', d, base_writes=True),
                destroy_case(address, 'zero_filled', d, fill=0),
                destroy_case(address, 'all_bits', d, fill=0xff)]
    out = [copy_case(address, 'whole_file'),
           copy_case(address, 'first_part', parts=1),
           copy_case(address, 'first_of_three_parts', parts=3),
           copy_case(address, 'no_file', file=False),
           copy_case(address, 'no_bytes', data=False),
           copy_case(address, 'empty_file', size=0),
           copy_case(address, 'empty_part', parts=1, size=0),
           copy_case(address, 'allocation_fails', allocation=False),
           copy_case(address, 'other_holders', holders=3),
           copy_case(address, 'unload_when_free', flags=UNLOAD_WHEN_FREE),
           copy_case(address, 'unload_when_free_other_holders', flags=UNLOAD_WHEN_FREE, holders=2),
           copy_case(address, 'other_flags_only', flags=0xfd),
           copy_case(address, 'all_flags', flags=0xff),
           copy_case(address, 'holders_wrap', holders=0xff, flags=UNLOAD_WHEN_FREE),
           copy_case(address, 'file_gone_before_letting_go', copy_over_handle=True),
           copy_case(address, 'file_gone_before_letting_go_unload', copy_over_handle=True, flags=UNLOAD_WHEN_FREE),
           copy_case(address, 'huge_size_allocation_fails', size=0xffffffffffffff00, allocation=False),
           copy_case(address, 'no_bytes_in_part', parts=2, data=False),
           copy_case(address, 'zero_filled', fill=0),
           copy_case(address, 'all_bits', fill=0xff)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in FUNCTIONS:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    cases = cases_for(sys.argv[2])
    for c in cases:
        with open(os.path.join(sys.argv[1], c['id'] + '.json'), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
