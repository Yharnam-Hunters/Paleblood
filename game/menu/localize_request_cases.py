#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the localize request clear (game/menu/localize_request.cpp), for tools/verify.py run.

usage: localize_request_cases.py OUT_DIR

The language object is an invented buffer whose loads (+0x30, +0x38) point at two invented load
buffers. Each load's state (+0xc) takes the awaited value 3 and values next to it and with upper
bits set (0, 2, 4, 0x103, 0x80000003, -1, INT_MAX), so only the whole 32-bit value 3 counts, and
each load is checked on its own. The language object's own state starts at several values (it is
only written, never read). The request byte (0x05973810) starts clear, set, or at other nonzero
values, and the bytes next to it are set too, so a wider write shows up. One case has no second
load (null) while the first is not in the awaited state: the second must not be read then. One
case makes the first load the language object itself. Generated fixtures, not inputs recorded in
a game run.
"""
import json
import os
import struct
import sys

ADDRESS = '0x01d53420'
REQUEST_BYTE = 0x05973810
LOAD_STATE, LANGUAGE_STATE, FIRST_LOAD, SECOND_LOAD = 0x0c, 0x0c, 0x30, 0x38
AWAITED = 3
OTHER_STATES = (0, 2, 4, 0x103, 0x80000003, 0xffffffff, 0x7fffffff)


def buffer(size, fill, state):
    data = bytearray.fromhex(fill * size)
    struct.pack_into('<I', data, LOAD_STATE, state)
    return {'size': size, 'bytes': data.hex()}


def case(cid, first, second, language_state=1, request=0x01, neighbour='a5',
         second_missing=False, first_is_language=False):
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
         'buffers': {'language': buffer(0x48, '3c', language_state)},
         'args': {'rdi': 'buf:language'}, 'imports': [], 'stubs': [],
         'memory': [{'addr': hex(REQUEST_BYTE - 1), 'bytes': neighbour},
                    {'addr': hex(REQUEST_BYTE), 'bytes': f'{request:02x}'},
                    {'addr': hex(REQUEST_BYTE + 1), 'bytes': neighbour}]}
    b, m = c['buffers'], c['memory']
    if first_is_language:
        struct_bytes = bytearray.fromhex(b['language']['bytes'])
        struct.pack_into('<I', struct_bytes, LANGUAGE_STATE, first)
        b['language']['bytes'] = struct_bytes.hex()
        m.append({'addr': f'buf:language+{FIRST_LOAD:#x}', 'pointer': 'language'})
    else:
        b['first'] = buffer(0x20, '5a', first)
        m.append({'addr': f'buf:language+{FIRST_LOAD:#x}', 'pointer': 'first'})
    if second_missing:
        m.append({'addr': f'buf:language+{SECOND_LOAD:#x}', 'bytes': '00' * 8})
    else:
        b['second'] = buffer(0x20, '6e', second)
        m.append({'addr': f'buf:language+{SECOND_LOAD:#x}', 'pointer': 'second'})
    return c


def cases():
    out = [case('both_awaited', AWAITED, AWAITED)]
    for s in OTHER_STATES:
        out.append(case(f'first_{s:x}', s, AWAITED))
        out.append(case(f'second_{s:x}', AWAITED, s))
    out.append(case('neither_awaited', 0, 0))
    for request in (0x00, 0x02, 0x80, 0xff):
        for neighbour in ('00', 'ff'):
            out.append(case(f'request_{request:02x}_next_{neighbour}', AWAITED, AWAITED, request=request,
                            neighbour=neighbour))
            out.append(case(f'request_{request:02x}_next_{neighbour}_not_awaited', AWAITED, 2,
                            request=request, neighbour=neighbour))
    for state in (0, 8, 0xffffffff):
        out.append(case(f'language_state_{state:x}', AWAITED, AWAITED, language_state=state))
    out.append(case('no_second_load', 2, 0, second_missing=True))
    out.append(case('first_load_is_language', AWAITED, AWAITED, first_is_language=True))
    out.append(case('first_load_is_language_not_awaited', 4, AWAITED, first_is_language=True))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
