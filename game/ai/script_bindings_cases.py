#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the AI script bindings in script_bindings.cpp (invented data, not game
recordings): random draws, think and goal timers and numbers, target states and stat getters.

usage: script_bindings_cases.py OUT_DIR

Writes cases for every binding; verify.py keeps the ones whose address matches the function.
"""
import json
from pathlib import Path
import struct
import sys

MASK64 = (1 << 64) - 1
RANDOM_OWNER = '0x05956678'
FIND_CHARACTER = '0x01691e10'
# Game addresses the fake vtables point at; the cases stub them, so they never run.
RANDOM_NEXT = '0x01657bc0'
CHARACTER_SLOT = '0x01657c90'

THINK_SIZE = 0x9b0
PERCEPTION_SIZE = 0xd0
GOAL_SIZE = 0x218
THINK_TIMERS, THINK_NUMBERS, THINK_PERCEPTION, THINK_FORCED = 0x7c, 0xbc, 0x560, 0x9a8
GOAL_TIMERS, GOAL_NUMBERS, GOAL_TURN_TIMER = 0xb8, 0xf8, 0x210
STATE_CURRENT, STATE_PREVIOUS = 0xc4, 0xc8


def f32(value):
    return struct.pack('<f', value).hex()


def bits(word):
    return struct.pack('<I', word & 0xffffffff).hex()


def i32(value):
    return struct.pack('<i', value).hex()


NAN_A, NAN_B = 0x7fc12345, 0xffc54321   # quiet NaNs with distinct payloads
SPECIAL = [bits(NAN_A), bits(NAN_B), bits(0x7f800000), bits(0xff800000), bits(0x80000000),
           bits(0x00000001), bits(0x80000001)]


def pattern(size, byte):
    return bytes([(byte + i * 7) & 0xff for i in range(size)]).hex()


def base(address, cid, returns):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


# ---- random draws (0x01657ac0 float, 0x01657b20 int) ----

def random_case(address, cid, returns, draw, args):
    c = base(address, cid, returns)
    c['buffers'] = {'owner': {'size': 0xa68, 'bytes': pattern(0xa68, 0x11)},
                    'vtable': {'size': 0x20, 'bytes': pattern(0x20, 0x33)},
                    'func': {'size': 0x10, 'bytes': pattern(0x10, 0x55)}}
    c['memory'] = [{'addr': RANDOM_OWNER, 'pointer': 'owner'},
                   {'addr': 'buf:owner+2656', 'pointer': 'vtable'},
                   {'addr': 'buf:vtable+16', 'guest': RANDOM_NEXT}]
    c['stubs'] = [{'address': RANDOM_NEXT, 'argc': 1, 'argi': [0], 'ret': hex(draw & MASK64)}]
    c['args'] = dict(args, rdi='buf:func')
    return c


DRAWS = [0, 1, 0x80000, 0xfffff, 0x100000, 0xfffff000, 0xffffffff, 0x123456789abcdef0]


def random_float_cases():
    pairs = [(0.0, 1.0), (1.0, 0.0), (-5.0, 5.0), (2.5, 2.5), (-1e30, 1e30), (3.0, -7.25)]
    for d in DRAWS:
        for n, (lo, hi) in enumerate(pairs):
            yield random_case('0x01657ac0', f'float_draw_{d:x}_pair_{n}', 'f32', d,
                              {'xmm0': f32(lo), 'xmm1': f32(hi)})
    for n, lo in enumerate(SPECIAL):
        for m, hi in enumerate(SPECIAL + [f32(1.0)]):
            yield random_case('0x01657ac0', f'float_special_{n}_{m}', 'f32', 0x4321,
                              {'xmm0': lo, 'xmm1': hi})


def random_int_cases():
    pairs = [(0, 0), (0, 9), (-5, 5), (5, -5), (-2147483648, 2147483647), (2147483647, -2147483648),
             (100, 1000000), (-1, 0), (16777216, 16777300)]
    for d in DRAWS:
        for n, (lo, hi) in enumerate(pairs):
            yield random_case('0x01657b20', f'int_draw_{d:x}_pair_{n}', 'i32', d,
                              {'rsi': hex(lo & MASK64), 'rdx': hex(hi & MASK64)})
    yield random_case('0x01657b20', 'int_upper_bits_ignored', 'i32', 0xabcde,
                      {'rsi': hex(0xdead000000000003), 'rdx': hex(0xbeef000000000011)})


# ---- think and goal objects ----

def think_case(address, cid, returns, args=None, memory=(), perception=True):
    c = base(address, cid, returns)
    c['buffers'] = {'func': {'size': 0x10, 'bytes': pattern(0x10, 0x21)},
                    'think': {'size': THINK_SIZE, 'bytes': pattern(THINK_SIZE, 0x42)}}
    c['memory'] = [{'addr': 'buf:func+8', 'pointer': 'think'}]
    if perception:
        c['buffers']['perception'] = {'size': PERCEPTION_SIZE, 'bytes': pattern(PERCEPTION_SIZE, 0x63)}
        c['memory'].append({'addr': f'buf:think+{THINK_PERCEPTION}', 'pointer': 'perception'})
    c['memory'] += list(memory)
    c['args'] = dict(args or {}, rdi='buf:func')
    return c


def goal_case(address, cid, returns, args=None, memory=()):
    c = base(address, cid, returns)
    c['buffers'] = {'func': {'size': 0x10, 'bytes': pattern(0x10, 0x24)},
                    'goal': {'size': GOAL_SIZE, 'bytes': pattern(GOAL_SIZE, 0x48)}}
    c['memory'] = [{'addr': 'buf:func+8', 'pointer': 'goal'}] + list(memory)
    c['args'] = dict(args or {}, rdi='buf:func')
    return c


# Timer indices are signed (negative ones reach memory before the array, still inside the
# buffer here); number indices unsigned.
TIMER_INDICES = [0, 1, 7, 15, 16, 17, 0x7fffffff, -1, -2, -31, 0xdead00000000000f, 0xdead000000000010]
NUMBER_INDICES = [0, 1, 32, 63, 64, 65, 0x7fffffff, 0xffffffff, 0x80000000, 0xbeef00000000003f]
VALUES = [f32(0.0), f32(1.5), f32(-3.0), f32(1e-40)] + SPECIAL


def slot_cases(make, prefix, address_set, address_get, address_finish, array, indices, finish=True):
    for index in indices:
        for n, value in enumerate(VALUES[:4] if index not in (0, 15, 63) else VALUES):
            name = f'{prefix}_{index & MASK64:x}_{n}'
            yield make(address_set, 'set_' + name, 'void', {'rsi': hex(index & MASK64), 'xmm0': value})
            if address_get:
                where = (index & 0xffffffff)
                where = where - (1 << 32) if where >= 1 << 31 else where
                memory = []
                if -31 <= where <= 63:
                    memory = [{'addr': f'buf:{"think" if make is think_case else "goal"}+{array + 4 * where}',
                               'bytes': value}]
                yield make(address_get, 'get_' + name, 'f32', {'rsi': hex(index & MASK64)}, memory)
                if finish and address_finish:
                    yield make(address_finish, 'finish_' + name, 'i8', {'rsi': hex(index & MASK64)}, memory)


def timer_value_cases(make, address, array, holder):
    for n, value in enumerate([f32(0.0), bits(0x80000000), f32(-1.0), f32(0.001), f32(30.0),
                               bits(0x00000001), bits(0x80000001)] + SPECIAL):
        yield make(address, f'finish_value_{n}', 'i8', {'rsi': '0x3'},
                   [{'addr': f'buf:{holder}+{array + 12}', 'bytes': value}])


def think_slot_cases():
    yield from slot_cases(think_case, 'timer', '0x01657b80', '0x0165aff0', '0x0165b010', THINK_TIMERS,
                          TIMER_INDICES)
    yield from slot_cases(think_case, 'number', '0x01657ba0', '0x0165b030', None, THINK_NUMBERS,
                          NUMBER_INDICES)
    yield from timer_value_cases(think_case, '0x0165b010', THINK_TIMERS, 'think')


def goal_slot_cases():
    yield from slot_cases(goal_case, 'gtimer', '0x0167f2a0', '0x0167f820', '0x0167f840', GOAL_TIMERS,
                          TIMER_INDICES)
    yield from slot_cases(goal_case, 'gnumber', '0x0167f2d0', '0x0167f880', None, GOAL_NUMBERS,
                          NUMBER_INDICES)
    yield from timer_value_cases(goal_case, '0x0167f840', GOAL_TIMERS, 'goal')
    for n, value in enumerate([f32(0.0), bits(0x80000000), f32(-2.0), f32(0.5), bits(0x00000001)] + SPECIAL):
        yield goal_case('0x0167f860', f'turn_timer_{n}', 'i8', {},
                        [{'addr': f'buf:goal+{GOAL_TURN_TIMER}', 'bytes': value}])


# ---- target states ----

STATES = [0, 1, 2, 3, 4, -1, 0x7fffffff, -2147483648]


def state_cases():
    checks = [('0x0165baf0', 'battle'), ('0x0165bb30', 'caution'), ('0x0165bb50', 'find')]
    for current in STATES:
        for previous in dict.fromkeys((current, 0, 3, -1)):
            memory = [{'addr': f'buf:perception+{STATE_CURRENT}', 'bytes': i32(current)},
                      {'addr': f'buf:perception+{STATE_PREVIOUS}', 'bytes': i32(previous)}]
            tag = f'{current & 0xffffffff:x}_{previous & 0xffffffff:x}'
            for address, name in checks:
                yield think_case(address, f'is_{name}_{tag}', 'i8', memory=memory)
            yield think_case('0x0165bb10', f'is_change_{tag}', 'i8', memory=memory)
            yield think_case('0x0165bb90', f'previous_{tag}', 'i32', memory=memory)
            for forced in (-1, 0, 3, 7, -2, 0x7fffffff):
                yield think_case('0x0165bb70', f'current_{tag}_forced_{forced & 0xffffffff:x}', 'i32',
                                 memory=memory + [{'addr': f'buf:think+{THINK_FORCED}', 'bytes': i32(forced)}])


# ---- stat getters (character found through 0x01691e10, stat through its vtable) ----

STAT_GETTERS = [('0x0165a1d0', 'sp', 0x70, 'i32'), ('0x0165a1f0', 'mp', 0x68, 'i32'),
                ('0x0165a210', 'hp', 0x58, 'i32'), ('0x0165a230', 'hp_rate', 0x78, 'f32')]


def stat_cases():
    for address, name, slot, returns in STAT_GETTERS:
        results = ([0, 1, 364, 0x7fffffff, 0x80000000, 0xffffffff, 0x123456789abcdef0] if returns == 'i32'
                   else [0x3f800000, 0x3f000000, 0, 0x80000000, NAN_A, 0xbf800000, 0x7f800000])
        for target in (0, 1, 2, -1, 0xabcd000000000001):
            c = think_case(address, f'{name}_none_{target & MASK64:x}', returns, {'rsi': hex(target & MASK64)})
            c['stubs'] = [{'address': FIND_CHARACTER, 'argc': 2, 'argi': [0], 'argu32': [1], 'ret': 0}]
            yield c
            for value in results:
                c = think_case(address, f'{name}_{target & MASK64:x}_{value:x}', returns,
                               {'rsi': hex(target & MASK64)})
                c['buffers']['character'] = {'size': 0x40, 'bytes': pattern(0x40, 0x77)}
                c['buffers']['vtable'] = {'size': 0x100, 'bytes': pattern(0x100, 0x99)}
                c['memory'] += [{'addr': 'buf:character+0', 'pointer': 'vtable'},
                                {'addr': f'buf:vtable+{slot}', 'guest': CHARACTER_SLOT}]
                c['stubs'] = [{'address': FIND_CHARACTER, 'argc': 2, 'argi': [0], 'argu32': [1],
                               'ret': 'buf:character'},
                              {'address': CHARACTER_SLOT, 'argc': 1, 'argi': [0], 'ret': hex(value)}]
                yield c


def cases():
    yield from random_float_cases()
    yield from random_int_cases()
    yield from think_slot_cases()
    yield from goal_slot_cases()
    yield from state_cases()
    yield from stat_cases()


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    ids = [c['address'] + c['id'] for c in generated]
    assert len(ids) == len(set(ids)), 'duplicate case ids'
    for c in generated:
        (out / f"{c['address']}_{c['id']}.json").write_text(json.dumps(c, indent=1) + '\n')
    print(f'{len(generated)} synthetic edge cases written to {out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
