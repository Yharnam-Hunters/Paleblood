#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for constructing a resource load request (game/loading/load_request_construct.cpp),
for tools/verify.py run.

usage: load_request_construct_cases.py OUT_DIR

Each case gives a request buffer (0x148 bytes, filled with a pattern), a synthetic UTF-16 name and
the word to keep. The request heap's owner (0x0247bbf0) is a scripted stub returning a fake object
whose allocator at +0x458 has a fake vtable; its info slot is a stub that writes the flags byte.
The error report, wcslen, the string assign (0x02a2a720) and the tail constructor (0x024be8e0)
are scripted too.
"""
import json
import os
import struct
import sys

ADDRESS = '0x024be250'
HEAP_OWNER, ASSIGN, TAIL, FATAL_ERROR = '0x0247bbf0', '0x02a2a720', '0x024be8e0', '0x024b55b0'
HEAP_INFO = '0x00500200'
HEAP_OFFSET, INFO_SLOT = 0x458, 0x20
VALID = 0x20


def case(cid, name, word=0x1234, flags=VALID, fill=0xa5):
    text = (name + '\0').encode('utf-16-le')
    stubs = [{'address': HEAP_OWNER, 'argc': 0, 'ret': 'buf:owner'},
             {'address': HEAP_INFO, 'argc': 3, 'argi': [1, 2],
              'writes': [{'arg': 0, 'offset': 0, 'bytes': f'{flags:02x}'}]}]
    if not flags & VALID:
        stubs.append({'address': FATAL_ERROR, 'argc': 3})
    imports = []
    if name:
        imports.append({'name': 'wcslen', 'argc': 1, 'ret': len(name)})
    stubs += [{'address': ASSIGN, 'argc': 3, 'argmem': [{'arg': 0, 'size': 0x30}]},
              {'address': TAIL, 'argc': 1}]
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:request', 'rsi': 'buf:name', 'rdx': hex(word)},
            'buffers': {'request': {'size': 0x148, 'bytes': (bytes([fill]) * 0x148).hex()},
                        'name': {'size': len(text), 'bytes': text.hex()},
                        'owner': {'size': HEAP_OFFSET + 8, 'bytes': ''},
                        'heap_vtable': {'size': 0x40, 'bytes': 'e7' * 0x40}},
            'memory': [{'addr': f'buf:owner+{HEAP_OFFSET}', 'pointer': 'heap_vtable+0'},
                       {'addr': f'buf:heap_vtable+{INFO_SLOT}', 'guest': HEAP_INFO}],
            'imports': imports, 'stubs': stubs}


def cases():
    return [
        case('short_name', 'c0000'),
        case('long_name', 'chr/c2010.chrbnd.dcx', word=0xffffffffffffffff),
        case('empty_name', ''),
        case('one_character', 'x', word=0),
        case('heap_not_valid', 'param', flags=0xdf),
        case('heap_other_flags', 'param', flags=0xff, fill=0x00),
        case('heap_valid_only', 'm10_00_00_00', flags=VALID, fill=0xff),
    ]


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
