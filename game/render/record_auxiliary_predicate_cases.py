#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise auxiliary-selector boundaries and the flag's byte result.

Synthetic inputs compare the original and replacement without expected answers,
unrelated memory mappings, or an incoming-RAX requirement.
"""
import json
from pathlib import Path
import sys

ADDRESS = '0x02abb4f0'
RETURN = 'i8'
FLAGS = (0, 1, 0x3f, 0x40, 0x41, 0x80, 0xc0, 0xffffffff)
SELECTORS = (0, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0xffffffff)


def fixtures():
    for flags in FLAGS:
        for selector in SELECTORS:
            yield {
                'schema': 1, 'address': ADDRESS,
                'id': f'selector_{selector:08x}_flags_{flags:08x}',
                'returns': RETURN, 'args': {'rdi': hex(flags), 'rsi': hex(selector)},
                'stack': [], 'buffers': {}, 'memory': [], 'imports': [], 'stubs': [],
            }


def main():
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=False)
    cases = list(fixtures())
    for case in cases:
        (destination / (case['id'] + '.json')).write_text(json.dumps(case, indent=2) + chr(10))
    abi = {
        'calling_convention': 'sysv_amd64', 'confidence': 'supported', 'stack_alignment': 16,
        'arguments': [
            {'register': 'rdi', 'type': 'uint32_t flags', 'direction': 'input'},
            {'register': 'rsi', 'type': 'uint32_t selector', 'direction': 'input'},
        ],
        'returns': {'type': 'uint8_t'}, 'input_regions': [], 'writable_regions': [],
        'execution_limits': {'timeout_seconds': 10},
        'evidence': ['The original defines the AL predicate result; callers consume that byte.'],
        'scope': 'Synthetic selector and flag boundaries; genuine game recordings remain separate.',
    }
    (destination / 'abi.json').write_text(json.dumps(abi, indent=2) + chr(10))
    print(f'{len(cases)} synthetic cases written')


if __name__ == '__main__':
    main()
