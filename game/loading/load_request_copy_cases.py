#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for copying a resource load request and building its tail
(game/loading/load_request_copy.cpp), for tools/verify.py run.

usage: load_request_copy_cases.py OUT_DIR [copy|tail]

copy (0x024be3b0): a source request (0x148 bytes of a pattern with three synthetic wide strings,
each string's allocator a fake object whose vtable slot 0x10 is a scripted stub) and a copy
buffer of another pattern, or the same buffer for a copy onto itself. The request heap's owner
(0x0247bbf0) returns a fake object with the heap at +0x458; its info slot writes the flags byte.
The string assign (0x02a279a0), the tail constructor (0x024be8e0, which here only gives the copy's
strings allocators), the array assign (0x02b4d100) and the error report are scripted stubs.

tail (0x024be8e0): a tail buffer of a pattern; the default container heap (0x05940670) is set or
null, then made by a scripted 0x02fa2b10, whose info slot writes the flags byte (once per string
and once for the array; a case can give each its own flags).
"""
import json
import os
import struct
import sys

COPY, TAIL = '0x024be3b0', '0x024be8e0'
HEAP_OWNER, ASSIGN, ARRAY_ASSIGN, FATAL_ERROR = '0x0247bbf0', '0x02a279a0', '0x02b4d100', '0x024b55b0'
MAKE_HEAP, DEFAULT_HEAP = '0x02fa2b10', '0x05940670'
HEAP_INFO, ALLOCATOR_QUERY = '0x00500200', '0x00500300'
HEAP_OFFSET, INFO_SLOT, QUERY_SLOT = 0x458, 0x20, 0x10
VALID = 0x20
REQUEST = 0x148
STRINGS = (0x48, 0xa0, 0xd8)
NPOS = 0xffffffffffffffff


def put(raw, offset, data):
    raw[offset:offset + len(data)] = data


def string(raw, offset, text, owns, heap_buffer=None):
    """A wide string's fields at raw+offset (inline text; long text sits in heap_buffer)."""
    put(raw, offset + 0x18, struct.pack('<QQ', len(text), 7 if heap_buffer is None else len(text) + 2))
    if heap_buffer is None:
        put(raw, offset + 8, (text + '\0').encode('utf-16-le'))
    raw[offset + 0x30] = owns


def heap_info_stub(flags):
    stubs = [{'address': HEAP_INFO, 'argc': 3, 'argi': [1, 2],
              'writes': [{'arg': 0, 'offset': 0, 'bytes': f'{flags:02x}'}]}]
    if not flags & VALID:
        stubs.append({'address': FATAL_ERROR, 'argc': 3})
    return stubs


def copy_case(cid, texts=('c0000', 'a', ''), owns=(1, 0, 1), flags=VALID, same=False, fill=0xa5,
              source_fill=0x3c, long_name=False):
    raw = bytearray([source_fill]) * REQUEST
    buffers, memory = {}, []
    for i, (offset, text, own) in enumerate(zip(STRINGS, texts, owns)):
        heap_buffer = f'text{i}' if (long_name and i == 0) else None
        string(raw, offset, text, own, heap_buffer)
        if heap_buffer:
            buffers[heap_buffer] = {'size': 2 * len(text) + 8, 'bytes': (text + '\0').encode('utf-16-le').hex()}
            memory.append({'addr': f'buf:source+{offset + 8}', 'pointer': f'{heap_buffer}+0'})
        buffers[f'src_alloc{i}'] = {'size': 8}
        memory.append({'addr': f'buf:source+{offset + 0x28}', 'pointer': f'src_alloc{i}+0'})
        memory.append({'addr': f'buf:src_alloc{i}+0', 'pointer': 'query_vtable+0'})
    buffers.update({'source': {'size': REQUEST, 'bytes': raw.hex()},
                    'owner': {'size': HEAP_OFFSET + 8},
                    'heap_vtable': {'size': 0x40, 'bytes': 'e7' * 0x40},
                    'query_vtable': {'size': 0x20},
                    'copy_alloc0': {'size': 8}, 'copy_alloc1': {'size': 8}})
    target = 'source' if same else 'copy'
    if not same:
        buffers['copy'] = {'size': REQUEST, 'bytes': (bytes([fill]) * REQUEST).hex()}
    memory += [{'addr': f'buf:owner+{HEAP_OFFSET}', 'pointer': 'heap_vtable+0'},
               {'addr': f'buf:heap_vtable+{INFO_SLOT}', 'guest': HEAP_INFO},
               {'addr': f'buf:query_vtable+{QUERY_SLOT}', 'guest': ALLOCATOR_QUERY},
               {'addr': 'buf:copy_alloc0+0', 'pointer': 'query_vtable+0'},
               {'addr': 'buf:copy_alloc1+0', 'pointer': 'query_vtable+0'}]
    stubs = [{'address': HEAP_OWNER, 'argc': 0, 'ret': 'buf:owner'}] + heap_info_stub(flags)
    stubs.append({'address': ASSIGN, 'argc': 4, 'argmem': [{'arg': 0, 'size': 0x38}]})
    # The tail constructor gives the copy's second and third strings their allocators.
    stubs.append({'address': TAIL, 'argc': 1,
                  'writes': [{'arg': 0, 'offset': 0x28, 'pointer': 'copy_alloc0+0'},
                             {'arg': 0, 'offset': 0x60, 'pointer': 'copy_alloc1+0'}]})
    if not same:
        for _ in range(2):
            stubs += [{'address': ALLOCATOR_QUERY, 'argc': 1, 'ret': '0x77'},
                      {'address': ALLOCATOR_QUERY, 'argc': 1, 'ret': '0x88'},
                      {'address': ASSIGN, 'argc': 4, 'argmem': [{'arg': 0, 'size': 0x38}]}]
    stubs.append({'address': ARRAY_ASSIGN, 'argc': 2})
    return {'schema': 1, 'address': COPY, 'id': cid, 'returns': 'void',
            'args': {'rdi': f'buf:{target}', 'rsi': 'buf:source'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def tail_case(cid, flags=(VALID, VALID, VALID), present=True, fill=0xa5):
    stubs = []
    memory = [{'addr': 'buf:heap+0', 'pointer': 'heap_vtable+0'},
              {'addr': f'buf:heap_vtable+{INFO_SLOT}', 'guest': HEAP_INFO}]
    memory.append({'addr': DEFAULT_HEAP, **({'pointer': 'heap+0'} if present else {'bytes': '00' * 8})})
    if not present:
        stubs.append({'address': MAKE_HEAP, 'argc': 0, 'ret': 'buf:heap'})
    for f in flags:
        stubs += heap_info_stub(f)
    return {'schema': 1, 'address': TAIL, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:tail'},
            'buffers': {'tail': {'size': 0x98, 'bytes': (bytes([fill]) * 0x98).hex()},
                        'heap': {'size': 8},
                        'heap_vtable': {'size': 0x40, 'bytes': 'e7' * 0x40}},
            'memory': memory, 'imports': [], 'stubs': stubs}


def cases(which):
    if which == 'tail':
        return [tail_case('existing_heap'),
                tail_case('heap_made_on_first_use', present=False),
                tail_case('zero_fill', fill=0),
                tail_case('ones_fill', fill=0xff, flags=(0xff, 0xff, 0xff)),
                tail_case('first_heap_invalid', flags=(0, VALID, VALID)),
                tail_case('array_heap_invalid', flags=(VALID, VALID, 0xdf)),
                tail_case('all_invalid_made', present=False, flags=(0, 0, 0)),
                tail_case('second_heap_invalid_made', present=False, flags=(VALID, 0x1f, VALID))]
    return [copy_case('short_strings'),
            copy_case('long_name', texts=('chr/c2010.chrbnd.dcx', 'b', 'xyz'), long_name=True),
            copy_case('empty_strings', texts=('', '', ''), owns=(0, 0, 0)),
            copy_case('full_inline', texts=('1234567', '7654321', 'abcdefg'), owns=(1, 1, 1), fill=0),
            copy_case('onto_itself', same=True),
            copy_case('onto_itself_heap_invalid', same=True, flags=0),
            copy_case('heap_not_valid', flags=0xdf),
            copy_case('heap_other_flags', flags=0xff, fill=0xff, source_fill=0),
            copy_case('owns_bytes_2', owns=(2, 0xff, 0x80)),
            copy_case('ones_source', source_fill=0xff, fill=0x11)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ('copy', 'tail'):
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases(sys.argv[2])
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
