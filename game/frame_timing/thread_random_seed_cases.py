#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for frame_timing_random_generator_seed_024f8110
(game/frame_timing/thread_random_seed.cpp), for tools/verify.py run.

usage: thread_random_seed_cases.py OUT_DIR

The four words are written into a buffer filled with a pattern and twice as long, so a fifth
word or a short write shows. Seeds: zero, one, two, the signed edges (0x7fffffff, 0x80000000),
all ones, seeds whose first state carries out of 32 and 48 bits, a time-of-day seed, a word that
is not 16-byte aligned, and 48 more from a fixed pseudo-random sequence (high bit set or clear).
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import random
import sys

ADDRESS = '0x024f8110'
GUARD_GOT = '0x057e3be0'
WORDS_BYTES = 0x10


def case(cid, seed, offset=0):
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': f'buf:generator+{offset:#x}', 'rsi': seed & 0xffffffff},
            'buffers': {'generator': {'size': 2 * WORDS_BYTES + offset, 'bytes': 'a5' * (2 * WORDS_BYTES + offset)},
                        'guard': {'size': 0x10, 'bytes': '5a' * 0x10}},
            'memory': [{'addr': GUARD_GOT, 'pointer': 'guard'}],
            'imports': [], 'stubs': []}


CASES = [
    case('seed_zero', 0),
    case('seed_one', 1),
    case('seed_two', 2),
    case('seed_signed_max', 0x7fffffff),
    case('seed_signed_min', 0x80000000),
    case('seed_all_ones', 0xffffffff),
    case('seed_multiplier_low_word', 0x5deece66d & 0xffffffff),
    case('seed_carry_past_48_bits', 0xffff0000),
    case('seed_time_of_day', 0x5f5e_1234),
    case('seed_unaligned_words', 0x1234_5678, offset=4),
    case('seed_odd_offset_words', 0x8765_4321, offset=0xc),
]
_rng = random.Random(0x24f8110)
CASES += [case(f'seed_sequence_{i:02d}', _rng.getrandbits(32) | (0x80000000 if i % 2 else 0)) for i in range(48)]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in CASES:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(CASES)} synthetic cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
