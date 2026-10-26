#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored stderr-wrapper wire cases; these do not execute or model real stdio.

Mapped stream buffers are opaque scripted storage, not guessed FILE layouts. Only
formal stream writes within that storage are allowed; input text remains read-only.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x0111a740'
FPUTS = '0x02fbf1e8'
STDERR_GOT = '0x057e40c0'
MASK64 = (1 << 64) - 1
BUFFER_BYTES = 64
STREAM_OFFSET = 8
ABI = {
    'calling_convention': 'sysv_amd64', 'confidence': 'supported',
    'evidence': ['Pinned original 20-byte body at 0x0111a740; fputs NID and JUMP_SLOT',
                 'One load from _Stderr GLOB_DAT 0x057e40c0; supplied libc exports a data OBJECT'],
    'stack_alignment': 16,
    'arguments': [
        {'register': 'rdi', 'type': 'const char* input string wire value', 'direction': 'input'},
        # Initial noise registers are observed inputs, not additional logical parameters.
        {'register': 'rsi', 'type': 'uint64_t ignored incoming caller-saved register', 'direction': 'input'},
        {'register': 'rdx', 'type': 'uint64_t ignored incoming caller-saved register', 'direction': 'input'},
        {'register': 'rcx', 'type': 'uint64_t ignored incoming caller-saved register', 'direction': 'input'},
    ],
    'returns': {'type': 'int32_t zero; fputs result discarded'},
    'input_regions': ['message', 'stream', 'decoy'],
    'writable_regions': ['stream'],
    'execution_limits': {'timeout_seconds': 5},
}


def case(cid, text=b'camera diagnostic\0', result=0, text_pointer=None,
         stream_pointer=None, text_offset=8, stream_offset=STREAM_OFFSET,
         write=False, decoy=False, incoming=None):
    """Capture bounded formal arguments without assigning a real FILE structure."""
    assert len(text) + text_offset <= BUFFER_BYTES
    args = {'rdi': text_pointer if text_pointer is not None else f'buf:message+{text_offset:#x}',
            'rsi': '0x0', 'rdx': '0x0', 'rcx': '0x0'}
    if incoming:
        args.update(incoming)
    operation = {'address': FPUTS, 'argc': 2, 'ret': hex(result & MASK64)}
    observed = []
    if text_pointer is None:
        observed.append({'arg': 0, 'size': text.index(0) + 1})
    if stream_pointer is None:
        observed.append({'arg': 1, 'size': 16})
    if observed:
        operation['argmem'] = observed
    if write:
        assert stream_pointer is None
        operation['writes'] = [{'arg': 1, 'offset': 8, 'bytes': '1032547698badcfe'}]
    message = b'\xa5' * text_offset + text + b'\x5a' * (BUFFER_BYTES - text_offset - len(text))
    fixture = {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': args,
        'buffers': {
            'message': {'size': BUFFER_BYTES, 'bytes': message.hex()},
            'stream': {'size': BUFFER_BYTES, 'bytes': 'b6' * BUFFER_BYTES},
            'decoy': {'size': BUFFER_BYTES, 'bytes': 'd7' * BUFFER_BYTES},
        },
        'memory': [{'addr': STDERR_GOT, **({'pointer': f'stream+{stream_offset:#x}'}
                    if stream_pointer is None else {'bytes': struct.pack('<Q', stream_pointer & MASK64).hex()})}],
        'imports': [], 'stubs': [operation],
    }
    if decoy:
        assert stream_pointer is None
        fixture['memory'].append({'addr': f'buf:stream+{stream_offset:#x}', 'pointer': 'decoy'})
    return fixture


CASES = [
    case('ordinary_message'),
    case('empty_string', text=b'\0'),
    case('one_character', text=b'x\0'),
    case('newline_preserved', text=b'line\n\0'),
    case('percent_is_plain_input', text=b'%s %n 100%\0'),
    case('embedded_terminator', text=b'ab\0unused suffix'),
    case('high_byte_string', text=b'\x80\xff\xfe\0'),
    case('unaligned_text', text_offset=9),
    case('unaligned_stream', stream_offset=9),
    case('opaque_stream_with_decoy', decoy=True),
    case('fputs_positive_one_ignored', result=1),
    case('fputs_positive_other_ignored', result=19),
    case('fputs_low_sign_bit_ignored', result=0x80000000),
    case('fputs_minus_one_ignored', result=MASK64),
    case('fputs_upper_word_ignored', result=1 << 32),
    case('fputs_high_sign_bit_ignored', result=1 << 63),
    case('fputs_mixed_result_ignored', result=0x1234567887654321),
    case('stream_write_preserved_success', write=True),
    case('stream_write_preserved_failure', write=True, result=MASK64),
    case('stream_write_preserved_upper_result', write=True, result=1 << 63),
    case('null_text_forwarded', text_pointer='0x0'),
    case('opaque_high_text_forwarded', text_pointer='0x1234567887654321'),
    case('opaque_all_text_bits_forwarded', text_pointer='0xffffffffffffffff'),
    case('null_stream_forwarded', stream_pointer=0),
    case('opaque_high_stream_forwarded', stream_pointer=0xfedcba9800000000),
    case('opaque_all_stream_bits_forwarded', stream_pointer=MASK64),
    case('incoming_rsi_is_replaced_by_slot', incoming={'rsi': '0x1234567800000001',
         'rdx': '0xffffffffffffffff', 'rcx': '0x8000000000000000'}),
    case('empty_string_still_calls_on_failure', text=b'\0', result=MASK64),
]


def main():
    """Write only authored cases and ABI into a fresh caller-selected directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=False)
    for fixture in CASES:
        with (output / (fixture['id'] + '.json')).open('x') as stream:
            stream.write(json.dumps(fixture, indent=1) + '\n')
    with (output / 'abi.json').open('x') as stream:
        stream.write(json.dumps(ABI, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
