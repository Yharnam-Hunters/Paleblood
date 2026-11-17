#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the tpfbnd binder resource constructor (game/loading/tpfbnd_resource.cpp).

usage: tpfbnd_resource_cases.py OUT_DIR

Each case supplies a synthetic 0xb0-byte resource buffer (0xa8 bytes plus 8 trailing guard bytes),
a UTF-16 type name buffer (such as "tpfbnd"), and a 64-bit heap word. The base binder resource
constructor (0x017ea230) is a scripted stub whose three arguments and incoming 0xa8-byte resource
buffer are checked, and which writes a synthetic base vtable and trailing fields in some cases so
that call order, vtable-overwriting behavior, and the 64-bit vtable pointer returned in RAX are
verified.
"""
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x0181fb10'
BASE_CONSTRUCT = '0x017ea230'
RESOURCE_SIZE = 0xa8
BUFFER_SIZE = 0xb0
IGNORED_REGISTERS = ('rcx', 'r8', 'r9')

ABI = {
    'calling_convention': 'sysv_amd64',
    'confidence': 'supported',
    'stack_alignment': 16,
    'returns': {
        'type': 'uint64_t',
        'note': 'Preserves the relocated 64-bit vtable pointer (const uint8_t *) left in RAX at return; '
                'cross-reference searches found no observed callers in eboot.elf to prove RAX is ignored.',
    },
    'arguments': [
        {'register': 'rdi', 'type': 'TpfbndResource * (0xa8-byte writable object)', 'direction': 'inout'},
        {'register': 'rsi', 'type': 'const char16_t * (forwarded type name, may be null)', 'direction': 'input'},
        {'register': 'rdx', 'type': 'uint64_t (forwarded heap word)', 'direction': 'input'},
    ] + [
        {'register': register, 'type': 'uint64_t ignored GP seed; not a source formal', 'direction': 'input'}
        for register in IGNORED_REGISTERS
    ],
    'source_formal_slots': ['rdi', 'rsi', 'rdx'],
    'input_regions': ['resource', 'type_name'],
    'writable_regions': ['resource'],
    'evidence': [
        'Disassembly at 0x0181fb10 saves rdi in rbx, forwards rdi/rsi/rdx unchanged to 0x017ea230, '
        'stores 0x0576ac10 at [rbx] via rax, and returns with 0x0576ac10 in rax.',
        'Base constructor 0x017ea230 reads rdi (this), rsi (type name passed to 0x0137f210), and '
        'rdx (heap word stored in HeapReference::word_0x10).',
        'Cross-reference searches of eboot.elf found no direct callers, relocations, or address loads '
        'targeting 0x0181fb10 (only .eh_frame/.eh_frame_hdr); computed or external calls cannot be ruled out, '
        'and 0x01822120 inlines the equivalent sequence with u"tpfbnd" and 0 on a 0xa8-byte allocation.',
    ],
    'execution_limits': {'timeout_seconds': 5},
    'observation_limits': (
        'Synthetic fixtures stub 0x017ea230 and compare final memory, preserved registers, RAX return value, '
        'and stub call arguments. No original-game recording is claimed.'
    ),
}


def u64_hex(value):
    return struct.pack('<Q', value & 0xffffffffffffffff).hex()


def make_case(cid, type_name_arg='buf:type_name', heap_word=0, fill=0xa5, base_writes=False,
              base_ret=None, ignored=None):
    type_bytes = 'tpfbnd\0'.encode('utf-16-le')
    stub = {
        'address': BASE_CONSTRUCT,
        'argc': 3,
        'argi': [0, 1, 2],
        'argmem': [{'arg': 0, 'size': RESOURCE_SIZE}],
    }
    if base_ret is not None:
        stub['ret'] = hex(base_ret)
    if base_writes:
        stub['writes'] = [
            {'arg': 0, 'offset': 0x00, 'bytes': u64_hex(0x0000000005768100)},
            {'arg': 0, 'offset': 0x80, 'bytes': u64_hex(heap_word)},
            {'arg': 0, 'offset': 0x88, 'bytes': '00' * 0x18},
            {'arg': 0, 'offset': 0xa0, 'bytes': u64_hex(0x0123456789abcdef)},
        ]
    args = {
        'rdi': 'buf:resource',
        'rsi': type_name_arg,
        'rdx': hex(heap_word),
        'rcx': '0x0',
        'r8': '0x0',
        'r9': '0x0',
    }
    if ignored:
        args.update(ignored)
    return {
        'schema': 1,
        'address': ADDRESS,
        'id': cid,
        'returns': 'i64',
        'args': args,
        'buffers': {
            'resource': {'size': BUFFER_SIZE, 'bytes': f'{fill:02x}' * BUFFER_SIZE},
            'type_name': {'size': len(type_bytes), 'bytes': type_bytes.hex()},
        },
        'memory': [],
        'imports': [],
        'stubs': [stub],
    }


def cases():
    return [
        make_case('ordinary'),
        make_case('base_writes_vtable_and_fields', heap_word=0x1122334455667788, base_writes=True,
                  base_ret=0x123456789abcdef0),
        make_case('high_word_bit_only', heap_word=0x0000000100000000, base_writes=True),
        make_case('all_bits_heap_word', heap_word=0xffffffffffffffff, fill=0xff, base_writes=True),
        make_case('zero_filled_null_type', type_name_arg='0x0', heap_word=0, fill=0x00),
        make_case('offset_type_name', type_name_arg='buf:type_name+0x4', heap_word=0x7f00000000000001,
                  base_writes=True),
        make_case('ignored_regs_noise', heap_word=0xabcdef0123456789, base_writes=True,
                  base_ret=0xffffffffffffffff,
                  ignored={'rcx': '0xffffffffffffffff', 'r8': '0x80000000ffffffff', 'r9': 'buf:type_name+0x2'}),
    ]


def main():
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = cases()
    for c in generated:
        (out / f"{c['id']}.json").write_text(json.dumps(c, indent=1) + '\n', encoding='utf-8')
    (out / 'abi.json').write_text(json.dumps(ABI, indent=1) + '\n', encoding='utf-8')
    print(f'{len(generated)} cases written to {out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
