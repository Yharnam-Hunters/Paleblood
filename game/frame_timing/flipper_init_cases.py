#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for frame_timing_flipper_init (0x02434520), for tools/verify.py run.

usage: flipper_init_cases.py OUT_DIR

The config getter returns chosen modes (defaults, in range, the clamp boundary, out of range);
the wide string the constructor builds is short (no heap buffer, nothing freed) or long (freed
through its allocator's slot +0x70, a stand-in game function the case stubs).
"""
import json
import os
import struct
import sys

ADDRESS = '0x02434520'
FREE_FN = '0x00400020'


def case(cid, modes, heap=True, t_us=1_234_567_890_123):
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:flipper'},
         'buffers': {'flipper': {'size': 712}, 'config': {'size': 8}},
         'memory': [{'addr': '0x0593d710', 'pointer': 'config'}], 'stubs': [], 'imports': []}
    for n, mode in enumerate(modes):
        data, alloc = f'strdata{n}', f'stralloc{n}'
        c['buffers'][data] = {'size': 8}
        capacity = 15 if heap else 7
        writes = [{'arg': 0, 'offset': 8, 'pointer': data},
                  {'arg': 0, 'offset': 32, 'bytes': struct.pack('<Q', capacity).hex()}]
        if heap:
            c['buffers'][alloc] = {'size': 8}
            c['buffers'][alloc + '_vt'] = {'size': 120}
            c['memory'] += [{'addr': f'buf:{alloc}', 'pointer': alloc + '_vt'},
                            {'addr': f'buf:{alloc}_vt+112', 'guest': FREE_FN}]
            writes.append({'arg': 0, 'offset': 40, 'pointer': alloc})
        c['stubs'].append({'address': '0x02bc0e00', 'argc': 2, 'writes': writes})
        c['stubs'].append({'address': '0x024eba00', 'argc': 3, 'ret': mode})
        if heap:
            c['stubs'].append({'address': FREE_FN, 'argc': 2})
    tv = struct.pack('<qq', t_us // 1_000_000, t_us % 1_000_000)
    c['imports'].append({'name': 'gettimeofday', 'argc': 2, 'ret': 0,
                         'writes': [{'arg': 0, 'offset': 0, 'size': 16, 'bytes': tv.hex()}]})
    return c


CASES = [
    case('defaults', (4, 3)),
    case('mode_60', (2, 2)),
    case('zero', (0, 0)),
    case('boundary', (4, 4)),
    case('clamped', (5, 7)),
    case('huge', (0xffffffff, 0x80000000)),
    case('short_string_no_free', (4, 3), heap=False),
    case('clock_zero', (1, 3), t_us=0),
    case('clock_other', (3, 1), t_us=987_654_321),
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
