#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic inputs for in-place ASCII uppercase conversion, including non-ASCII bytes and aliases."""
import json
from pathlib import Path
import sys
ADDRESS = '0x011c4910'
FORMALS = ['rdi']

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
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64', 'args': args, 'buffers': {k: {'size': len(v), 'bytes': v.hex()} for k, v in storage.items()}, 'memory': [], 'imports': [], 'stubs': []}

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

def case(cid, payload, offset=17, size=None, incoming=None):
    """One readable finite scan; mutable prefix permission excludes the terminator."""
    if not isinstance(payload, bytes) or b'\x00' not in payload or offset < 0:
        raise ValueError('finite terminated payload and nonnegative offset required')
    storage = buffers()
    if size is not None:
        if size < 1:
            raise ValueError('positive buffer extent required')
        storage['data'] = bytearray([165] * size)
    if offset + len(payload) > len(storage['data']):
        raise ValueError('payload exceeds supplied buffer')
    storage['data'][offset:offset + len(payload)] = payload
    prefix = payload.index(0)
    args = seeds()
    args['rdi'] = f'buf:data+0x{offset:x}'
    reads = [span('data', offset, prefix + 1)]
    writes = [span('data', offset, prefix)]
    return finish(cid, storage, args, reads, writes, incoming=incoming)
CASES = [case('empty', b'\x00tail'), case('single', b'Q\x00tail'), case('ordinary', b'AbCdeF012_-\x00tail'), case('ascii_edges', b'@AZ[az{`byBY\x00tail'), case('high_bytes', b'\x80\xff\x81\xfe\x7fAZaz\x00tail'), case('all_nonzero_bytes', bytes(range(1, 256)) + b'\x00'), case('reverse_nonzero_bytes', bytes(range(255, 0, -1)) + b'\x00'), case('exact_empty', b'\x00', offset=0, size=1), case('exact_span', b'aZ\x80zA\xff\x00', offset=0, size=7), case('ignored_zero', b'AzbY\x00tail', incoming={k: '0x0' for k in seeds() if k not in FORMALS}), case('ignored_mapped', b'AzbY\x00tail', incoming={k: 'buf:ignored+0x7' for k in seeds() if k not in FORMALS})]
for length in (2, 3, 7, 15, 16, 17, 31, 32, 63, 64, 127, 256, 273):
    CASES.append(case(f'length_{length:03d}', bytes((i % 255 + 1 for i in range(length))) + b'\x00'))
for offset in (0, 1, 3, 7, 15, 31):
    CASES.append(case(f'unaligned_{offset:02x}', b'aBZy\x80z\xff\x00tail', offset=offset))
if __name__ == '__main__':
    raise SystemExit(main())
