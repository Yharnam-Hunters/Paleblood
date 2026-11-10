#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for loading_stdio_stream_open (0x00464b90).

usage: stdio_stream_open_cases.py OUT_DIR

The C library is scripted. Every combination of the four mode bits that pick the fopen mode
(compared as the executable's own mode strings), path pointers with either tag bit set, a
successful open, and failed opens with each errno the function maps (fetched again for every
comparison, so the number of __error calls is compared) and others. One case has rewind clear
the stream's FILE pointer: fopen returns the address of the stream's own FILE field, so rewind's
argument is that field and its scripted write clears it. The original reads the field again
after rewind and treats the stream as failed. Generated fixtures, not inputs recorded in a game
run.
"""
import json
import os
import struct
import sys

ADDRESS = '0x00464b90'
PATH = b'/hostapp/system/log.txt\0'
ERRNO_CHECKS = {2: 1, 13: 2, 1: 3, 28: 4}   # errno -> __error calls before the answer


def case(cid, mode, opened=True, errno=0, tag=0, other_bits=0, rewind_clears=False):
    stream = bytearray(b'\x4e' * 0x38)
    struct.pack_into('<I', stream, 0x28, mode | other_bits)
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:stream'},
         'buffers': {'stream': {'size': 0x38, 'bytes': stream.hex()},
                     'path': {'size': 0xc + len(PATH), 'bytes': ('70' * 0xc) + PATH.hex()},
                     'errno': {'size': 8, 'bytes': struct.pack('<i', errno).hex()}, 'file': {'size': 0x10}},
         'memory': [{'addr': 'buf:stream+0x10', 'pointer': f'path+{tag}'}], 'imports': []}
    fopen = {'name': 'fopen', 'argc': 2, 'argmem': [{'arg': 0, 'size': len(PATH)}],
             'ret': ('buf:stream+0x20' if rewind_clears else 'buf:file') if opened else '0x0'}
    c['imports'].append(fopen)
    if opened:
        rewind = {'name': 'rewind', 'argc': 1}
        if rewind_clears:
            rewind['writes'] = [{'arg': 0, 'bytes': '00' * 8}]
        c['imports'].append(rewind)
    if not opened or rewind_clears:
        for _ in range(ERRNO_CHECKS.get(errno, 4)):
            c['imports'].append({'name': '__error', 'argc': 0, 'ret': 'buf:errno'})
    return c


CASES = []
for mode in range(16):
    CASES.append(case(f'mode_{mode:x}_opened', mode))
for errno in (2, 13, 1, 28, 0, 5, 17, -1):
    CASES.append(case(f'fails_errno_{errno & 0xffffffff:x}', 0x1, opened=False, errno=errno))
CASES += [
    case('path_tag_1', 0x2, tag=1),
    case('path_tag_2', 0x4, tag=2),
    case('path_tag_3', 0x8, tag=3),
    case('upper_mode_bits_ignored', 0x5, other_bits=0xfffffff0),
    case('upper_mode_bits_fail', 0x8, opened=False, errno=28, other_bits=0x7ffffff0),
    case('rewind_clears_file', 0x1, errno=13, rewind_clears=True),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in CASES:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(CASES)} synthetic cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
