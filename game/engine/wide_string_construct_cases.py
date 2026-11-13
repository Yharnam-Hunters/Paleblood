#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the wide string's constructor from text (game/engine/wide_string_construct.cpp),
for tools/verify.py run.

usage: wide_string_construct_cases.py OUT_DIR

The string (`string`, 0x38 bytes) starts as a pattern, so the fields the constructor leaves alone
(the first word, the owns byte) show. The default allocator global points at a fake heap whose info
slot is a scripted stub writing the chosen flags (eight bytes: the original keeps its stack guard
right after them), or is null and the creation (0x02fa2b10) is a stub returning that heap. The counted assign (0x02a2a720) is a stub that records the string as the
constructor leaves it and the text; wcslen is a scripted import; an invalid heap's report
(0x024b55b0) is a stub that returns, as the game's report does.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02bc0e00'
DEFAULT_ALLOCATOR = '0x05940670'
CREATE_DEFAULT, ASSIGN_COUNTED, FATAL_ERROR = '0x02fa2b10', '0x02a2a720', '0x024b55b0'
INFO = '0x00500100'
INFO_SLOT = 0x20
GUARD_GOT = '0x057e3be0'
VALID = 0x20
STRING = 0x38


def case(cid, text, flags=VALID, present=True, created_flags=None, length=None):
    """text: the characters before the terminator (a str), or raw UTF-16 bytes."""
    raw = text if isinstance(text, bytes) else text.encode('utf-16-le') + b'\0\0'
    buffers = {
        'string': {'size': STRING, 'bytes': '6b' * STRING},
        'text': {'size': len(raw), 'bytes': raw.hex()},
        'guard': {'size': 8, 'bytes': struct.pack('<Q', 0x0123456789abcdef).hex()},
        'heap': {'size': 0x10, 'bytes': '3c' * 0x10},
        'heap_vtable': {'size': 0x80, 'bytes': 'e7' * 0x80},
    }
    memory = [{'addr': GUARD_GOT, 'pointer': 'guard'},
              {'addr': 'buf:heap+0', 'pointer': 'heap_vtable+0'},
              {'addr': f'buf:heap_vtable+{INFO_SLOT}', 'guest': INFO}]
    stubs, imports = [], []
    if present:
        memory.append({'addr': DEFAULT_ALLOCATOR, 'pointer': 'heap'})
    else:
        memory.append({'addr': DEFAULT_ALLOCATOR, 'bytes': struct.pack('<Q', 0).hex()})
        stubs.append({'address': CREATE_DEFAULT, 'argc': 0, 'ret': 'buf:heap'})
        if created_flags is not None:
            flags = created_flags
    stubs.append({'address': INFO, 'argc': 3,
                  'writes': [{'arg': 0, 'bytes': (bytes([flags]) + b'\x11' * 7).hex()}]})
    if not flags & VALID:
        stubs.append({'address': FATAL_ERROR, 'argc': 3})
    if raw[:2] != b'\0\0':
        imports.append({'name': 'wcslen', 'argc': 1, 'ret': len(text) if length is None else length})
    stubs.append({'address': ASSIGN_COUNTED, 'argc': 3, 'ret': 'buf:string',
                  'argmem': [{'arg': 0, 'size': STRING}, {'arg': 1, 'size': len(raw)}]})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:string', 'rsi': 'buf:text'},
            'buffers': buffers, 'memory': memory, 'imports': imports, 'stubs': stubs}


CASES = [
    case('empty_text', ''),
    case('empty_text_chars_after_terminator', b'\0\0a\0b\0\0\0'),
    case('one_char', 'a'),
    case('short', 'abc'),
    case('inline_full_seven', 'seven77'),
    case('eight_needs_heap', 'eight888'),
    case('long', 'a rather longer piece of text for the heap'),
    case('non_ascii', 'é中￿'),
    case('allocator_created', 'abc', present=False),
    case('allocator_created_empty_text', '', present=False),
    case('allocator_created_invalid', 'xy', present=False, created_flags=0),
    case('invalid_heap_reported', 'abc', flags=0),
    case('invalid_heap_other_flags', 'abc', flags=0xdf),
    case('invalid_heap_empty_text', '', flags=0x1f),
    case('valid_flag_only', 'abc', flags=VALID),
    case('all_flags', 'abc', flags=0xff),
    case('wcslen_answer_used', 'abcdef', length=3),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
