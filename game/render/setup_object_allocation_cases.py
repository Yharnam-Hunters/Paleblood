#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the swap-chain setup's object creation (game/render/setup_object_allocation.cpp),
for tools/verify.py run.

usage: setup_object_allocation_cases.py OUT_DIR

The owner's vtable slot 0x58 points at a stubbed fake address; the stub records all six argument
registers and returns a value. Cases vary the two values (with high bits set, which the original
passes on whole), the services, information and candidate registers (dropped, dropped, passed on),
and the allocator's result (zero, small, all bits).
"""
import json
import os
import sys

ADDRESS = '0x0247c4e0'
ALLOCATE = '0x00500200'  # any address: the vtable slot points at it and it is stubbed


def case(cid, first=0x40, second=0x10, services=0x1111, info=0x2222, candidate=0x3333, result=0x7f0012340000):
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
            'args': {'rdi': first, 'rsi': second, 'rdx': 'buf:owner+0x0', 'rcx': services, 'r8': info,
                     'r9': candidate},
            'buffers': {'owner': {'size': 0x10, 'bytes': '00' * 0x10}, 'vtable': {'size': 0x80, 'bytes': 'cc' * 0x80}},
            'memory': [{'addr': 'buf:owner+0x0', 'pointer': 'vtable+0x0'},
                       {'addr': 'buf:vtable+0x58', 'guest': ALLOCATE}],
            'imports': [], 'stubs': [{'address': ALLOCATE, 'argc': 6, 'ret': result}]}


def cases():
    return [case('typical'),
            case('zero_values', first=0, second=0),
            case('high_bits', first=0xffffffff00000040, second=0x8000000000000010),
            case('all_bits', first=0xffffffffffffffff, second=0xffffffffffffffff),
            case('services_and_info_dropped', services=0xdeadbeefcafef00d, info=0x0123456789abcdef),
            case('candidate_null', candidate=0),
            case('candidate_all_bits', candidate=0xffffffffffffffff),
            case('result_null', result=0),
            case('result_all_bits', result=0xffffffffffffffff)]


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
