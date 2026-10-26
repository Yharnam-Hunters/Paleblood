#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure scalar-wire inputs; no expected output, source-flow or return oracle.

Names/constants describe machine bit operations. Original API meaning/prototype,
initializer reachability and unknown computed callers remain unproved. The i64
fixture carrier preserves full64 RAX under the frontend's uint64_t ABI mapping.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x0111b060'
OUTPUTS = ['rsi']
ABI = {'calling_convention': 'sysv_amd64',
 'confidence': 'supported',
 'stack_alignment': 16,
 'evidence': ['Complete canonical scalar body at 0x0111b060; all exact-byte refs/FDE bound',
              'XOR EAX,EAX on every normal-return path defines full64 RAX zero',
              'Parent accepted bounded scalar wire; API/logical original prototype remains '
              'unknown'],
 'arguments': [{'register': 'rdi',
                'type': 'uint64_t ignored opaque GP-position slot; meaning unknown',
                'direction': 'input'},
               {'register': 'rsi',
                'type': 'void* optional writable exactly4-byte output; unaligned allowed',
                'direction': 'output'},
               {'register': 'rdx',
                'type': 'uint64_t ignored incoming GP seed; not a source formal',
                'direction': 'input'},
               {'register': 'rcx',
                'type': 'uint64_t ignored incoming GP seed; not a source formal',
                'direction': 'input'},
               {'register': 'r8',
                'type': 'uint64_t ignored incoming GP seed; not a source formal',
                'direction': 'input'},
               {'register': 'r9',
                'type': 'uint64_t ignored incoming GP seed; not a source formal',
                'direction': 'input'}],
 'returns': {'type': 'uint64_t'},
 'input_regions': ['arena', 'ignored'],
 'writable_regions': ['arena'],
 'execution_limits': {'timeout_seconds': 5},
 'return_carrier': 'Existing frontend uint64_t maps to fixture i64; harness preserves all64 RAX '
                   'bits',
 'source_formal_slots': ['rdi', 'rsi'],
 'other_argument_entries': 'Explicit ignored GP seeds only',
 'store_contract': 'Optional exactly4-byte stores; no pointee reads. Nonnull views require mapped '
                   'writable4 bytes; unaligned/partial aliases allowed',
 'store_order': ['rsi'],
 'scope': 'Mechanical scalar GP/output wire only; no original API/prototype, installer execution, '
          'unknown computed-caller or raw FP equivalence claim',
 'failure_scope': 'Normally returning paths only; no invented recovery/zero return after invalid '
                  'nonnull output fault',
 'snapshot_limit': 'Same-value stores and restored temporary writes are not observed by snapshots'}


def case(cid, offsets, arena_size=64, pattern=None, incoming=None, initial_words=()):
    """Supply buffer views and incoming slots without deriving source outcomes."""
    if len(offsets) != len(OUTPUTS):
        raise ValueError('one supplied view per output slot required')
    arena = bytearray(((0x35 + 17 * index) & 0xff) if pattern is None else pattern
                      for index in range(arena_size))
    for offset, value in initial_words:
        if not 0 <= offset <= offset + 4 <= arena_size:
            raise ValueError('initial DWORD outside supplied buffer')
        arena[offset:offset + 4] = struct.pack('<I', value)
    args = {register: '0x0' for register in ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9')}
    for register, offset in zip(OUTPUTS, offsets):
        if offset is not None and not 0 <= offset <= offset + 4 <= arena_size:
            raise ValueError('output view needs exactly four mapped writable bytes')
        args[register] = '0x0' if offset is None else f'buf:arena+0x{offset:x}'
    if set(incoming or {}) & set(OUTPUTS):
        raise ValueError('ignored incoming seeds cannot replace output views')
    args.update(incoming or {})
    fixture = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64', 'args': args,
               'buffers': {'arena': {'size': arena_size, 'bytes': arena.hex()},
                           'ignored': {'size': 64, 'bytes': bytes((0x71 + 3 * i) & 0xff for i in range(64)).hex()}},
               'memory': [], 'imports': [], 'stubs': []}
    return fixture


CASES = [
    case('null_default', [None]),
    case('aligned_offset0', [0]),
    case('aligned_offset8', [8]),
    case('unaligned_offset1', [1]),
    case('unaligned_offset2', [2]),
    case('unaligned_offset3', [3]),
    case('unaligned_offset5', [5]),
    case('unaligned_offset7', [7]),
    case('last_four_bytes', [60]),
    case('exact_four_byte_buffer', [0], arena_size=4),
    case('unaligned_last_four', [61], arena_size=65),
    case('zero_pattern', [8], pattern=0),
    case('all_bits_pattern', [8], pattern=0xff),
    case('existing_word_one', [8], initial_words=[(8, 1)]),
    case('ignored_slot_mapped', [8], incoming={'rdi': 'buf:ignored+0x8'}),
    case('unaligned_ignored_slot_mapped', [9], incoming={'rdi': 'buf:ignored+0x9'}),
    case('ignored_gp_all_bits', [8], incoming={'rdi': '0xffffffffffffffff', 'rdx': '0x1234567887654321', 'rcx': '0x8000000000000000', 'r8': '0xfeedface', 'r9': '0x7fffffffffffffff'}),
    case('null_ignored_gp_opaque', [None], incoming={'rdi': '0xfedcba9876543210', 'rdx': '0xffffffffffffffff', 'rcx': '0x1', 'r8': '0x8000000000000000', 'r9': '0x12345678ffffffff'}),
]


def main():
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    destination = Path(sys.argv[1]); destination.mkdir(parents=True, exist_ok=False)
    for fixture in CASES:
        (destination / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    (destination / 'abi.json').write_text(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(CASES)} authored scalar wire inputs written to {destination}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
