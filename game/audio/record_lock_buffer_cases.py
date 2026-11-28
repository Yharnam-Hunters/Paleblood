#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic FMOD capture-buffer lock cases."""
import json
from pathlib import Path
import struct
import sys


def case(name, capacity, offset, length, slot=0, layout='separate', capture=0x123456789abcdef0):
    owner = 64
    record = 1760
    arena = bytearray(b'\xa5' * 2048)
    # Distinct nearby entries expose wrong indices and strides.
    for i in range(-3, 13):
        struct.pack_into('<Q', arena, owner + 0x2d0 + 8 * i, 0x3344556600000100 + i)
        struct.pack_into('<I', arena, owner + 0x4d0 + 4 * i, 43 + i)
    size_field = owner + 0x4d0 + 4 * slot
    buffer_field = owner + 0x2d0 + 8 * slot
    struct.pack_into('<I', arena, size_field, capacity)
    struct.pack_into('<Q', arena, buffer_field, capture)
    struct.pack_into('<i', arena, record + 24, slot)
    outputs = {
        'separate': (1856, 1872, 1888, 1904),
        'same': (1856, 1856, 1856, 1856),
        'pointer-overlap': (1856, 1860, 1888, 1904),
        'length-overlap': (1856, 1872, 1888, 1890),
        'first-length-second-pointer': (1856, 1872, 1873, 1904),
        'second-length-first-pointer': (1856, 1872, 1888, 1857),
        'index-and-capacity': (record + 24, buffer_field, size_field, 1904),
        'capacity-and-buffer': (size_field, buffer_field, 1888, 1904),
        'unaligned': (1857, 1875, 1891, 1905),
    }[layout]
    first, second, first_len, second_len = [f'buf:arena+{p}' for p in outputs]
    return {'schema': 1, 'address': '0x0111b4e0', 'id': name, 'returns': 'i32',
            'args': {'rdi': f'buf:arena+{owner + 0x38}', 'rsi': f'buf:arena+{record}',
                     'rdx': hex(0xaabbccdd00000000 | offset),
                     'rcx': hex(0x1122334400000000 | length), 'r8': first, 'r9': second},
            'stack': [first_len, second_len],
            'buffers': {'arena': {'size': len(arena), 'bytes': arena.hex()}}}


def cases():
    """Cover offsets, clamping, wrapping sums, output aliasing and signed port indices."""
    rows = []
    for capacity in (0, 1, 17, 3072, 0x80000001, 0xffffffff):
        near = sorted({0, 1, max(0, capacity - 1), capacity, min(capacity + 1, 0xffffffff),
                       0x7fffffff, 0xffffffff})
        for offset in near:
            for length in sorted({0, 1, max(0, capacity - offset), capacity, 0xffffffff}):
                rows.append(case(f'range-{capacity:x}-{offset:x}-{length:x}', capacity, offset, length))
    for slot in (-2, -1, 0, 1, 5, 11):
        for layout in ('separate', 'same', 'pointer-overlap', 'length-overlap',
                       'first-length-second-pointer', 'second-length-first-pointer',
                       'index-and-capacity', 'capacity-and-buffer', 'unaligned'):
            for label, offset, length in [('single', 2, 6), ('wrap', 9, 15), ('error', 17, 8)]:
                rows.append(case(f'{label}-{slot}-{layout}', 17, offset, length, slot, layout))
    for capture in (0, 0xffffffff, 0x8000000000000000, 0xfffffffffffffffd):
        rows.append(case(f'capture-{capture:x}', 32, 7, 3, capture=capture))
    return rows


def main():
    """Write one fixture per case."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
