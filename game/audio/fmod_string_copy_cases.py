#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""NUL-terminated string copy used by FMOD callers; copy the terminator and return destination."""
import json
from pathlib import Path
import sys
ADDRESS = '0x011c4810'
ABI = {'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16, 'returns': {'type': 'uint64_t'}, 'source_formal_slots': ['rdi', 'rsi'], 'input_regions': ['destination', 'source', 'arena', 'ignored'], 'writable_regions': ['destination', 'arena'], 'execution_limits': {'timeout_seconds': 5}, 'evidence': ['NUL-terminated string copy used by FMOD callers; copy the terminator and return destination.', 'Synthetic cases cover bounded input buffers, aliases and ignored incoming registers.'], 'arguments': [{'register': 'rdi', 'type': 'pointer or scalar input in rdi', 'direction': 'inout'}, {'register': 'rsi', 'type': 'pointer or scalar input in rsi', 'direction': 'input'}, {'register': 'rdx', 'type': 'ignored incoming uint64_t', 'direction': 'input'}, {'register': 'rcx', 'type': 'ignored incoming uint64_t', 'direction': 'input'}, {'register': 'r8', 'type': 'ignored incoming uint64_t', 'direction': 'input'}, {'register': 'r9', 'type': 'ignored incoming uint64_t', 'direction': 'input'}]}

def case(cid, payload, count_input=None, destination_offset=16, source_offset=8, destination_size=None, source_size=None, alias=None, arena_size=128, incoming=None, opaque=None):
    """Build input bytes only; derive span bounds from input termination/count or explicit bounded alias."""
    if not isinstance(payload, bytes):
        raise ValueError('payload must be bytes')
    if count_input is not None:
        raise ValueError('until-zero has no count formal')
    count = None
    first_zero = payload.find(b'\x00')
    if first_zero < 0:
        raise ValueError('until-zero needs an input terminator')
    access = first_zero + 1 if first_zero >= 0 else len(payload)
    if alias == 'after':
        if first_zero != 0:
            raise ValueError('unbounded forward overlap requires immediately reached zero')
    if opaque is not None:
        raise ValueError('opaque formal pointers require counted zero access')
    if min(destination_offset, source_offset) < 0:
        raise ValueError('negative byte offset')
    destination_size = max(64, destination_offset + access + 8) if destination_size is None else destination_size
    source_size = max(64, source_offset + len(payload) + 8) if source_size is None else source_size
    if min(destination_size, source_size, arena_size) < 1:
        raise ValueError('buffers must have positive extent')
    buffers = {'destination': bytearray([165] * destination_size), 'source': bytearray([211] * source_size), 'arena': bytearray([183] * arena_size), 'ignored': bytearray((49 + 17 * index & 255 for index in range(64)))}
    if alias is None:
        source_region, destination_region = ('source', 'destination')
    elif alias in ('self', 'before', 'after'):
        source_region = destination_region = 'arena'
        if alias == 'self' and destination_offset != source_offset:
            raise ValueError('self alias needs equal offsets')
        if alias == 'before' and destination_offset >= source_offset:
            raise ValueError('before alias needs smaller destination')
        if alias == 'after' and destination_offset <= source_offset:
            raise ValueError('after alias needs larger destination')
    else:
        raise ValueError('unknown alias domain')
    if source_offset + len(payload) > len(buffers[source_region]):
        raise ValueError('initial payload outside source region')
    if source_offset + access > len(buffers[source_region]) or destination_offset + access > len(buffers[destination_region]):
        raise ValueError('declared reached access span outside buffer')
    buffers[source_region][source_offset:source_offset + len(payload)] = payload
    args = {'rdi': f'buf:{destination_region}+0x{destination_offset:x}', 'rsi': f'buf:{source_region}+0x{source_offset:x}', 'rdx': '0x76543210fedcba98', 'rcx': '0xfedcba9876543210', 'r8': '0x8000000000000001', 'r9': '0xffffffffffffffff'}
    if opaque is not None:
        if len(opaque) != 2 or any((not isinstance(x, str) for x in opaque)):
            raise ValueError('opaque pair must contain pointer strings')
        args['rdi'], args['rsi'] = opaque
    formal = {'rdi', 'rsi'}
    if set(incoming or {}) & formal:
        raise ValueError('ignored seeds cannot override formal inputs')
    if set(incoming or {}) - (set(args) - formal):
        raise ValueError('unsupported ignored GP seed')
    args.update(incoming or {})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64', 'args': args, 'buffers': {name: {'size': len(data), 'bytes': data.hex()} for name, data in buffers.items()}, 'memory': [], 'imports': [], 'stubs': []}
CASES = [case('empty', b'\x00'), case('single_ascii', b'A\x00'), case('four_bytes', b'ABCD\x00'), case('ordinary', b'copy bytes\x00'), case('early_zero_payload', b'AB\x00unused\xfftail'), case('high_byte_80', b'\x80\x00'), case('high_byte_ff', b'\xff\x00'), case('high_bytes', b'\x80\xff\xfe\x81\x00'), case('nonzero_byte_range', bytes(range(1, 256)) + b'\x00')]
for length in (2, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256):
    CASES.append(case(f'length_{length:03d}', bytes((index % 255 + 1 for index in range(length))) + b'\x00'))
for offset in (0, 1, 3, 7, 15, 31):
    CASES.append(case(f'unaligned_{offset:02x}', b'\x80Q\xffR\x00', destination_offset=offset, source_offset=offset + 2))
CASES.extend([case('exact_one_byte_buffers', b'\x00', destination_offset=0, source_offset=0, destination_size=1, source_size=1), case('exact_payload_buffers', b'ABCD\x00', destination_offset=0, source_offset=0, destination_size=5, source_size=5), case('last_terminator', b'\x80AB\xff\x00', destination_offset=17, source_offset=9, destination_size=22, source_size=14), case('self_alias', b'ABCD\x00', alias='self', destination_offset=17, source_offset=17), case('self_alias_high_bytes', b'\xff\x80\xfe\x81\x00', alias='self', destination_offset=3, source_offset=3), case('alias_before_1', b'ABCD\x00', alias='before', destination_offset=16, source_offset=17), case('alias_before_3', b'\x80AB\xffC\x00', alias='before', destination_offset=8, source_offset=11), case('alias_before_8', b'longer bytes\x00', alias='before', destination_offset=7, source_offset=15), case('empty_after_alias', b'\x00ABCD', alias='after', destination_offset=10, source_offset=9), case('ignored_gp_zero', b'ABCD\x00', incoming={'rdx': '0x0', 'rcx': '0x0', 'r8': '0x0', 'r9': '0x0'}), case('ignored_gp_mapped', b'ABCD\x00', incoming={'rdx': 'buf:ignored+0x1', 'rcx': 'buf:arena+0x3', 'r8': 'buf:source+0x2', 'r9': 'buf:ignored+0x7'})])

def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (output / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    (output / 'abi.json').write_text(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(CASES)} authored input states written to {output}')
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
