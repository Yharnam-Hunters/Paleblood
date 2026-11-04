#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic file-operation inputs with scripted kernel callbacks, bounded outputs, descriptor errors and aliases."""
import json
from pathlib import Path
import sys
ADDRESS = '0x0111a6e0'
KERNEL_READ = '0x02fbf408'
MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1
DATA_BYTES = 64
OUTPUT_BYTES = 24
PAYLOAD = bytes.fromhex('1032547698badcfe1122334455667788')

def case(cid, descriptor=1311768467139281697, count=16, result=16, write_bytes=0, output='buf:out+0x8', buffer_pointer=None):
    """Only formal data arg1 can receive scripted writes; wrapper output stays mapped."""
    requested = count & MASK32
    operation = {'address': KERNEL_READ, 'argc': 3, 'ret': hex(result & MASK64)}
    if buffer_pointer is None and requested:
        operation['argmem'] = [{'arg': 1, 'size': min(requested, len(PAYLOAD))}]
    if write_bytes:
        assert buffer_pointer is None and 0 < write_bytes <= min(requested, len(PAYLOAD))
        operation['writes'] = [{'arg': 1, 'bytes': PAYLOAD[:write_bytes].hex()}]
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32', 'args': {'rdi': hex(descriptor & MASK64), 'rsi': buffer_pointer if buffer_pointer is not None else 'buf:data+0x8', 'rdx': hex(count & MASK64), 'rcx': output}, 'buffers': {'data': {'size': DATA_BYTES, 'bytes': 'c7' * DATA_BYTES}, 'out': {'size': OUTPUT_BYTES, 'bytes': 'e5' * OUTPUT_BYTES}}, 'imports': [], 'stubs': [operation]}
CASES = [case('ordinary_exact'), case('zero_descriptor_still_calls', descriptor=0), case('high_word_descriptor', descriptor=18364758542507835392), case('all_descriptor_bits', descriptor=MASK64), case('zero_count_exact', count=0, result=0), case('one_count_exact', count=1, result=1), case('high_count_bit_zero_extends', count=2147483648, result=0), case('all_low_count_bits_zero_extend', count=MASK32, result=0), case('count_upper_word_ignored', count=12379813811299811344), case('all_count_bits_upper_ignored', count=MASK64, result=MASK32), case('zero_result_is_incomplete', result=0), case('partial_result', result=7), case('oversized_positive_result', result=17), case('positive_result_low_sign_bit', result=2147483648), case('positive_high_word_low_count_equal', result=4294967312), case('positive_int64_max', result=(1 << 63) - 1), case('negative_int64_min', result=1 << 63), case('negative_high_word_low_zero', result=18446744069414584320), case('negative_minus_one', result=MASK64), case('negative_mixed_low_count_equal', result=9223372036854775824), case('kernel_buffer_write_exact', write_bytes=16), case('kernel_buffer_write_partial', result=7, write_bytes=7), case('kernel_buffer_write_then_error', result=MASK64, write_bytes=16), case('output_aliases_buffer', output='buf:data+0x10', write_bytes=16), case('output_aliases_buffer_start', output='buf:data+0x8', write_bytes=16), case('unaligned_count_output', output='buf:out+0x9', result=7), case('opaque_buffer_zero_count_forwarded', count=0, result=0, buffer_pointer='0xfedcba9876543210'), case('null_buffer_zero_count_forwarded', count=0, result=0, buffer_pointer='0x0')]

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
