#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the AI goal functions (game/ai/goals.cpp), for tools/verify.py run.

usage: goals_cases.py OUT_DIR reserve|no_update|update_time|no_interrupt|combo_cancel

- reserve: the scheduler's slot from the delay (0, whole and fractional frame counts, past the
  ring, negative, -0, tiny, NaN with a payload, infinities, out of the 64-bit range) and the
  cursor (0, 99, negative, the 32-bit extremes); the first slot free, at the limit, past it; full
  runs before a free slot, across the ring's end, all 100 full; the disabled byte (with a full
  slot and with a count of 255, which wraps); limits 0, 1 and 255; NaN delays when the booking
  moves; and 200 seeded random schedulers. The scheduler's 0x6c bytes are compared.
- no_update, update_time, no_interrupt, combo_cancel: a definition found (its bytes around the
  option filled with a pattern), none found, the manager missing (the fatal report is stubbed and
  leaves it missing, so the lookup gets the null read again), ids with
  garbage in the upper half of the register, flag bytes with garbage above them, and float pairs
  with NaN payloads, infinities and -0. The lookup (0x016dda50) is stubbed and compared.
"""
import json
import os
import random
import struct
import sys

ADDRESSES = {'reserve': '0x016a3be0', 'no_update': '0x0167b6b0', 'update_time': '0x0167b710',
             'no_interrupt': '0x0167b790', 'combo_cancel': '0x0167b7f0'}
SLOTS = 100
SCHEDULER_SIZE = 0x6c
DEFINITION_SIZE = 0x140
MANAGER_GLOBAL = '0x0593b0a0'
MANAGER_NAME = '0x04d35687'
SINGLETON_FILE, SINGLETON_MESSAGE, SINGLETON_LINE = '0x04d3b369', '0x04d3b3bd', 0xb1
FIND_DEFINITION = '0x016dda50'
FATAL_ERROR = '0x024b55b0'


def f32(v):
    return struct.pack('<f', v).hex()


def f32_bits(bits):
    return struct.pack('<I', bits).hex()


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid,
            'returns': 'f32' if variant == 'reserve' else 'void',
            'args': {}, 'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def scheduler_bytes(counts, cursor, limit, disabled, tail=b'\x5a\x5b'):
    assert len(counts) == SLOTS
    return bytes(counts) + struct.pack('<iBB', cursor, limit, disabled) + tail


def reserve_case(cid, delay_hex, counts=None, cursor=0, limit=3, disabled=0):
    c = base('reserve', cid)
    counts = counts if counts is not None else [0] * SLOTS
    c['buffers'] = {'scheduler': {'size': SCHEDULER_SIZE,
                                  'bytes': scheduler_bytes(counts, cursor, limit, disabled).hex()}}
    c['args'] = {'rdi': 'buf:scheduler', 'xmm0': delay_hex}
    return c


def full_from(start, length, limit=3, base_counts=None):
    counts = list(base_counts) if base_counts else [0] * SLOTS
    for i in range(length):
        counts[(start + i) % SLOTS] = limit
    return counts


def reserve_cases():
    out = []
    for name, delay in (('zero', 0.0), ('one_second', 1.0), ('one_frame', 1 / 30), ('half_frame', 0.5 / 30),
                        ('fraction', 2.51), ('just_under_100_frames', 3.3333), ('past_ring', 100.0),
                        ('large', 123456.7), ('negative', -0.5), ('negative_frame', -1 / 30),
                        ('minus_zero', -0.0), ('tiny', 1e-30), ('huge', 1e20), ('negative_huge', -1e20),
                        ('int32_edge', 2.0 ** 31 / 30), ('int64_edge', 2.0 ** 63 / 30)):
        out.append(reserve_case(f'delay_{name}', f32(delay), cursor=7))
    for name, bits in (('nan', 0x7fc00000), ('nan_payload', 0x7fc12345), ('negative_nan', 0xffc00001),
                       ('inf', 0x7f800000), ('negative_inf', 0xff800000), ('denormal', 0x00000001)):
        out.append(reserve_case(f'delay_{name}', f32_bits(bits), cursor=3))
    for cursor in (0, 99, 100, 150, -1, -5, -100, 0x7fffffff, -0x80000000, 12345678):
        out.append(reserve_case(f'cursor_{cursor & 0xffffffff:x}', f32(1.0), cursor=cursor))
    # The first slot: below the limit, at it, past it (a count above the limit).
    out.append(reserve_case('first_below_limit', f32(0.0), counts=[2] + [0] * 99))
    out.append(reserve_case('first_at_limit', f32(0.0), counts=[3] + [0] * 99))
    out.append(reserve_case('first_past_limit', f32(0.0), counts=[9] + [0] * 99))
    for start, length in ((10, 1), (10, 5), (40, 30), (95, 10), (99, 2), (0, 99), (50, 99)):
        out.append(reserve_case(f'full_{start}_{length}', f32(start / 30 + 0.001),
                                counts=full_from(start, length)))
    out.append(reserve_case('all_full', f32(1.0), counts=[3] * SLOTS))
    out.append(reserve_case('all_full_mixed_counts', f32(2.0), counts=[3 + i % 5 for i in range(SLOTS)]))
    out.append(reserve_case('all_full_but_previous', f32(0.5), counts=full_from(15, 99)))
    out.append(reserve_case('disabled_full_slot', f32(0.0), counts=[3] * SLOTS, disabled=1))
    out.append(reserve_case('disabled_wraps', f32(0.0), counts=[255] + [0] * 99, disabled=1))
    out.append(reserve_case('disabled_other_value', f32(0.2), counts=[7] * SLOTS, disabled=0x80))
    out.append(reserve_case('limit_zero', f32(1.0), limit=0))
    out.append(reserve_case('limit_zero_disabled', f32(1.0), limit=0, disabled=1))
    out.append(reserve_case('limit_one', f32(0.0), counts=full_from(0, 3, limit=1), limit=1))
    out.append(reserve_case('limit_255_room', f32(0.0), counts=[254] + [0] * 99, limit=255))
    out.append(reserve_case('limit_255_full', f32(0.0), counts=[255, 255] + [0] * 98, limit=255))
    for name, bits in (('nan_moved', 0x7fc0abcd), ('inf_moved', 0x7f800000), ('negative_nan_all_full', 0xffc00007)):
        counts = [3] * SLOTS if 'all_full' in name else full_from(0, 4)
        out.append(reserve_case(f'delay_{name}', f32_bits(bits), counts=counts))
    rng = random.Random(1094)
    for i in range(200):
        limit = rng.choice((0, 1, 2, 3, 3, 3, 5, 255))
        counts = [rng.choice((0, limit, limit, rng.randrange(256))) for _ in range(SLOTS)]
        delay = rng.choice((rng.uniform(-5, 10), rng.uniform(0, 4), float(rng.randrange(0, 200)) / 30))
        out.append(reserve_case(f'random_{i:03d}', f32(delay), counts=counts,
                                cursor=rng.choice((rng.randrange(SLOTS), rng.randrange(-2 ** 31, 2 ** 31))),
                                limit=limit, disabled=rng.choice((0, 0, 0, 1))))
    return out


def definition_bytes():
    return bytes((0x30 + i) & 0xff for i in range(DEFINITION_SIZE)).hex()


def option_case(variant, cid, goal_id, args, found=True, manager='present'):
    """manager: 'present' or 'missing' (the stubbed report leaves it missing)."""
    c = base(variant, cid)
    c['buffers'] = {'manager': {'size': 0x40}, 'definition': {'size': DEFINITION_SIZE, 'bytes': definition_bytes()}}
    if manager == 'present':
        c['memory'].append({'addr': MANAGER_GLOBAL, 'pointer': 'manager'})
    else:
        c['memory'].append({'addr': MANAGER_GLOBAL, 'bytes': '00' * 8})
        report = {'address': FATAL_ERROR, 'argc': 4,
                  'argi': [0, 2, 3], 'argu32': [1]}
        c['stubs'].append(report)
    c['stubs'].append({'address': FIND_DEFINITION, 'argc': 1, 'argu32': [1],
                       'ret': 'buf:definition' if found else 0})
    c['args'] = {'rdi': goal_id, **args}
    return c


def option_cases(variant):
    out = []
    if variant in ('no_update', 'no_interrupt'):
        values = (('one', 1), ('zero', 0), ('two', 2), ('ff', 0xff), ('garbage_above', 0x1234567800000101),
                  ('garbage_above_zero', 0xffffffffffffff00))
        for name, value in values:
            out.append(option_case(variant, f'found_{name}', 2000, {'rsi': value}))
        out.append(option_case(variant, 'not_found', 2001, {'rsi': 1}, found=False))
        args = {'rsi': 1}
    elif variant == 'update_time':
        pairs = (('zero', 0.0, 0.0), ('range', 0.5, 1.5), ('reversed', 3.0, 1.0), ('negative', -1.0, -0.25),
                 ('minus_zero', -0.0, -0.0), ('huge', 1e30, 3.4e38), ('tiny', 1e-40, 1e-45))
        for name, a, b in pairs:
            out.append(option_case(variant, f'found_{name}', 2000, {'xmm0': f32(a), 'xmm1': f32(b)}))
        for name, a, b in (('nan_payloads', 0x7fc12345, 0xffc54321), ('infinities', 0x7f800000, 0xff800000),
                           ('signalling_nan', 0x7f800001, 0x7fa00000)):
            out.append(option_case(variant, f'found_{name}', 2000, {'xmm0': f32_bits(a), 'xmm1': f32_bits(b)}))
        out.append(option_case(variant, 'wide_registers', 2000,
                               {'xmm0': f32(0.25) + '11223344556677889900aabb', 'xmm1': f32(0.75) + 'ccddeeff00112233aabbccdd'}))
        out.append(option_case(variant, 'not_found', 2001, {'xmm0': f32(1.0), 'xmm1': f32(2.0)}, found=False))
        args = {'xmm0': f32(0.1), 'xmm1': f32(0.2)}
    else:
        out.append(option_case(variant, 'found', 2000, {}))
        out.append(option_case(variant, 'found_garbage_args', 2000, {'rsi': 0xdeadbeef, 'xmm0': f32(9.0)}))
        out.append(option_case(variant, 'not_found', 2001, {}, found=False))
        args = {}
    for name, goal_id in (('id_zero', 0), ('id_negative', 0xffffffff), ('id_garbage_above', 0xdeadbeef000007d0),
                          ('id_large', 0x7fffffff)):
        out.append(option_case(variant, name, goal_id, args))
    out.append(option_case(variant, 'manager_missing', 2000, args, manager='missing'))
    out.append(option_case(variant, 'manager_missing_not_found', 2000, args, found=False, manager='missing'))
    return out


def cases(variant):
    return reserve_cases() if variant == 'reserve' else option_cases(variant)


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
