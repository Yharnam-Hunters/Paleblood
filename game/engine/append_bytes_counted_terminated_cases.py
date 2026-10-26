#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Standalone deterministic input bytes and bounded views; no result or execution oracle."""
import json
from pathlib import Path
import sys

ADDRESS = '0x011c48b0'
MODE = 'counted_append'
DOMAINS = {}
FORMALS = ['rdi', 'rsi', 'rdx']
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16,
    'returns': {'type': 'uint64_t', 'wire': 'full original RDI copied to RAX on every normal return'},
    'execution_limits': {'timeout_seconds': 5},
    'scope': 'Complete mechanical leaf wire only; original API/prototype and runtime reachability unknown',
    'evidence': ['Canonical full body/raw FDE/CIE/EH and saved readOnly/noanalysis query8073',
                 'Six-new284B classification only; existing011c4970/011c4990 are excluded from new sources',
                 'Known references/caller windows do not establish absent computed entries or original prototypes'],
    'arguments': [{'register': reg, 'type': ('full RDI destination; full RSI source; low32 RDX count; upper32 ignored' if reg in FORMALS else 'uint64_t ignored GP seed, not a source formal'),
                   'direction': 'inout' if reg == 'rdi' and MODE in ('upper', 'lower', 'append', 'counted_append') else 'input'}
                  for reg in ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9')],
    'source_formal_slots': FORMALS,
    'input_regions': ['data', 'destination', 'source', 'left', 'right', 'arena', 'ignored'],
    'writable_regions': ['data'] if MODE in ('upper', 'lower') else ['destination', 'arena'] if MODE in ('append', 'counted_append') else [],
    'memory_contract': 'If low32 count is zero return with no scan/read/write. Otherwise scan destination through NUL, capture low32 count, then capture source BYTE before each forward BYTE store. A copied NUL returns immediately; exhaustion after count nonzero bytes performs one mandatory extra NUL store at old terminator+count',
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


def case(cid, prefix, payload, count_input=None, destination_offset=17, source_offset=8,
         destination_size=None, source_size=None, alias=None, arena_size=640, incoming=None, opaque=None):
    """Declare finite scans and copy spans from input intent, with no output calculation."""
    if not isinstance(prefix, bytes) or not isinstance(payload, bytes) or b'\0' in prefix:
        raise ValueError('prefix must exclude zero and payload must be bytes')
    if min(destination_offset, source_offset, arena_size) < 0 or arena_size == 0:
        raise ValueError('negative offset or empty arena')
    counted = MODE == 'counted_append'
    if counted:
        if not isinstance(count_input, int) or not 0 <= count_input <= 0xffffffffffffffff:
            raise ValueError('full64 count input required')
        count = count_input & 0xffffffff
    else:
        if count_input is not None:
            raise ValueError('unbounded append has no count formal')
        count = None
    zero = payload.find(b'\0')
    if not counted and zero < 0:
        raise ValueError('unbounded append requires finite source terminator')
    scan = len(prefix) + 1
    terminal = destination_offset + len(prefix)
    if counted and count == 0:
        read_count = write_count = scan = 0
    elif counted and alias == 'after':
        # Future source zeros can be overwritten. The authored count, not a scan model, bounds reads.
        read_count, write_count = count, count + 1
    elif zero >= 0 and (not counted or zero < count):
        read_count = write_count = zero + 1
    elif counted and len(payload) >= count:
        read_count, write_count = count, count + 1
    else:
        raise ValueError('payload shorter than bounded nonterminated reads')
    storage = buffers()
    if destination_size is not None:
        if destination_size < 1:
            raise ValueError('positive destination extent required')
        storage['destination'] = bytearray([0xb7] * destination_size)
    if source_size is not None:
        if source_size < 1:
            raise ValueError('positive source extent required')
        storage['source'] = bytearray([0xd3] * source_size)
    storage['arena'] = bytearray([0xcd] * arena_size)
    if alias is None:
        dr, sr = 'destination', 'source'
    elif alias in ('before', 'after', 'self_zero'):
        dr = sr = 'arena'
        if alias == 'before' and terminal >= source_offset:
            raise ValueError('before append requires terminal below source')
        if alias == 'after' and terminal <= source_offset:
            raise ValueError('after append requires terminal above source')
        if alias == 'self_zero' and (terminal != source_offset or zero != 0):
            raise ValueError('self_zero requires source at old destination zero')
        if not counted and alias == 'after' and zero != 0:
            raise ValueError('unbounded forward after alias may not terminate')
    else:
        raise ValueError('unsupported alias')
    if destination_offset + len(prefix) + 1 > len(storage[dr]) or source_offset + len(payload) > len(storage[sr]):
        raise ValueError('initial prefix or payload outside buffer')
    storage[dr][destination_offset:terminal + 1] = prefix + b'\0'
    if alias is not None:
        overlap_start = max(destination_offset, source_offset)
        overlap_end = min(terminal + 1, source_offset + len(payload))
        if overlap_start < overlap_end and storage[dr][overlap_start:overlap_end] != payload[overlap_start - source_offset:overlap_end - source_offset]:
            raise ValueError('alias payload conflicts with initial destination scan')
    storage[sr][source_offset:source_offset + len(payload)] = payload
    args = seeds()
    args['rdi'], args['rsi'] = f'buf:{dr}+0x{destination_offset:x}', f'buf:{sr}+0x{source_offset:x}'
    if counted:
        args['rdx'] = hex(count_input)
    if opaque is not None:
        if not counted or count != 0 or alias is not None or len(opaque) != 2:
            raise ValueError('opaque pointer pair requires counted low32zero')
        args['rdi'], args['rsi'] = opaque
    reads = [span(dr, destination_offset, scan), span(sr, source_offset, read_count)]
    writes = [span(dr, terminal, write_count)]
    return finish(cid, storage, args, reads, writes, alias or 'separate', opaque is not None, incoming)


CASES = []
if MODE == 'append':
    CASES.extend([case('empty_both', b'', b'\0tail'), case('empty_source', b'DST', b'\0tail'),
                  case('empty_destination', b'', b'AbC\0tail'), case('ordinary', b'DST:', b'AbC\0tail'),
                  case('high_bytes', b'\x80Q', b'\xff\x81Az\0tail'),
                  case('all_nonzero_bytes', b'X:', bytes(range(1, 256)) + b'\0'),
                  case('exact_span', b'D', b'ABC\0', destination_offset=0, source_offset=0, destination_size=5, source_size=4),
                  case('self_zero', b'ABC', b'\0tail', alias='self_zero', destination_offset=13, source_offset=16),
                  case('alias_before1', b'AB', b'QRST\0tail', alias='before', destination_offset=12, source_offset=15),
                  case('alias_before3', b'A', b'\x80Q\xffRST\0', alias='before', destination_offset=8, source_offset=12),
                  case('empty_after_alias', b'D', b'\0', alias='after', destination_offset=12, source_offset=10),
                  case('ignored_zero', b'D:', b'Az\0tail', incoming={k: '0x0' for k in seeds() if k not in FORMALS}),
                  case('ignored_mapped', b'D:', b'Az\0tail', incoming={k: 'buf:ignored+0x3' for k in seeds() if k not in FORMALS})])
    for length in (1, 2, 7, 15, 16, 31, 32, 63, 64, 127):
        CASES.append(case(f'bound_{length:03d}', b'P:', bytes((i % 255) + 1 for i in range(length)) + b'\0'))
else:
    CASES.extend([case('zero_null', b'', b'', 0, opaque=('0x0', '0x0')),
                  case('zero_opaque', b'', b'', 0, opaque=('0xfedcba9876543210', '0x8000000000000001')),
                  case('upper32_zero_opaque', b'', b'', 0xdeadbeef00000000, opaque=('0x8765432100000001', '0x8000000000000000')),
                  case('zero_null_source', b'DST', b'', 0, opaque=('buf:destination+0x11', '0x0')),
                  case('zero_null_destination', b'', b'AB\0', 0, opaque=('0x0', 'buf:source+0x8')),
                  case('zero_mapped', b'DST', b'AB\0', 0),
                  case('one_nonzero', b'D:', b'ABCD\0', 1), case('one_zero', b'D:', b'\0tail', 1),
                  case('count_before_zero', b'D:', b'AB\0tail', 2),
                  case('count_includes_zero', b'D:', b'AB\0tail', 3),
                  case('count_after_zero', b'D:', b'AB\0tail', 6),
                  case('upper32_one', b'D:', b'ABCD\0', 0xffffffff00000001),
                  case('upper32_two', b'D:', b'ABCD\0', 0x1234567800000002),
                  case('bit31_early_zero', b'D:', b'Q\0', 0x80000000),
                  case('max32_early_zero', b'D:', b'Q\0', 0xffffffff),
                  case('max64_early_zero', b'D:', b'\x80Q\xff\0', 0xffffffffffffffff),
                  case('high_bytes', b'\x80Q', b'\xff\x81Az\0', 5),
                  case('all_nonzero_bytes', b'X:', bytes(range(1, 256)), 255),
                  case('exact_exhaustion', b'D', b'ABC', 3, destination_offset=0, source_offset=0, destination_size=5, source_size=3),
                  case('exact_early_zero', b'D', b'AB\0', 5, destination_offset=0, source_offset=0, destination_size=4, source_size=3),
                  case('self_zero', b'ABC', b'\0tail', 5, alias='self_zero', destination_offset=13, source_offset=16),
                  case('alias_before1', b'AB', b'QRST\0tail', 5, alias='before', destination_offset=12, source_offset=15),
                  case('alias_before3', b'A', b'\x80Q\xffRST\0', 7, alias='before', destination_offset=8, source_offset=12),
                  case('alias_after_chain1', b'K', b'K\0QRSTUV', 6, alias='after', destination_offset=8, source_offset=8),
                  case('alias_after_chain2', b'\x80Q', b'\x80Q\0RSTUV', 6, alias='after', destination_offset=8, source_offset=8),
                  case('alias_after_chain3', b'\x80Q\xff', b'\x80Q\xff\0STUV', 7, alias='after', destination_offset=8, source_offset=8),
                  case('ignored_zero', b'D:', b'Az\0tail', 4, incoming={k: '0x0' for k in seeds() if k not in FORMALS}),
                  case('ignored_mapped', b'D:', b'Az\0tail', 4, incoming={k: 'buf:ignored+0x3' for k in seeds() if k not in FORMALS})])
    for length in (1, 2, 3, 7, 15, 16, 31, 32, 63, 64, 127):
        CASES.append(case(f'bound_{length:03d}', b'P:', bytes((i % 255) + 1 for i in range(length)), length))
for offset in (0, 1, 3, 7, 15, 31):
    count = 5 if MODE == 'counted_append' else None
    CASES.append(case(f'unaligned_{offset:02x}', b'D:', b'\x80QAz\0tail', count, destination_offset=offset, source_offset=offset + 2))

if __name__ == '__main__':
    raise SystemExit(main())
