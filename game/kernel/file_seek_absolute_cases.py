#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic file-operation inputs with scripted kernel callbacks, bounded outputs, descriptor errors and aliases."""
import json
from pathlib import Path
import sys
ADDRESS = '0x0111a720'
KERNEL_SEEK = '0x02fbf428'
MASK64 = (1 << 64) - 1

def case(cid, descriptor=1311768467139281697, offset=305419896, result=0, incoming=None):
    """Observe the original's exact argument widths and signed 64-bit result test."""
    args = {'rdi': hex(descriptor & MASK64), 'rsi': hex(offset & MASK64), 'rdx': '0x0', 'rcx': '0x0', 'r8': '0x0', 'r9': '0x0'}
    if incoming:
        args.update(incoming)
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32', 'args': args, 'buffers': {}, 'imports': [], 'stubs': [{'address': KERNEL_SEEK, 'argc': 3, 'ret': hex(result & MASK64)}]}
CASES = [case('ordinary_success'), case('zero_descriptor_still_calls', descriptor=0), case('descriptor_one', descriptor=1), case('descriptor_high_word', descriptor=18364758542507835392), case('descriptor_high_sign_bit', descriptor=1 << 63), case('descriptor_all_bits', descriptor=MASK64), case('descriptor_low_word', descriptor=4294967295), case('descriptor_mixed_words', descriptor=9223372037160195704), case('offset_zero', offset=0), case('offset_one', offset=1), case('offset_signed_max', offset=2147483647), case('offset_high_bit_zero_extends', offset=2147483648), case('offset_all_low_bits_zero_extends', offset=4294967295), case('offset_upper_word_ignored', offset=12379813811299811328), case('offset_mixed_upper_word_ignored', offset=1311768467139281697), case('offset_all_bits', offset=MASK64), case('positive_result_one', result=1), case('positive_result_low_sign_bit', result=2147483648), case('positive_result_all_low_bits', result=4294967295), case('positive_result_upper_word', result=1 << 32), case('positive_result_int64_max', result=(1 << 63) - 1), case('negative_result_int64_min', result=1 << 63), case('negative_result_high_word_low_zero', result=18446744069414584320), case('negative_result_minus_one', result=MASK64), case('negative_result_mixed_low_positive', result=12379813811299811329), case('zero_descriptor_negative_result', descriptor=0, result=MASK64), case('discarded_incoming_argument_registers', incoming={'rdx': '0xffffffffffffffff', 'rcx': '0x1122334455667788', 'r8': '0x8000000000000000', 'r9': '0xdeadbeefcafebabe'}), case('all_width_boundaries_together', descriptor=MASK64, offset=MASK64, result=18446744069414584320, incoming={'rdx': '0x12345678ffffffff'})]

def main():
    """Write deterministic synthetic inputs to the requested directory."""
    if len(sys.argv) != 2:
        print(f'usage: {Path(__file__).name} OUT_DIR', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for fixture in CASES:
        with (output / (fixture['id'] + '.json')).open('w') as stream:
            stream.write(json.dumps(fixture, indent=1) + '\n')
    print(f'{len(CASES)} synthetic cases written to {output}')
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
