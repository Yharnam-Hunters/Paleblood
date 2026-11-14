#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for frame_timing_part_0x10_update_013d5440 (game/frame_timing/part_0x10_update.cpp),
for tools/verify.py run.

usage: part_0x10_update_cases.py OUT_DIR

- seconds 0, -0, negative, -inf, NaN (quiet, signalling and negative: none updates), the
  smallest denormal, 1/30, 1 and +inf;
- the source present or missing; the generator's flag (stubbed) 0, 1 and 0x100 (only its low
  byte counts, so 0x100 is idle);
- the output switch off, 1, 0x80 and 0xff; output counts 0, 3, -1 and 0x7fffffff (2n + 1 wraps);
- a fill that moves the part's target and output (the generator is the part itself here), so
  both are read again after it; a step that replaces the thread's allocator holder (the
  allocated block is the thread block itself), so the free reads the holder again.
The part's flags start at 0xaa so a missing or extra write shows. The thread pointer (gs:[0])
points into a fake thread block whose slot holds an allocator holder; the headed allocate and
free (0x00829a40 / 0x00829a90) are stubbed.
"""
import json
import os
import struct
import sys

ADDRESS = '0x013d5440'
THREAD_SLOT = '0x057e40f8'
BLOCK = 0x80          # thread pointer offset inside the tls buffer
SLOT = -0x30          # thread-local offset of the holder pointer
SOURCE_STEP, GENERATOR_FLAG, GENERATOR_REFRESH = '0x01576450', '0x00b6a020', '0x00b6a030'
BUFFER_SIZE, FILL, STEP = '0x00b68770', '0x00b69450', '0x00b69190'
ALLOCATE, FREE = '0x00829a40', '0x00829a90'
USE_TARGET, COUNT = '0x00500200', '0x00500300'   # fake addresses in the output's vtable slots 5 and 9


def f32(v):
    return struct.pack('<f', v).hex()


def case(cid, seconds=1 / 30, source=True, flag=1, output=0, count=3, size=0x40,
         fill_moves=False, step_replaces_holder=False):
    raw = seconds if isinstance(seconds, str) else f32(seconds)
    c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
         'args': {'rdi': 'buf:part', 'xmm0': raw},
         'buffers': {'part': {'size': 0x148, 'bytes': 'aa' * 0x148}, 'generator': {'size': 0x90},
                     'source': {'size': 0x20}, 'target': {'size': 0x20}, 'target2': {'size': 0x20},
                     'output': {'size': 0x10}, 'output2': {'size': 0x10}, 'output_vt': {'size': 0x50},
                     'block': {'size': 0x100},
                     'tls': {'size': BLOCK + 0x10}, 'holder': {'size': 0x60}, 'holder2': {'size': 0x60},
                     'allocator': {'size': 0x10}, 'allocator2': {'size': 0x10}},
         'gs': f'buf:tls+{BLOCK}',
         'memory': [{'addr': f'buf:tls+{BLOCK}', 'pointer': f'tls+{BLOCK}'},
                    {'addr': THREAD_SLOT, 'bytes': struct.pack('<q', SLOT).hex()},
                    {'addr': f'buf:tls+{BLOCK + SLOT}', 'pointer': 'holder'},
                    {'addr': 'buf:holder+0x58', 'pointer': 'allocator'},
                    {'addr': 'buf:holder2+0x58', 'pointer': 'allocator2'},
                    {'addr': 'buf:part+0x10', 'pointer': 'part' if fill_moves else 'generator'},
                    {'addr': 'buf:part+0x18', **({'pointer': 'source'} if source else {'bytes': '00' * 8})},
                    {'addr': 'buf:part+0x20', 'pointer': 'target'},
                    {'addr': 'buf:part+0x28', 'pointer': 'output'},
                    {'addr': 'buf:part+0x30', 'bytes': f'{output:02x}'},
                    {'addr': 'buf:output', 'pointer': 'output_vt'},
                    {'addr': 'buf:output2', 'pointer': 'output_vt'},
                    {'addr': 'buf:output_vt+0x28', 'guest': USE_TARGET},
                    {'addr': 'buf:output_vt+0x48', 'guest': COUNT}],
         'imports': [], 'stubs': []}
    value = struct.unpack('<f', bytes.fromhex(raw))[0]
    if not value > 0:
        return c
    stubs = c['stubs']
    if source:
        stubs.append({'address': SOURCE_STEP, 'argc': 1, 'argf32': [0]})
    stubs.append({'address': GENERATOR_FLAG, 'argc': 1, 'ret': hex(flag)})
    if flag & 0xff == 0:
        stubs.append({'address': GENERATOR_REFRESH, 'argc': 1})
    if output:
        stubs.append({'address': COUNT, 'argc': 1, 'ret': hex(count & 0xffffffff)})
        stubs.append({'address': BUFFER_SIZE, 'argc': 2, 'argu32': [1], 'ret': hex(size)})
        stubs.append({'address': ALLOCATE, 'argc': 2, 'argu32': [1], 'ret': 'buf:block'})
        fill = {'address': FILL, 'argc': 4, 'argu32': [2], 'argf32': [0]}
        if fill_moves:
            fill['writes'] = [{'arg': 0, 'offset': 0x20, 'pointer': 'target2'},
                              {'arg': 0, 'offset': 0x28, 'pointer': 'output2'}]
        stubs.append(fill)
        stubs.append({'address': USE_TARGET, 'argc': 3, 'argu32': [2]})
    else:
        stubs.append({'address': BUFFER_SIZE, 'argc': 2, 'argu32': [1], 'ret': hex(size)})
        stubs.append({'address': ALLOCATE, 'argc': 2, 'argu32': [1],
                      'ret': 'buf:tls' if step_replaces_holder else 'buf:block'})
        step = {'address': STEP, 'argc': 2, 'argf32': [0]}
        if step_replaces_holder:
            step['writes'] = [{'arg': 1, 'offset': BLOCK + SLOT, 'pointer': 'holder2'}]
        stubs.append(step)
        stubs.append({'address': FREE, 'argc': 2})
    return c


CASES = [
    case('seconds_zero', 0.0),
    case('seconds_negative_zero', '00000080'),
    case('seconds_negative', -1 / 30),
    case('seconds_negative_inf', float('-inf')),
    case('seconds_nan', '0000c07f'),
    case('seconds_signalling_nan', '0100807f'),
    case('seconds_negative_nan', '0000c0ff'),
    case('seconds_negative_zero_output_on', '00000080', output=1),
    case('seconds_denormal', '01000000'),
    case('seconds_one', 1.0),
    case('seconds_inf', float('inf')),
    case('step_source_busy', flag=1),
    case('step_source_idle', flag=0),
    case('step_flag_high_byte_idle', flag=0x100),
    case('step_flag_0xff', flag=0xff),
    case('step_no_source', source=False),
    case('step_no_source_idle', source=False, flag=0),
    case('step_size_zero', size=0),
    case('step_replaces_holder', step_replaces_holder=True),
    case('output_on', output=1),
    case('output_0xff', output=0xff),
    case('output_0x80', output=0x80),
    case('output_idle', output=1, flag=0),
    case('output_no_source', output=1, source=False),
    case('output_count_zero', output=1, count=0),
    case('output_count_negative', output=1, count=-1),
    case('output_count_max_wraps', output=1, count=0x7fffffff),
    case('output_count_large', output=1, count=0x40000000, size=0x12345678),
    case('output_fill_moves_target', output=1, fill_moves=True),
    case('output_denormal', '01000000', output=1),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
