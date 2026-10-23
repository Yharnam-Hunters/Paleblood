#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Standalone deterministic input bytes and bounded views; no result or execution oracle."""
import json
from pathlib import Path
import sys

ADDRESS = '0x011c4940'
MODE = 'lower'
DOMAINS = {}
FORMALS = ['rdi']
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16,
    'returns': {'type': 'uint64_t', 'wire': 'full original RDI copied to RAX on empty and nonempty normal returns'},
    'execution_limits': {'timeout_seconds': 5},
    'scope': 'Complete mechanical leaf wire only; original API/prototype and runtime reachability unknown',
    'evidence': ['Canonical full body/raw FDE/CIE/EH and saved readOnly/noanalysis query8073',
                 'Six-new284B classification only; existing011c4970/011c4990 are excluded from new sources',
                 'Known references/caller windows do not establish absent computed entries or original prototypes'],
    'arguments': [{'register': reg, 'type': ('full RDI mutable reached byte sequence' if reg in FORMALS else 'uint64_t ignored GP seed, not a source formal'),
                   'direction': 'inout' if reg == 'rdi' and MODE in ('upper', 'lower', 'append', 'counted_append') else 'input'}
                  for reg in ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9')],
    'source_formal_slots': FORMALS,
    'input_regions': ['data', 'destination', 'source', 'left', 'right', 'arena', 'ignored'],
    'writable_regions': ['data'] if MODE in ('upper', 'lower') else ['destination', 'arena'] if MODE in ('append', 'counted_append') else [],
    'memory_contract': 'Read first byte, stop at NUL; only ASCII 0x41..0x5a cause a BYTE store of captured byte+0x20; then read next byte. No other byte or NUL is written',
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


def case(cid, payload, offset=17, size=None, incoming=None):
    """One readable finite scan; mutable prefix permission excludes the terminator."""
    if not isinstance(payload, bytes) or b'\0' not in payload or offset < 0:
        raise ValueError('finite terminated payload and nonnegative offset required')
    storage = buffers()
    if size is not None:
        if size < 1:
            raise ValueError('positive buffer extent required')
        storage['data'] = bytearray([0xa5] * size)
    if offset + len(payload) > len(storage['data']):
        raise ValueError('payload exceeds supplied buffer')
    storage['data'][offset:offset + len(payload)] = payload
    prefix = payload.index(0)
    args = seeds()
    args['rdi'] = f'buf:data+0x{offset:x}'
    reads = [span('data', offset, prefix + 1)]
    writes = [] if MODE == 'length' else [span('data', offset, prefix)]
    return finish(cid, storage, args, reads, writes, incoming=incoming)


CASES = [case('empty', b'\0tail'), case('single', b'Q\0tail'),
         case('ordinary', b'AbCdeF012_-\0tail'),
         case('ascii_edges', b'@AZ[az{`byBY\0tail'),
         case('high_bytes', b'\x80\xff\x81\xfe\x7fAZaz\0tail'),
         case('all_nonzero_bytes', bytes(range(1, 256)) + b'\0'),
         case('reverse_nonzero_bytes', bytes(range(255, 0, -1)) + b'\0'),
         case('exact_empty', b'\0', offset=0, size=1),
         case('exact_span', b'aZ\x80zA\xff\0', offset=0, size=7),
         case('ignored_zero', b'AzbY\0tail', incoming={k: '0x0' for k in seeds() if k not in FORMALS}),
         case('ignored_mapped', b'AzbY\0tail', incoming={k: 'buf:ignored+0x7' for k in seeds() if k not in FORMALS})]
for length in (2, 3, 7, 15, 16, 17, 31, 32, 63, 64, 127, 256, 273):
    CASES.append(case(f'length_{length:03d}', bytes((i % 255) + 1 for i in range(length)) + b'\0'))
for offset in (0, 1, 3, 7, 15, 31):
    CASES.append(case(f'unaligned_{offset:02x}', b'aBZy\x80z\xff\0tail', offset=offset))

if __name__ == '__main__':
    raise SystemExit(main())
