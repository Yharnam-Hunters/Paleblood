#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the swap-chain present-mode facade (0x02ad5dd0).

Generate outside the repository: swap_chain_present_mode_cases.py OUT_DIR.
Covers all accepted modes, unsigned rejection before an object read, full-width loaded-source forwarding,
object retention and propagation of the callee's low 32-bit result. These are not recordings.
"""
import json
from pathlib import Path
import sys


def case(cid, source_mode, result=0, source_offset=0):
    mode = source_mode & 0xffffffff
    valid = mode <= 4
    return {
        'schema': 1, 'address': '0x02ad5dd0', 'id': cid, 'returns': 'i32',
        'args': {'rdi': 'buf:self' if valid else 0, 'rsi': hex(source_mode)},
        'buffers': {'self': {'size': 0x33d8}, 'source': {'size': 64}} if valid else {},
        'memory': [{'addr': 'buf:self+13264', 'pointer': f'source+{source_offset}'}] if valid else [],
        'stubs': [{'address': '0x02ad6e50', 'argc': 3, 'argi': [0, 1], 'argu32': [2], 'ret': hex(result)}] if valid else [],
    }


def cases():
    for mode in range(5):
        yield case(f'mode_{mode}', mode, result=mode + 9, source_offset=mode * 8)
    for mode in (5, 8, 0x7fffffff, 0x80000000, 0xffffffff):
        yield case(f'reject_{mode:08x}_null_object', mode)
    for mode in range(5):
        yield case(f'upper_bits_mode_{mode}', 0x89abcdef00000000 | mode,
                   result=0x80020016, source_offset=24)
    yield case('all_bits_set_rejected', 0xffffffffffffffff)
    yield case('negative_result', 1, result=0xffffffff)
    yield case('zero_result', 2)
    yield case('return_low_bits', 3, result=0x1234567800000007)


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for entry in generated:
        (out / (entry['id'] + '.json')).write_text(json.dumps(entry, indent=2) + '\n')
    print(f'{len(generated)} synthetic edge cases written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
