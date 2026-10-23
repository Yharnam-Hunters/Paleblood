#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic bounded signed-string cases, including the original's nonpositive-length quirk."""
import json
from pathlib import Path
import sys

from string_compare_cases import case as string_case


ADDRESS = '0x011c4990'


def case(cid, left, right, length):
    """Add a signed comparison bound to independently mapped synthetic byte strings."""
    fixture = string_case(cid, left, right)
    fixture['address'] = ADDRESS
    fixture['args']['rdx'] = length
    return fixture


CASES = [
    case('both_empty', b'', b'', 23),
    case('equal_ascii', b'FMOD nonblocking thread', b'FMOD nonblocking thread', 23),
    case('prefix_only', b'prefix-left', b'prefix-right', 6),
    case('difference_at_bound', b'prefix-left', b'prefix-right', 8),
    case('difference_after_bound', b'prefix-left', b'prefix-right', 7),
    case('one_byte_equal', b'az', b'aA', 1),
    case('two_bytes_differ', b'az', b'aA', 2),
    case('upper_length_bits_ignored', b'az', b'aA', (1 << 32) + 1),
    case('zero_length_different_first_byte', b'z', b'A', 0),
    case('zero_length_ignores_second_byte', b'az', b'aA', 0),
    case('negative_length_different_first_byte', b'A', b'z', -1),
    case('negative_length_ignores_second_byte', b'az', b'aA', -1),
    case('minimum_signed_length', b'\x80', b'\x7f', -(1 << 31)),
    case('maximum_signed_length', b'a' * 257, b'a' * 257, (1 << 31) - 1),
    case('left_terminates', b'prefix', b'prefix suffix', 23),
    case('right_terminates', b'prefix suffix', b'prefix', 23),
    case('nul_ends_comparison', b'equal\0different-left', b'equal\0different-right', 100),
    case('high_bit_left', b'\x80', b'\x7f', 1),
    case('high_bit_right', b'\x7f', b'\x80', 1),
    case('negative_bytes_difference', b'\x80', b'\xff', 1),
    case('zero_minus_negative_byte', b'', b'\xff', 0),
    case('negative_byte_minus_zero', b'\x80', b'', -1),
    case('equal_negative_bytes', b'\x80\xff\x81', b'\x80\xff\x81', 3),
]
aliased = case('identical_pointer', b'\xff shared string', b'unused', (1 << 31) - 1)
aliased['args']['rsi'] = 'buf:left'
CASES.append(aliased)


def main():
    """Write the synthetic case set into a caller-selected private directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (output / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
