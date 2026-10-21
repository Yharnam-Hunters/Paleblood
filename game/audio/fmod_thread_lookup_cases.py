#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic cases for FMOD's thread-affinity lookup at 0x0111a8c0.

The real mapped comparison helpers run on synthetic names and affinity masks. No system
imports are called. Cases cover each slot, exact/prefix boundaries, untouched outputs,
64-bit copies and output aliasing. These are not recordings from a game run.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x0111a8c0'
TABLE = 0x0586d430
NAMES = [
    b'FMOD mixer thread',
    b'FMOD AudioOut thread',
    b'FMOD stream thread',
    b'FMOD nonblocking thread',
    b'FMOD file thread',
    b'FMOD geometry thread',
    b'FMOD EventNetNode Thread',
    b'FMOD record update thread',
]
SLOTS = ['mixer', 'audio_out', 'stream', 'nonblocking', 'file', 'geometry', 'event_net_node', 'record_update']
MASKS = [0x1122334455667788, 0x8877665544332211, 0x8000000000000001, 0xffffffffffffffff,
          0x0102030405060708, 0xfedcba9876543210, 0xabc0000012345678, 0x1234567800000000]


def case(cid, name, masks=None, output='buf:out+0x8'):
    """Set a synthetic name and eight synthetic 64-bit masks with canaries around the output."""
    raw = name + b'\0'
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': 'buf:name', 'rsi': output},
        'buffers': {'name': {'size': len(raw), 'bytes': raw.hex()},
                    'out': {'size': 24, 'bytes': 'a5' * 24}},
        'memory': [{'addr': hex(TABLE), 'bytes': struct.pack('<8Q', *(MASKS if masks is None else masks)).hex()}],
        'imports': [],
    }


CASES = [case('matches_' + slot, name) for slot, name in zip(SLOTS, NAMES)]
CASES += [
    case('unknown_name', b'unknown thread'),
    case('empty_name', b''),
    case('case_sensitive_name', b'fmod mixer thread'),
    case('exact_name_suffix_is_unknown', NAMES[0] + b' extra'),
    case('nonblocking_suffix_after_byte_23', NAMES[3] + b' extra'),
    case('nonblocking_shorter_than_23', NAMES[3][:-1]),
    case('nonblocking_differs_at_byte_22', NAMES[3][:22] + b'X'),
    case('nonblocking_embedded_nul', b'FMOD nonblocking\0thread'),
    case('high_byte_name', b'\xff thread'),
    case('unknown_null_output', b'unknown thread', output=0),
    case('zero_slot_mask', NAMES[0], masks=[0] + MASKS[1:]),
    case('output_aliases_name', NAMES[0], output='buf:name'),
    case('output_aliases_another_table_slot', NAMES[0], output='guest:' + hex(TABLE + 8)),
]


def main():
    """Write the generated fixture set to a caller-selected private output directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        (output / (fixture['id'] + '.json')).write_text(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
