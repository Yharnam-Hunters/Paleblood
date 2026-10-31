#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the current-thread wrapper at 0x0111a8a0.

The scripted system call returns an opaque 64-bit value. Canaries on both sides of the
output detect writes beyond that handle. These are not recordings from a game run.
"""
import json
from pathlib import Path
import sys


ADDRESS = '0x0111a8a0'


def case(cid, handle):
    """Preserve the full system-call result in the middle of a canary-filled buffer."""
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': 'buf:out+0x8'},
        'buffers': {'out': {'size': 24, 'bytes': 'a5' * 24}},
        'imports': [{'name': 'scePthreadSelf', 'argc': 0, 'ret': hex(handle)}],
    }


CASES = [
    case('zero_handle', 0),
    case('one_handle', 1),
    case('low_word_handle', 0x89abcdef),
    case('high_word_handle', 0x1234567800000000),
    case('mixed_words_handle', 0x123456789abcdef0),
    case('high_bit_handle', 1 << 63),
    case('all_bits_handle', (1 << 64) - 1),
]


def main():
    """Write the synthetic fixture set into the caller-selected private directory."""
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
