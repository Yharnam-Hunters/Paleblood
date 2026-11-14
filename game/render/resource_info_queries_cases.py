#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the swap-chain resource info queries (game/render/resource_info_queries.cpp),
for tools/verify.py run.

usage: resource_info_queries_cases.py OUT_DIR mode|copy|alternate|storage

- mode: the shared counter at 0, just below and at the 24-bit limit, and at the 32-bit wrap.
- copy: every selector from -2 to 0x60, the ends of the signed range, and large codes whose low
  byte names a group without a copy (the whole value is compared).
- alternate / storage: a null address (no query), a failed query (with a protection word that
  would admit), and every protection bit alone, none, all, and bits above the low byte only.
"""
import json
import os
import sys

ADDRESSES = {'mode': '0x02abaef0', 'copy': '0x02abb4b0', 'alternate': '0x02abad70', 'storage': '0x02abadd0'}
RETURNS = {'mode': 'i32', 'copy': 'i8', 'alternate': 'i8', 'storage': 'i8'}
COUNTER = '0x056ba180'
GUARD_GOT = '0x057e3be0'


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def mode_case(counter):
    c = base('mode', f'counter_{counter:#010x}')
    c['memory'] = [{'addr': COUNTER, 'bytes': counter.to_bytes(4, 'little').hex()}]
    return c


def copy_case(selector):
    c = base('copy', f'selector_{selector & 0xffffffff:#010x}')
    c['args'] = {'rdi': selector & 0xffffffff}
    return c


def admitted_case(variant, cid, address=0x7f0000001000, status=0, protection=None):
    c = base(variant, cid)
    c['args'] = {'rdi': address}
    c['buffers'] = {'guard': {'size': 0x10, 'bytes': '5a' * 0x10}}
    c['memory'] = [{'addr': GUARD_GOT, 'pointer': 'guard'}]
    if address:
        entry = {'name': 'sceKernelQueryMemoryProtection', 'argc': 4, 'ret': status}
        if protection is not None:
            entry['writes'] = [{'arg': 3, 'bytes': (protection & 0xffffffff).to_bytes(4, 'little').hex()}]
        c['imports'] = [entry]
    return c


def cases(variant):
    if variant == 'mode':
        return [mode_case(v) for v in (0, 1, 0x123456, 0xfffffe, 0xffffff, 0x1000000, 0xfffffffe, 0xffffffff)]
    if variant == 'copy':
        selectors = list(range(-2, 0x61)) + [0x7fffffff, -0x80000000, 0x113, 0x10000059, 0x80000000]
        return [copy_case(s) for s in selectors]
    out = [admitted_case(variant, 'null_address', address=0),
           admitted_case(variant, 'query_fails', status=0x80020016, protection=0xff),
           admitted_case(variant, 'query_fails_negative_one', status=0xffffffff, protection=0xff),
           admitted_case(variant, 'protection_none', protection=0),
           admitted_case(variant, 'protection_all', protection=0xffffffff),
           admitted_case(variant, 'protection_above_low_byte', protection=0xffffff00),
           admitted_case(variant, 'high_address', address=0xffffffffffff0000, protection=0x11)]
    out += [admitted_case(variant, f'protection_bit_{b}', protection=1 << b) for b in range(8)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
