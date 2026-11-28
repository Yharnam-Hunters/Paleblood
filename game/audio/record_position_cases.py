#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate synthetic FMOD recording-position comparisons."""
import json
from pathlib import Path
import struct
import sys


def case(name, fmt, position=95, divisor=3, index=5, output='separate'):
    context, record = 64, 1248
    arena = bytearray(b'\xa5' * 1408)
    for slot in range(-6, 13):
        struct.pack_into('<i', arena, context + 0x418 + 4 * slot, 130 + slot)
    entry = context + 0x418 + 4 * index
    struct.pack_into('<i', arena, entry, position)
    struct.pack_into('<i', arena, record + 24, index)
    struct.pack_into('<I', arena, record + 60, fmt)
    struct.pack_into('<I', arena, record + 84, divisor)
    offsets = {'separate': 1360, 'unaligned': 1361, 'port': entry, 'index': record + 24,
               'format': record + 60, 'divisor': record + 84, 'partial-divisor': record + 85}
    destination = '0x0' if output == 'null' else f'buf:arena+{offsets[output]}'
    return {'schema': 1, 'address': '0x0111b3f0', 'id': name, 'returns': 'i32',
            'args': {'rdi': f'buf:arena+{context}', 'rsi': f'buf:arena+{record}',
                     'rdx': destination},
            'buffers': {'arena': {'size': len(arena), 'bytes': arena.hex()}}}


def cases():
    """Cover each format, intermediate wrapping, signed indices and aliased output."""
    rows = []
    for fmt in (*range(17), 0x101, 0xffffffff):
        rows.append(case(f'format-{fmt}', fmt))
        for position in (-0x80000000, -512, -1, 0, 1, 3, 4, 35, 36, 37, 0x20000001, 0x7fffffff):
            rows.append(case(f'position-{fmt}-{position}', fmt, position=position))
        for divisor in (0, 1, 2, 95, 96, 0x100, 0x10001, 0x80000000, 0xffffffff):
            rows.append(case(f'divisor-{fmt}-{divisor}', fmt, divisor=divisor))
        rows.append(case(f'zero-divisor-null-{fmt}', fmt, divisor=0, output='null'))
        if fmt == 14 or fmt > 15:
            rows.append(case(f'ignored-format-null-{fmt}', fmt, output='null'))
    for fmt, factor in [(6, 14), (7, 64), (8, 28), (9, 28)]:
        last = 0xffffffff // factor
        for position in (last - 1, last, last + 1, last + 2):
            rows.append(case(f'wrap-{fmt}-{position}', fmt, position=position, divisor=1))
    for fmt in (0, 1, 3, 6, 7, 8, 10, 14, 15):
        for index in (-3, 0, 9):
            for output in ('unaligned', 'port', 'index', 'format', 'divisor', 'partial-divisor'):
                rows.append(case(f'alias-{fmt}-{index}-{output}', fmt, index=index, output=output))
    return rows


def main():
    """Write each synthetic input under its case ID."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len(rows) == len({row['id'] for row in rows})
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic cases')


if __name__ == '__main__':
    main()
