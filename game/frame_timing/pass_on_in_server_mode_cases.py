#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for passing the gate's handle on in the server modes
(game/frame_timing/pass_on_in_server_mode.cpp), for tools/verify.py run.

usage: pass_on_in_server_mode_cases.py OUT_DIR

The request maker (0x02264640) and the call that gives the request the handle (0x02269e30) are
stubbed and compared: the service each is given, the handle's address and the kind. Covers an
empty handle (no calls, false), each kind and values past them (negative, the 32-bit ends, a kind
with garbage above its low 32 bits), the giving call's answer (true, false, and answers whose
upper bits are set), a null request, a null service, and a missing FrpgNetMan that the report
makes present (the second read is used). Not covered: a FrpgNetMan still missing after the report,
which faults on the read of +0xca8 in the original.
"""
import json
import os
import struct
import sys

PASS_ON = '0x02265960'
FRPG_NET_MAN, FATAL_ERROR = '0x0593b120', '0x024b55b0'
MAKE_REQUEST, GIVE_HANDLE = '0x02264640', '0x02269e30'
REPORT_FILE = 0x04d3b369  # the singleton check's source file name, the report's first argument
SERVICE_FIELD = 0xca8  # FrpgNetMan's pass-on service
NET_SIZE = SERVICE_FIELD + 0x10


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, kind=1, handle=True, answer=1, request=True, service=True, net='present'):
    """net: 'present', or 'late' (missing until the report, which stores it)."""
    c = {'schema': 1, 'address': PASS_ON, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:holder', 'rsi': kind & 0xffffffffffffffff},
         'buffers': {'holder': {'size': 16}, 'object': {'size': 0x130}, 'net': {'size': NET_SIZE},
                     'service': {'size': 0x40}, 'request': {'size': 0xe0}},
         'memory': [{'addr': 'buf:holder', 'pointer': 'object'} if handle else {'addr': 'buf:holder', 'bytes': '00' * 8},
                    {'addr': 'buf:holder+8', 'bytes': q(0x1122334455667788)},
                    {'addr': 'buf:object+8', 'bytes': '01000000'},
                    {'addr': f'buf:net+{SERVICE_FIELD}', 'pointer': 'service'} if service
                    else {'addr': f'buf:net+{SERVICE_FIELD}', 'bytes': '00' * 8},
                    {'addr': FRPG_NET_MAN, 'pointer': 'net'} if net == 'present'
                    else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8}],
         'imports': [], 'stubs': []}
    if net == 'late':
        # Stubs write only through their arguments: the report's first one is the source file's
        # name in the image, so the singleton's slot is reached from it.
        c['stubs'].append({'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3],
                           'writes': [{'arg': 0, 'offset': int(FRPG_NET_MAN, 16) - REPORT_FILE, 'pointer': 'net'}]})
    if handle:
        c['stubs'].append({'address': MAKE_REQUEST, 'argc': 1, 'argi': [0], 'ret': 'buf:request' if request else 0})
        c['stubs'].append({'address': GIVE_HANDLE, 'argc': 3, 'argi': [0, 1], 'argu32': [2],
                           'argmem': [{'arg': 1, 'size': 16}], 'ret': answer})
    return c


def cases():
    out = [case('no_handle', handle=False), case('no_handle_kind_2', kind=2, handle=False),
           case('no_handle_late_net', handle=False, net='late')]
    for kind in (1, 2, 0, 3, -1, 0x7fffffff, -0x80000000):
        out.append(case(f'kind_{kind & 0xffffffff:#010x}', kind=kind))
    out += [case('kind_upper_garbage', kind=0xdeadbeef00000002),
            case('answer_false', answer=0), case('answer_false_kind_2', kind=2, answer=0),
            case('answer_upper_bits_true', answer=0xffffff01), case('answer_upper_bits_false', answer=0xffffff00),
            case('answer_two', answer=2),
            case('null_request', request=False), case('null_request_false', request=False, answer=0),
            case('null_service', service=False), case('late_net', net='late'),
            case('late_net_kind_2_false', kind=2, net='late', answer=0)]
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
