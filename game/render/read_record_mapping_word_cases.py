#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"Exercise unaligned mapping-field extraction and adjacent/upper-word bits. Synthetic inputs compare both implementations without expected answers."
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x01486cf0'
RETURN = "i64"
FIRST_VALUES = (0, 1, 5, 7, 15, 31)
SECOND_VALUES = (0, 1, 6, 7)

def fixture(case_id, word):
    arena = bytearray((0x35 + 29 * i) & 255 for i in range(512))
    struct.pack_into('<I', arena, 49, word & 0xffffffff)
    return {
        'schema': 1, 'address': ADDRESS, 'id': case_id, 'returns': RETURN,
        'args': {'rdi': 'buf:arena+0x21'}, 'stack': [],
        'buffers': {'arena': {'size': len(arena), 'bytes': arena.hex()}},
        'memory': [], 'imports': [], 'stubs': [],
    }


def fixtures():
    base_words = (0xad008001 | (first << 2) | (second << 8) | (mode << 11)
                  for first in FIRST_VALUES for second in SECOND_VALUES for mode in range(4))
    for index, word in enumerate(base_words):
        yield fixture(f'edge_{index:04d}', word)
    # Bit 7 borders the first field, bit 11 borders the second field, and
    # bits 13 onward must not widen the mode or leak the upper word.
    for first in (1, 5, 6, 8):
        for mode in range(4):
            for poison in (0x80, 0x2000, 0x4000, 0x8000, 0x10000,
                           0x7fff0000, 0x80000000, 0xffff0000):
                word = (first << 2) | (6 << 8) | (mode << 11) | poison
                case_id = f'field_first_{first:02x}_mode_{mode}_poison_{poison:08x}'
                yield fixture(case_id, word)


ABI_JSON = '{"calling_convention":"sysv_amd64","confidence":"supported","stack_alignment":16,"arguments":[{"register":"rdi","type":"const RecordMappingSource *","direction":"input"}],"returns":{"type":"uint64_t"},"input_regions":["arena"],"writable_regions":["arena"],"execution_limits":{"timeout_seconds":10},"evidence":["Synthetic mapping arguments and unaligned borrowed input; no expected output values."],"scope":"Packed mapping behavior; genuine game recordings remain separate."}'


def main():
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=False)
    cases = list(fixtures())
    for case in cases:
        (destination / (case['id'] + '.json')).write_text(json.dumps(case, indent=2) + chr(10))
    abi = json.loads(ABI_JSON)
    (destination / 'abi.json').write_text(json.dumps(abi, indent=2) + chr(10))
    print(f'{len(cases)} synthetic cases written')


if __name__ == '__main__':
    main()
