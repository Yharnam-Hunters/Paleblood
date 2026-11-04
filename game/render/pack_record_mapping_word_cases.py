#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"Exercise packed mapping families, modes and wrapped membership indices. Synthetic inputs compare both implementations without expected answers."
import json
from pathlib import Path
import struct
import sys

ADDRESS = '0x01471310'
RETURN = "i64"
FIRST_AND_SECOND = ((0, 22),
 (1, 6),
 (2, 22),
 (3, 6),
 (4, 22),
 (5, 6),
 (6, 22),
 (7, 6),
 (8, 22),
 (9, 6),
 (10, 22),
 (11, 6),
 (12, 22),
 (13, 6),
 (14, 22),
 (15, 6),
 (16, 22),
 (17, 6),
 (18, 22),
 (19, 6),
 (20, 22),
 (21, 6),
 (22, 22),
 (23, 6),
 (24, 22),
 (25, 6),
 (26, 22),
 (27, 6),
 (28, 22),
 (29, 6),
 (30, 22),
 (31, 6),
 (32, 22),
 (33, 6),
 (34, 22),
 (35, 6),
 (36, 22),
 (37, 6),
 (38, 22),
 (39, 6),
 (40, 22),
 (41, 6),
 (42, 22),
 (43, 6),
 (44, 22),
 (45, 6),
 (46, 22),
 (47, 6),
 (48, 22),
 (49, 6),
 (50, 22),
 (51, 6),
 (52, 22),
 (53, 6),
 (54, 22),
 (55, 6),
 (56, 22),
 (57, 6),
 (58, 22),
 (59, 6),
 (60, 22),
 (61, 6),
 (62, 22),
 (63, 6),
 (64, 22),
 (65, 6),
 (127, 6),
 (128, 22),
 (255, 6),
 (256, 22),
 (4294967295, 6))
MODES = (0, 1, 2, 3, 4, 0x100, 0xffffffff)

def fixtures():
    arguments = ((first, second, mode) for first, second in FIRST_AND_SECOND for mode in MODES)
    for index, (first, second, mode) in enumerate(arguments):
        yield {
            'schema': 1, 'address': ADDRESS, 'id': f'edge_{index:04d}', 'returns': RETURN,
            'args': {'rdi': hex(first), 'rsi': hex(second), 'rdx': hex(mode)},
            'stack': [], 'buffers': {}, 'memory': [], 'imports': [], 'stubs': [],
        }


ABI_JSON = '{"calling_convention":"sysv_amd64","confidence":"supported","stack_alignment":16,"arguments":[{"register":"rdi","type":"uint32_t first","direction":"input"},{"register":"rsi","type":"uint32_t second","direction":"input"},{"register":"rdx","type":"uint32_t mode","direction":"input"}],"returns":{"type":"uint64_t"},"input_regions":[],"writable_regions":[],"execution_limits":{"timeout_seconds":10},"evidence":["Synthetic mapping arguments and unaligned borrowed input; no expected output values."],"scope":"Packed mapping behavior; genuine game recordings remain separate."}'


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
