#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for kernel_condition_wait (0x02483e80), for tools/verify.py run.

usage: condition_wait_cases.py OUT_DIR

Untimed (-1) and timed waits, success, timeout, unknown errors and a null mutex. The fatal
error function (0x024b55b0) never returns in the game; the cases stub it, so the code after it
runs too (EPERM goes on to the unknown-error report, EINVAL returns -3).
"""
import json
import os
import sys

FATAL = '0x024b55b0'


def case(cid, timeout=-1, ret=0, mutex=True, fatal_ret=None):
    c = {'schema': 1, 'address': '0x02483e80', 'id': cid, 'returns': 'i32',
         'args': {'rdi': 'buf:cond', 'rsi': 'buf:mutex' if mutex else '0x0', 'rdx': hex(timeout & 0xffffffff)},
         'buffers': {'cond': {'size': 8}, 'mutex': {'size': 8}}, 'memory': [], 'stubs': [],
         'imports': [{'name': 'scePthreadCondWait' if timeout == -1 else 'scePthreadCondTimedwait',
                      'argc': 2 if timeout == -1 else 3, 'ret': ret}]}
    if fatal_ret is not None:
        c['stubs'] = [{'address': FATAL, 'argc': 3, 'ret': r} for r in fatal_ret]
    return c


EPERM, ETIMEDOUT, OTHER = 0x80020001 - (1 << 32), 0x8002003c - (1 << 32), 0x80020005 - (1 << 32)
CASES = [
    case('untimed_ok'),
    case('timed_ok', timeout=16_666),
    case('timed_zero', timeout=0),
    case('timed_max', timeout=0x7fffffff),
    case('timed_out', timeout=1000, ret=ETIMEDOUT),
    case('untimed_timed_out_code', ret=ETIMEDOUT),
    case('positive_result', ret=5, fatal_ret=[0]),
    case('unknown_error', ret=OTHER, fatal_ret=[0]),
    case('eperm', ret=EPERM, fatal_ret=[0, 0]),
    case('eperm_fatal_returns_timeout_code', ret=EPERM, fatal_ret=[ETIMEDOUT, 0]),
    case('einval', ret=0x80020016 - (1 << 32), fatal_ret=[0]),
    case('null_mutex', mutex=False, fatal_ret=[0]),
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
