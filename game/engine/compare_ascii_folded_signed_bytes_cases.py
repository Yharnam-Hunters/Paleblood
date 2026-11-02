#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Standalone deterministic input bytes and bounded views; no result or execution oracle."""
import json
from pathlib import Path
import sys

ADDRESS = '0x011c49c0'
MODE = 'compare'
DOMAINS = {}
FORMALS = ['rdi', 'rsi']
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16,
    'returns': {'type': 'uint64_t', 'wire': 'Map captured ASCII uppercase bytes to lowercase; sign-extend each resulting BYTE to32 then subtract in EAX; full raw RAX zeroextends32 even for a negative signed32 difference'},
    'execution_limits': {'timeout_seconds': 5},
    'scope': 'Complete mechanical leaf wire only; original API/prototype and runtime reachability unknown',
    'evidence': ['Canonical full body/raw FDE/CIE/EH and saved readOnly/noanalysis query8073',
                 'Six-new284B classification only; existing011c4970/011c4990 are excluded from new sources',
                 'Known references/caller windows do not establish absent computed entries or original prototypes'],
    'arguments': [{'register': reg, 'type': ('full RDI left readable bytes; full RSI right readable bytes' if reg in FORMALS else 'uint64_t ignored GP seed, not a source formal'),
                   'direction': 'inout' if reg == 'rdi' and MODE in ('upper', 'lower', 'append', 'counted_append') else 'input'}
                  for reg in ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9')],
    'source_formal_slots': FORMALS,
    'input_regions': ['data', 'destination', 'source', 'left', 'right', 'arena', 'ignored'],
    'writable_regions': ['data'] if MODE in ('upper', 'lower') else ['destination', 'arena'] if MODE in ('append', 'counted_append') else [],
    'memory_contract': 'Read left BYTE and map if0x41..0x5a, then read right BYTE and map similarly, before left-NUL test. If mappedleft NUL or byte mismatch return difference; otherwise advance both full pointers and repeat. No writes',
    'bounds_contract': 'DOMAINS contains authored valid maximum byte spans, never expected values/return/branch outcomes. Inactive bytes remain distinguishable.',
    'observation_limits': 'Native arbitrary reads, same-value/restored writes, raw caller-clobbered GP/FP and invalid faults uninstrumented; no gameplay, at most future edge-verified',
    'error_limits': '875 prior errors/281 explicit points/594 unattributed LSDA owners;73 linear caller windows/233 skips; unknown computed references remain'}


def buffers():
    """Distinguishable inactive buffers and sentinels surrounding each supplied byte view."""
    return {name: bytearray([value] * 640) for name, value in
            [('data', 0xa5), ('destination', 0xb7), ('source', 0xd3), ('left', 0x91), ('right', 0xe5), ('arena', 0xcd), ('ignored', 0x37)]}


def span(region, offset, size):
    """A declared input-side byte extent, without expected contents after execution."""
    return {'buffer': region, 'offset': offset, 'size': size}


def finish(cid, storage, args, reads, writes, alias='separate', opaque=False, incoming=None):
    """Validate all declared views and ignored input slots, then serialize input only."""
    if cid in DOMAINS:
        raise ValueError('duplicate case id')
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
        elif not 0 <= int(value, 0) <= 0xffffffffffffffff:
            raise ValueError('raw GP value exceeds64 bits')
    DOMAINS[cid] = {'read_spans': reads, 'write_spans': writes, 'alias': alias,
                    'formal_pointers_opaque': opaque,
                    'authored_input_extent_not_expected_result_or_native_access_observation': True}
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64', 'args': args,
            'buffers': {k: {'size': len(v), 'bytes': v.hex()} for k, v in storage.items()},
            'memory': [], 'imports': [], 'stubs': []}


def seeds():
    return {'rdi': '0xfedcba9876543210', 'rsi': '0x8000000000000001',
            'rdx': '0x76543210fedcba98', 'rcx': '0xffffffffffffffff',
            'r8': '0x80000000ffffffff', 'r9': '0x123456789abcdef0'}


def main():
    if len(sys.argv) != 2:
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=False)
    for row in CASES:
        (out / (row['id'] + '.json')).write_text(json.dumps(row, indent=1) + '\n')
    (out / 'abi.json').write_text(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(CASES)} authored input states written to {out}')
    return 0


def case(cid, left, right, left_offset=17, right_offset=9, left_size=None, right_size=None, alias=False, incoming=None):
    """Bound both independent captured-byte reads without predicting a comparison result."""
    if not isinstance(left, bytes) or not isinstance(right, bytes) or b'\0' not in left or b'\0' not in right or min(left_offset, right_offset) < 0:
        raise ValueError('two finite terminated byte sequences required')
    storage = buffers()
    lr, rr = ('arena', 'arena') if alias else ('left', 'right')
    if left_size is not None:
        if alias or left_size < 1:
            raise ValueError('positive separate left extent required')
        storage[lr] = bytearray([0x91] * left_size)
    if right_size is not None:
        if alias or right_size < 1:
            raise ValueError('positive separate right extent required')
        storage[rr] = bytearray([0xe5] * right_size)
    if left_offset + len(left) > len(storage[lr]) or right_offset + len(right) > len(storage[rr]):
        raise ValueError('initial payload exceeds buffer')
    storage[lr][left_offset:left_offset + len(left)] = left
    if alias:
        lo, hi = max(left_offset, right_offset), min(left_offset + len(left), right_offset + len(right))
        if lo < hi and storage[lr][lo:hi] != right[lo - right_offset:hi - right_offset]:
            raise ValueError('conflicting read-only alias payloads')
    storage[rr][right_offset:right_offset + len(right)] = right
    # A first zero in either supplied input bounds the pair of reads, independent of any folding/difference calculation.
    maximum = min(left.index(0), right.index(0)) + 1
    args = seeds()
    args['rdi'], args['rsi'] = f'buf:{lr}+0x{left_offset:x}', f'buf:{rr}+0x{right_offset:x}'
    return finish(cid, storage, args, [span(lr, left_offset, maximum), span(rr, right_offset, maximum)], [], 'read_only' if alias else 'separate', incoming=incoming)


CASES = [case('empty_both', b'\0tail', b'\0tail'), case('empty_left', b'\0tail', b'A\0tail'),
         case('empty_right', b'A\0tail', b'\0tail'), case('ordinary_equal', b'AbCdEf\0tail', b'aBcDeF\0tail'),
         case('prefix_difference', b'AbCdX\0', b'aBcDy\0'), case('left_shorter', b'Ab\0tail', b'aBc\0tail'),
         case('right_shorter', b'AbC\0tail', b'aB\0tail'),
         case('high_negative', b'\x80\0', b'\x7f\0'), case('high_positive', b'\x7f\0', b'\x80\0'),
         case('high_equal', b'\x80\xffAz\0', b'\x80\xffaZ\0'),
         case('zero_vs_high', b'\0tail', b'\x80\0tail'), case('high_vs_zero', b'\xff\0tail', b'\0tail'),
         case('all_nonzero_equal', bytes(range(1, 256)) + b'\0', bytes(range(1, 256)) + b'\0'),
         case('all_nonzero_folded', bytes(range(1, 256)) + b'\0', bytes(x + 0x20 if 0x41 <= x <= 0x5a else x for x in range(1, 256)) + b'\0'),
         case('exact_empty', b'\0', b'\0', left_offset=0, right_offset=0, left_size=1, right_size=1),
         case('exact_span', b'aZ\x80\0', b'Az\x80\0', left_offset=0, right_offset=0, left_size=4, right_size=4),
         case('self_alias', b'AZaZ\0', b'AZaZ\0', left_offset=17, right_offset=17, alias=True),
         case('overlap_alias', b'AAAA\0', b'AAA\0', left_offset=17, right_offset=18, alias=True),
         case('reverse_overlap_alias', b'AAA\0', b'AAAA\0', left_offset=18, right_offset=17, alias=True),
         case('ignored_zero', b'AzY\0', b'aZy\0', incoming={k: '0x0' for k in seeds() if k not in FORMALS}),
         case('ignored_mapped', b'AzY\0', b'aZy\0', incoming={k: 'buf:ignored+0x7' for k in seeds() if k not in FORMALS})]
for byte in (0x40, 0x41, 0x5a, 0x5b, 0x60, 0x61, 0x7a, 0x7b, 0x7f, 0x80, 0xff):
    CASES.append(case(f'edge_left_{byte:02x}', bytes([byte, 0]), b'a\0'))
    CASES.append(case(f'edge_right_{byte:02x}', b'a\0', bytes([byte, 0])))
for offset in (0, 1, 3, 7, 15, 31):
    CASES.append(case(f'unaligned_{offset:02x}', b'AZ\x80z\xff\0', b'az\x80Z\xff\0', left_offset=offset, right_offset=offset + 2))

if __name__ == '__main__':
    raise SystemExit(main())
