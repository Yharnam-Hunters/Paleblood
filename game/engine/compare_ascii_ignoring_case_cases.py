#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic case-insensitive string comparisons, including signed high bytes, NUL boundaries and aliases."""
import json
from pathlib import Path
import sys
ADDRESS = '0x011c49c0'
FORMALS = ['rdi', 'rsi']

def buffers():
    """Distinguishable inactive buffers and sentinels surrounding each supplied byte view."""
    return {name: bytearray([value] * 640) for name, value in [('data', 165), ('destination', 183), ('source', 211), ('left', 145), ('right', 229), ('arena', 205), ('ignored', 55)]}

def span(region, offset, size):
    """A declared input-side byte extent, without expected contents after execution."""
    return {'buffer': region, 'offset': offset, 'size': size}

def finish(cid, storage, args, reads, writes, alias='separate', opaque=False, incoming=None):
    """Validate all declared views and ignored input slots, then serialize input only."""
    if set(incoming or {}) & set(FORMALS) or set(incoming or {}) - set(args):
        raise ValueError('ignored seeds cannot change formals or unknown slots')
    args.update(incoming or {})
    for view in reads + writes:
        if view['buffer'] not in storage or min(view['offset'], view['size']) < 0 or view['offset'] + view['size'] > len(storage[view['buffer']]):
            raise ValueError('declared byte view exceeds buffer')
    for value in args.values():
        if value.startswith('buf:'):
            region, _, at = value[4:].partition('+')
            if region not in storage or not 0 <= int(at or '0', 0) < len(storage[region]):
                raise ValueError('mapped pointer exceeds buffer')
        elif not 0 <= int(value, 0) <= 18446744073709551615:
            raise ValueError('raw GP value exceeds64 bits')
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32', 'args': args, 'buffers': {k: {'size': len(v), 'bytes': v.hex()} for k, v in storage.items()}, 'memory': [], 'imports': [], 'stubs': []}

def seeds():
    return {'rdi': '0xfedcba9876543210', 'rsi': '0x8000000000000001', 'rdx': '0x76543210fedcba98', 'rcx': '0xffffffffffffffff', 'r8': '0x80000000ffffffff', 'r9': '0x123456789abcdef0'}

def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in CASES:
        (out / (row['id'] + '.json')).write_text(json.dumps(row, indent=1) + '\n')
    print(f'{len(CASES)} authored input states written to {out}')
    return 0

def case(cid, left, right, left_offset=17, right_offset=9, left_size=None, right_size=None, alias=False, incoming=None):
    """Bound both independent captured-byte reads without predicting a comparison result."""
    if not isinstance(left, bytes) or not isinstance(right, bytes) or b'\x00' not in left or (b'\x00' not in right) or (min(left_offset, right_offset) < 0):
        raise ValueError('two finite terminated byte sequences required')
    storage = buffers()
    lr, rr = ('arena', 'arena') if alias else ('left', 'right')
    if left_size is not None:
        if alias or left_size < 1:
            raise ValueError('positive separate left extent required')
        storage[lr] = bytearray([145] * left_size)
    if right_size is not None:
        if alias or right_size < 1:
            raise ValueError('positive separate right extent required')
        storage[rr] = bytearray([229] * right_size)
    if left_offset + len(left) > len(storage[lr]) or right_offset + len(right) > len(storage[rr]):
        raise ValueError('initial payload exceeds buffer')
    storage[lr][left_offset:left_offset + len(left)] = left
    if alias:
        lo, hi = (max(left_offset, right_offset), min(left_offset + len(left), right_offset + len(right)))
        if lo < hi and storage[lr][lo:hi] != right[lo - right_offset:hi - right_offset]:
            raise ValueError('conflicting read-only alias payloads')
    storage[rr][right_offset:right_offset + len(right)] = right
    maximum = min(left.index(0), right.index(0)) + 1
    args = seeds()
    args['rdi'], args['rsi'] = (f'buf:{lr}+0x{left_offset:x}', f'buf:{rr}+0x{right_offset:x}')
    return finish(cid, storage, args, [span(lr, left_offset, maximum), span(rr, right_offset, maximum)], [], 'read_only' if alias else 'separate', incoming=incoming)
CASES = [case('empty_both', b'\x00tail', b'\x00tail'), case('empty_left', b'\x00tail', b'A\x00tail'), case('empty_right', b'A\x00tail', b'\x00tail'), case('ordinary_equal', b'AbCdEf\x00tail', b'aBcDeF\x00tail'), case('prefix_difference', b'AbCdX\x00', b'aBcDy\x00'), case('left_shorter', b'Ab\x00tail', b'aBc\x00tail'), case('right_shorter', b'AbC\x00tail', b'aB\x00tail'), case('high_negative', b'\x80\x00', b'\x7f\x00'), case('high_positive', b'\x7f\x00', b'\x80\x00'), case('high_equal', b'\x80\xffAz\x00', b'\x80\xffaZ\x00'), case('zero_vs_high', b'\x00tail', b'\x80\x00tail'), case('high_vs_zero', b'\xff\x00tail', b'\x00tail'), case('all_nonzero_equal', bytes(range(1, 256)) + b'\x00', bytes(range(1, 256)) + b'\x00'), case('all_nonzero_folded', bytes(range(1, 256)) + b'\x00', bytes((x + 32 if 65 <= x <= 90 else x for x in range(1, 256))) + b'\x00'), case('exact_empty', b'\x00', b'\x00', left_offset=0, right_offset=0, left_size=1, right_size=1), case('exact_span', b'aZ\x80\x00', b'Az\x80\x00', left_offset=0, right_offset=0, left_size=4, right_size=4), case('self_alias', b'AZaZ\x00', b'AZaZ\x00', left_offset=17, right_offset=17, alias=True), case('overlap_alias', b'AAAA\x00', b'AAA\x00', left_offset=17, right_offset=18, alias=True), case('reverse_overlap_alias', b'AAA\x00', b'AAAA\x00', left_offset=18, right_offset=17, alias=True), case('ignored_zero', b'AzY\x00', b'aZy\x00', incoming={k: '0x0' for k in seeds() if k not in FORMALS}), case('ignored_mapped', b'AzY\x00', b'aZy\x00', incoming={k: 'buf:ignored+0x7' for k in seeds() if k not in FORMALS})]
for byte in (64, 65, 90, 91, 96, 97, 122, 123, 127, 128, 255):
    CASES.append(case(f'edge_left_{byte:02x}', bytes([byte, 0]), b'a\x00'))
    CASES.append(case(f'edge_right_{byte:02x}', b'a\x00', bytes([byte, 0])))
for offset in (0, 1, 3, 7, 15, 31):
    CASES.append(case(f'unaligned_{offset:02x}', b'AZ\x80z\xff\x00', b'az\x80Z\xff\x00', left_offset=offset, right_offset=offset + 2))
if __name__ == '__main__':
    raise SystemExit(main())
