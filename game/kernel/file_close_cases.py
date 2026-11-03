#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic file-operation inputs with scripted kernel callbacks, bounded outputs, descriptor errors and aliases."""
import json
from pathlib import Path
import sys
ADDRESS = '0x0111a6b0'
KERNEL_CLOSE = '0x02fbf438'
MASK64 = (1 << 64) - 1

def case(cid, descriptor=1311768467139281697, result=0):
    """The numeric descriptor is observed, never treated as readable/writable storage."""
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32', 'args': {'rdi': hex(descriptor & MASK64)}, 'buffers': {}, 'imports': [], 'stubs': [{'address': KERNEL_CLOSE, 'argc': 1, 'ret': hex(result & MASK64)}]}
CASES = [case('ordinary_success'), case('zero_descriptor_still_calls', descriptor=0), case('one_descriptor', descriptor=1), case('high_word_descriptor', descriptor=18364758542507835392), case('high_sign_descriptor', descriptor=1 << 63), case('all_descriptor_bits', descriptor=MASK64), case('positive_status_one', result=1), case('positive_status_two', result=2), case('low_status_sign_bit', result=2147483648), case('all_low_status_bits', result=4294967295), case('upper_word_ignored', result=1 << 32), case('upper_sign_ignored', result=1 << 63), case('all_upper_bits_ignored', result=18446744069414584320), case('upper_bits_low_error', result=1311768464867721217), case('all_return_bits_error', result=MASK64), case('zero_descriptor_error', descriptor=0, result=1)]

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
