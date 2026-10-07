#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for render_swap_chain_present (0x025b2fb0), for tools/verify.py run.

usage: swap_chain_present_cases.py OUT_DIR

The clock is a rising series (gettimeofday, microseconds) long enough for any wait; spare
sceKernelUsleep entries. Covers the first call, pacing off, interval 0, the 60 Hz interval-1
shortcut and invalid intervals there, waits long enough to sleep (over 17 ms), short ones and
ones already past their target, the flip-mode tables for 60 Hz and other refresh rates, and the
GPU-side reset with its flag set and clear and its skip in 60 Hz modes.
"""
import json
import os
import struct
import sys

T0 = 1_000_000_000


def case(cid, interval, last=T0, frames=100, pacing=1, refresh=30, gpu=None, start=None, step=500):
    """gpu: None (global flag clear) or the value the GPU flag getter returns."""
    start = T0 + 1000 if start is None else start
    clock = [start + i * step for i in range(400)]
    b = {'self': {'size': 40}, 'swapchain': {'size': 8}, 'gpu1': {'size': 24}, 'gpu2': {'size': 16}, 'gpu': {'size': 46256}}
    m = [{'addr': 'buf:self', 'pointer': 'swapchain'}, {'addr': 'buf:self+8', 'bytes': f'{pacing:02x}'},
         {'addr': 'buf:self+12', 'bytes': struct.pack('<I', refresh).hex()},
         {'addr': 'buf:self+24', 'bytes': struct.pack('<Q', last).hex()},
         {'addr': 'buf:self+32', 'bytes': struct.pack('<I', frames).hex()},
         {'addr': '0x0553ac86', 'bytes': '00' if gpu is None else '01'},
         {'addr': '0x059406c8', 'pointer': 'gpu1'}, {'addr': 'buf:gpu1+16', 'pointer': 'gpu2'},
         {'addr': 'buf:gpu2+8', 'pointer': 'gpu'}]
    stubs = [{'address': '0x02ad5dd0', 'argc': 2}, {'address': '0x02ad5dd0', 'argc': 2},
             {'address': '0x024b55b0', 'argc': 3}, {'address': '0x024b55b0', 'argc': 3}, {'address': '0x024b55b0', 'argc': 3},
             {'address': '0x02ab9a50', 'argc': 1, 'ret': 1 if gpu else 0}, {'address': '0x02ab9a40', 'argc': 1},
             {'address': '0x02aafa00', 'argc': 1}]
    imports = [{'name': 'gettimeofday', 'argc': 2, 'ret': 0, 'series': {'arg': 0, 'format': 'timeval_us', 'values': clock}}]
    imports += [{'name': 'sceKernelUsleep', 'argc': 1, 'ret': 0} for _ in range(3)]
    return {'schema': 1, 'address': '0x025b2fb0', 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:self', 'rsi': hex(interval)}, 'buffers': b, 'memory': m, 'stubs': stubs, 'imports': imports}


CASES = [
    case('first_call', 2, last=0),
    case('pacing_off', 2, pacing=0),
    case('interval_zero', 0),
    case('refresh60_interval1', 1, refresh=60),
    case('refresh60_interval5', 5, refresh=60),
    case('refresh60_interval2_wait', 2, refresh=60),
    case('refresh30_interval2_wait', 2),
    case('refresh30_interval1_wait', 1),
    case('long_wait_sleeps', 3, step=2000),
    case('wait_just_over_17ms', 2, start=T0 + 33333 - 17100, step=50),
    case('wait_under_17ms', 2, start=T0 + 33333 - 16000, step=100),
    case('target_already_passed', 1, start=T0 + 50000),
    case('refresh60_interval3', 3, refresh=60),
    case('refresh60_interval4', 4, refresh=60),
    case('interval_over_4_paced', 6, refresh=30),
    case('gpu_reset_flag_set', 2, gpu=1),
    case('gpu_reset_flag_clear', 2, gpu=0),
    case('gpu_skip_60hz_interval1', 1, refresh=60, gpu=1),
    case('gpu_skip_60hz_interval6', 6, refresh=60, gpu=1),
    case('gpu_60hz_interval2', 2, refresh=60, gpu=1),
    case('frame_counter_wraps', 3, frames=0xfffffffe),
    # 17,001 us before the target is the first gap that sleeps (for 1 us): the threshold itself.
    case('sleep_threshold', 2, start=T0 + 33333 - 17001, step=100),
    case('below_sleep_threshold', 2, start=T0 + 33333 - 17000, step=100),
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
