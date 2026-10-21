#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic signed-byte comparison cases; no gameplay recordings are included."""
import json
from pathlib import Path
import sys


ADDRESS = '0x011c4970'


def case(cid, left, right):
    """Compare two separately mapped, terminated byte strings without writable outputs."""
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': 'buf:left', 'rsi': 'buf:right'},
        'buffers': {key: {'size': len(value) + 1, 'bytes': (value + b'\0').hex()}
                    for key, value in [('left', left), ('right', right)]},
        'imports': [],
    }


CASES = [
    case('both_empty', b'', b''),
    case('equal_ascii', b'FMOD mixer thread', b'FMOD mixer thread'),
    case('left_empty', b'', b'A'),
    case('right_empty', b'A', b''),
    case('positive_difference', b'z', b'A'),
    case('negative_difference', b'A', b'z'),
    case('left_is_prefix', b'prefix', b'prefix suffix'),
    case('right_is_prefix', b'prefix suffix', b'prefix'),
    case('late_difference', b'a' * 257 + b'z', b'a' * 257 + b'A'),
    case('zero_ends_comparison', b'equal\0ignored left', b'equal\0ignored right'),
    case('high_bit_left', b'\x80', b'\x7f'),
    case('high_bit_right', b'\x7f', b'\x80'),
    case('negative_bytes_difference', b'\x80', b'\xff'),
    case('negative_bytes_reverse', b'\xff', b'\x80'),
    case('zero_minus_negative_byte', b'', b'\xff'),
    case('negative_byte_minus_zero', b'\x80', b''),
    case('equal_negative_bytes', b'\x80\xff\x81', b'\x80\xff\x81'),
]
aliased = case('identical_pointer', b'\xff shared string', b'unused')
aliased['args']['rsi'] = 'buf:left'
CASES.append(aliased)


def main():
    """Write the synthetic case set to a private directory selected by the caller."""
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
