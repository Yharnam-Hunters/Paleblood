#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for frame_timing_pace_frame (0x02434770), for tools/verify.py run.

usage: frame_limiter_cases.py OUT_DIR

Synthetic SprjFlipper objects (fields as documented in frame_limiter.cpp) and clock
sequences: every flip mode and the pending switch, the reset and override fields, waits that
spin and the sleeping wait (0x013e3980 stubbed to 0), catch-up after late frames, forced late,
the 60 FPS notification, a clock that goes backwards, and NaN or huge intervals. A NaN
interval with a normal frame makes the original wait forever, so that case is forced late. Real inputs
come from an in-game run with BB_CAPTURE_DIR set.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02434770'
SIZE = 0x2c8
F30, F60 = 0x3d088889, 0x3c888889
T0 = 5_000_000_000          # microseconds: the previous frame ended here


def flipper(**f):
    b = bytearray(SIZE)

    def u32(off, v): struct.pack_into('<I', b, off, v & 0xffffffff)
    def u64(off, v): struct.pack_into('<Q', b, off, v & 0xffffffffffffffff)
    def f32(off, v): struct.pack_into('<f', b, off, v)
    u32(0x8, f.get('mode', 1))
    u32(0xc, f.get('pending_mode', 0))
    u32(0x10, f.get('sync', 2))
    b[0x14] = f.get('flag14', 1)
    u32(0x18, f.get('interval_bits', F30))
    u64(0x20, T0 - 33_000)
    u64(0x28, T0)
    for i, (dt, late) in enumerate(f.get('ring', [])):
        u64(0x60 + 16 * i, dt)
        b[0x68 + 16 * i] = late
    u32(0x260, f.get('ring_index', 0))
    u32(0x268, f.get('window_a', 1))
    u32(0x26c, f.get('window_b', 30))
    for off in (0x270, 0x271, 0x272, 0x273, 0x274, 0x276, 0x2c4):
        b[off] = f.get(f'b{off:x}', 0)
    # Distinct values, so a wrong shift of the history shows.
    for i, v in enumerate(f.get('history', [0.030 + 0.0005 * i for i in range(16)])):
        f32(0x278 + 4 * i, v)
    u32(0x2bc, f.get('override_a', 0xffffffff))
    u32(0x2c0, f.get('override_b', 0xffffffff))
    return bytes(b)


def clock(start_us, step_us, count):
    """gettimeofday results: start, then `count - 1` more, `step_us` apart."""
    out = []
    for i in range(count):
        t = start_us + i * step_us
        tv = struct.pack('<qq', t // 1_000_000, t % 1_000_000)
        out.append({'name': 'gettimeofday', 'argc': 2, 'ret': 0,
                    'writes': [{'arg': 0, 'offset': 0, 'size': 16, 'bytes': tv.hex()}]})
    return out


def case(cid, fl, clock_calls, spin_only=1, notify=False, usleeps=0):
    imports = list(clock_calls) + [{'name': 'sceKernelUsleep', 'argc': 1, 'ret': 0}] * usleeps
    buffers = {'flipper': {'size': SIZE, 'bytes': fl.hex()}, 'window': {'size': 8}}
    memory = [{'addr': '0x05940500', 'pointer': 'window'}]
    if notify:
        buffers['notify'] = {'size': 96}
        memory.append({'addr': '0x05940e00', 'pointer': 'notify'})
    else:
        memory.append({'addr': '0x05940e00', 'bytes': '00' * 8})
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:flipper'}, 'buffers': buffers, 'memory': memory,
            'stubs': [{'address': '0x013e3980', 'ret': spin_only}], 'imports': imports}


def spin(start_offset_us=5_000, step_us=1_000, budget_us=34_000):
    """A clock that starts `start_offset_us` after the previous frame and steps past the budget."""
    return clock(T0 + start_offset_us, step_us, budget_us // step_us + 4)


CASES = [
    case('mode0', flipper(mode=0), spin()),
    case('mode1_steady_30', flipper(mode=1), spin()),
    case('mode2_60', flipper(mode=2), spin(budget_us=17_000)),
    case('mode3', flipper(mode=3), spin()),
    case('mode4', flipper(mode=4), spin()),
    case('mode_invalid_keeps_fields', flipper(mode=9, interval_bits=F60, sync=7), spin(budget_us=17_000)),
    case('pending_mode_switch', flipper(mode=1, pending_mode=2, b276=1), spin(budget_us=17_000)),
    case('reset_flag', flipper(mode=2, b2c4=1), spin()),
    case('overrides', flipper(override_a=5, override_b=12), spin()),
    case('override_one', flipper(override_a=0xffffffff, override_b=3), spin()),
    case('clear_flag_271', flipper(b271=1, window_a=4, window_b=9), spin()),
    case('clear_flag_272', flipper(b272=1, window_a=4, window_b=9), spin()),
    case('over_budget_no_wait', flipper(mode=1), clock(T0 + 50_000, 1_000, 2)),
    case('exactly_on_budget', flipper(mode=1), clock(T0 + 33_333, 1, 4)),
    case('catch_up_shortens', flipper(b270=1, mode=9, window_b=3, window_a=3, ring_index=2,
                                      ring=[(40_000, 1), (40_000, 1), (40_000, 1)]), spin()),
    case('catch_up_to_third', flipper(b270=1, mode=9, window_a=2, ring_index=1,
                                      ring=[(90_000, 1), (90_000, 1)]), spin()),
    # mode=9 keeps the window fields (a valid mode would overwrite them).
    case('catch_up_not_needed', flipper(b270=1, mode=9, window_a=3, ring_index=2,
                                        ring=[(10_000, 1), (10_000, 0), (10_000, 1)]), spin()),
    case('forced_late_273', flipper(b273=1), clock(T0 + 5_000, 1_000, 2)),
    case('sleeping_wait', flipper(mode=4), clock(T0 + 1_000, 7_000, 8), spin_only=0, usleeps=6),
    case('sleeping_wait_short', flipper(mode=2), clock(T0 + 14_000, 1_000, 6), spin_only=0, usleeps=2),
    case('notify_60', flipper(b274=1), spin(), notify=True),
    case('notify_missing', flipper(b274=1), spin(), notify=False),
    case('clock_backwards', flipper(mode=1), clock(T0 - 2_000_000, 40_000, 3)),
    # A NaN interval makes the original's budget 2^63 us: it would wait forever. Forced late
    # (+0x273) skips the wait and still runs every NaN conversion and comparison after it.
    case('interval_nan_forced_late', flipper(mode=9, interval_bits=0x7fc00000, b273=1), clock(T0 + 1_000, 1_000, 3)),
    case('interval_huge', flipper(mode=9, interval_bits=0x7f000000), clock(T0 + 1_000, 10**12, 3)),
    case('history_tiny_fps_zero', flipper(b273=1, history=[0.0] * 16), clock(T0 + 10, 1, 2)),
    # The 60 FPS cases with clocks long enough for a 1/30 s wait, so BB_TARGET_FPS=30 can be
    # checked against "30 FPS++" on them too (the original uses only part of the clock).
    case('mode2_60_long_clock', flipper(mode=2), spin()),
    case('pending_mode_switch_long_clock', flipper(mode=1, pending_mode=2, b276=1), spin()),
    case('sleeping_wait_short_long_clock', flipper(mode=2), clock(T0 + 14_000, 1_000, 40), spin_only=0, usleeps=16),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
