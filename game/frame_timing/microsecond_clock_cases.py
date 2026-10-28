#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge fixtures for the monotonic microsecond clock at 0x0111a760.

These cover the shared clock state, unsigned truncation and wrapping arithmetic. They are
not recordings from a game run. Use tools/verify.py against the pinned original executable.
"""
import json
from pathlib import Path
import sys

from monotonic_clock_cases import case


ADDRESS = '0x0111a760'
CASES = [
    case('first_call', 0, 1000, 123456789),
    case('later_call', 1000, 1005, 250000000),
    case('same_second', 1000, 1000, 0),
    case('nsec_below_microsecond', 1000, 1001, 999),
    case('nsec_exact_microsecond', 1000, 1001, 1000),
    case('nsec_above_microsecond', 1000, 1001, 1001),
    case('nsec_max_valid', 1000, 1001, 999999999),
    case('seconds_before_base', 2000, 1999, 500000000),
    case('microsecond_wrap', 1, 4296, 999999999),
    case('seconds_above_32_bits', 7, (1 << 32) + 9, 1000),
    case('negative_base', -5, 3, 1000),
    case('seconds_low_word_zero', 0, 1 << 32, 1000),
    case('negative_seconds', 0, -1, 0),
    case('nsec_high_word_truncated', 1000, 1001, (1 << 32) + 1000),
    case('nsec_unsigned_low_word', 1000, 1001, -1),
    case('clock_fails', 1000, 0, 0, ret=0x80020016),
    case('first_clock_fails', 0, 1000, 999999999, ret=-1),
    case('positive_clock_failure', 1000, 1001, 1000, ret=1),
]

# Error handling must return before dereferencing either pointer. A successful output may
# also alias the shared base field; its final store follows initialization and elapsed time.
null_failure = case('failed_clock_null_pointers', 0, 0, 0, ret=1)
null_failure['args']['rdi'] = 0
null_failure['memory'] = [{'addr': '0x056d6ae8', 'bytes': '00' * 8}]
CASES.append(null_failure)
alias_output = case('output_aliases_base', 0, 1000, 123456789)
alias_output['args']['rdi'] = 'buf:state+0x10'
CASES.append(alias_output)


def main():
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        fixture = {**fixture, 'address': ADDRESS}
        (output / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
