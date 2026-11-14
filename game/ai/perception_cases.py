#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the AI perception functions in perception.cpp (invented data, not game
recordings): the visible-target lookup and IsVisibleTarget, the inside-angle test and
IsInsideTargetEx, and the target speed record.

usage: perception_cases.py OUT_DIR

Writes cases for every function; verify.py keeps the ones whose address matches the function.
Positions and directions come from small game functions that return the vector at a fixed
offset of their object (they run as they are), so the cases set them as object fields.
"""
import itertools
import json
from pathlib import Path
import random
import struct
import sys

MASK64 = (1 << 64) - 1
# Game functions that return the 16 bytes at a fixed offset of their argument.
VECTOR_AT_10 = '0x016a2750'
VECTOR_AT_20 = '0x016a2910'
VECTOR_AT_70 = '0x00959190'
VECTOR_AT_80 = '0x009591a0'
# Game addresses the fake vtables point at for ids; the cases stub them.
HANDLE_ID = '0x01624370'
FATAL_ERROR = '0x024b55b0'
FIND_CHARACTER = '0x01691e10'
INSIDE_DIRECTION = '0x01693850'
TARGET_VISIBLE = '0x016ad400'
CURRENT_TARGET = '0x016ad530'
WORLD_CHR_MAN = '0x0593e878'

NODE_SIZE = 0x90
THINK_SIZE = 0x9b0
NAN_A, NAN_B = 0x7fc12345, 0xffc54321
INF = float('inf')
PI = 3.1415927


def f32(value):
    return struct.pack('<f', value)


def bits(word):
    return struct.pack('<I', word & 0xffffffff)


def vec(x, y, z, w=1.0):
    return b''.join(bits(v) if isinstance(v, int) else f32(v) for v in (x, y, z, w))


def xmm(value):
    return (bits(value) if isinstance(value, int) else f32(value)).hex()


def pattern(size, byte):
    return bytearray((byte + i * 7) & 0xff for i in range(size))


class Case:
    """A case under construction: buffers as bytearrays, pointers as memory presets."""

    def __init__(self, address, cid, returns):
        self.c = {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {},
                  'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}
        self.raw = {}

    def buf(self, name, size, byte=0x11):
        self.raw[name] = pattern(size, byte)
        return self.raw[name]

    def put(self, name, offset, data):
        self.raw[name][offset:offset + len(data)] = data

    def ptr(self, name, offset, target):
        if target is None:
            self.put(name, offset, bytes(8))
        else:
            self.c['memory'].append({'addr': f'buf:{name}+{offset}', 'pointer': target})

    def global_ptr(self, address, target):
        self.c['memory'].append({'addr': address, 'pointer': target})

    def guest(self, name, offset, address):
        self.c['memory'].append({'addr': f'buf:{name}+{offset}', 'guest': address})

    def stub(self, address, ret, argc=1, **extra):
        entry = {'address': address, 'argc': argc, 'ret': ret}
        entry.update(extra)
        if 'argi' not in entry and 'argu32' not in entry:
            entry['argi'] = [0]
        self.c['stubs'].append(entry)

    def done(self):
        self.c['buffers'] = {n: {'size': len(b), 'bytes': bytes(b).hex()} for n, b in self.raw.items()}
        return self.c


def world_with_player(k, entry_byte=0x5a):
    k.buf('wcm', 0x68, 0x21)
    k.buf('player', 0x20, entry_byte)
    k.ptr('wcm', 0x60, 'player')
    k.global_ptr(WORLD_CHR_MAN, 'wcm')


def target_list(k, name, nodes, base=0x10, shift=0):
    """Nodes as dicts (node_kind, priority, visible, kind); linked from buf `name` + base + 8. Each
    node starts `shift` bytes into its buffer (so that a vector inside it is 16-byte aligned)."""
    previous, offset = name, base + 8
    for n, nd in enumerate(nodes):
        nn = f'{name}_node{n}'
        k.buf(nn, NODE_SIZE + shift, 0x30 + n * 3)
        k.put(nn, shift + 0x2, bytes([nd.get('node_kind', 1)]))
        k.put(nn, shift + 0x28, struct.pack('<i', nd.get('kind', 1)))
        p = nd.get('priority', 0.0)
        k.put(nn, shift + 0x30, bits(p) if isinstance(p, int) else f32(p))
        k.put(nn, shift + 0x40, bytes([nd.get('visible', 0)]))
        k.ptr(previous, offset, f'{nn}+{shift}' if shift else nn)
        previous, offset = nn, shift + 0x8
    k.ptr(previous, offset, None)


# ---- 0x016ad400: is the current target visible ----

KIND_SETS = [[], [1], [4], [5], [3], [11], [2], [7, 2], [1, 4], [4, 1], [5, 1], [1, 5], [3, 1], [1, 3],
             [11, 3], [3, 11], [1, 11], [0x81], [0x84], [0x85, 4], [4, 4], [2, 3], [2, 11, 1], [1, 1, 1],
             [6, 8, 9], [0x8b]]


def visible_lookup_cases():
    rng = random.Random(0x16ad400)
    n = 0
    for kinds in KIND_SETS:
        for variant in range(4):
            nodes = []
            for kind in kinds:
                pr = rng.choice([0.0, 1.0, 5.5, -0.5, 2.0, 100.0, -1.0, NAN_A, 1e-30])
                nodes.append({'node_kind': kind, 'priority': pr, 'visible': rng.choice([0, 1, 2, 0xff])})
            entries = [nd for nd in nodes if nd['node_kind'] & 0x7f == 1]
            if entries and not any(isinstance(nd['priority'], float) and nd['priority'] > -1.0 for nd in entries):
                entries[-1]['priority'] = 3.0   # no entry above -1 reads address 0x20 (a crash)
            k = Case(TARGET_VISIBLE, f'kinds_{n}', 'i8')
            k.buf('perception', 0xd0, 0x41 + variant)
            target_list(k, 'perception', nodes)
            k.c['args'] = {'rdi': 'buf:perception'}
            yield k.done()
            n += 1
    for n, (first, second) in enumerate([(2.0, 2.0), (1.0, 3.0), (3.0, 1.0), (-1.0, 0.0), (NAN_A, 0.5)]):
        k = Case(TARGET_VISIBLE, f'main_entry_{n}', 'i8')
        k.buf('perception', 0xd0)
        target_list(k, 'perception', [{'priority': first, 'visible': 1}, {'priority': second, 'visible': 0}])
        k.c['args'] = {'rdi': 'buf:perception'}
        yield k.done()


# ---- 0x0165ca50: IsVisibleTarget ----

SELECTORS = [-0x80000000, -3, -2, -1, 0, 1, 2, 10, 19, 20, 21, 22, 23, 24, 25, 26, 27, 99, 100, 101, 122,
             123, 124, 200, 0x7fffffff]


def is_visible_target_cases():
    for n, selector in enumerate(SELECTORS):
        for answer in (0, 1):
            k = Case('0x0165ca50', f'selector_{n}_{answer}', 'i8')
            k.buf('func', 0x10)
            k.buf('think', THINK_SIZE, 0x13)
            k.buf('perception', 0xd0, 0x15)
            k.ptr('func', 0x8, 'think')
            k.ptr('think', 0x560, 'perception')
            world_with_player(k, 0x60 + n)
            visible = (-2 <= selector <= -1) or selector in (20, 21, 22, 24, 25) or 100 <= selector <= 123
            if not visible:
                k.stub(TARGET_VISIBLE, answer, argc=1)
            k.c['args'] = {'rdi': 'buf:func', 'rsi': selector & MASK64}
            yield k.done()


# ---- 0x01658770: IsInsideTargetEx ----

def is_inside_target_ex_cases():
    n = 0
    for direction in list(range(10)) + [10, 11, 15]:
        for found_self, found_target, answer in ((1, 1, 1), (1, 1, 0), (0, 1, 0), (1, 0, 1)):
            if direction > 9 and (found_self, found_target, answer) != (1, 1, 1):
                continue
            k = Case('0x01658770', f'direction_{n}', 'i8')
            k.buf('func', 0x10)
            k.buf('think', THINK_SIZE, 0x17)
            k.buf('self', 0x20, 0x19)
            k.buf('target', 0x20, 0x1b)
            k.ptr('func', 0x8, 'think')
            k.stub(FIND_CHARACTER, 'buf:self' if found_self else 0, argc=2, argi=[0, 1])
            k.stub(FIND_CHARACTER, 'buf:target' if found_target else 0, argc=2, argi=[0, 1])
            if direction > 9:
                k.stub(FATAL_ERROR, 0, argc=4, argi=[0, 1, 2, 3])
            k.stub(INSIDE_DIRECTION, answer, argc=3, argi=[0, 1, 2], argf32=[0, 1])
            k.c['args'] = {'rdi': 'buf:func', 'rsi': (n * 3 - 5) & MASK64, 'rdx': (7 - n) & MASK64,
                           'rcx': direction, 'xmm0': xmm([90.0, 30.0, NAN_A, -45.0][n % 4]),
                           'xmm1': xmm([5.0, 0.0, INF, 1e9][n % 4])}
            yield k.done()
            n += 1


# ---- 0x01693850: inside the cone and the distance ----

def character(k, name, position, forward, side, byte):
    k.buf(name, 0x80, byte)
    k.buf(name + '_vt', 0x98, byte + 1)
    k.ptr(name, 0, name + '_vt')
    k.guest(name + '_vt', 0x80, VECTOR_AT_10)
    k.guest(name + '_vt', 0x88, VECTOR_AT_20)
    k.guest(name + '_vt', 0x90, VECTOR_AT_70)
    k.put(name, 0x10, vec(*position))
    k.put(name, 0x20, vec(*forward, 0.0))
    k.put(name, 0x70, vec(*side, 0.0))


HEADINGS = [100.0, 1000.0, 0.0, -0.0, PI, -PI, -1.5707964, 1.5707964, 0.7853982, -0.7853982, 0.78539824,
            2.3561945, -2.3561945, 2.0, -2.0, 0.5, 3.5, -3.5, NAN_A, 99.0, 1e30]
ANGLES = [90.0, 0.0, 30.0, 180.0, 360.0, 720.0, -90.0, 359.0, 1e6, 45.0, 120.0, INF, NAN_B, 1e-3, -720.0]
# Angles whose first wrap leaves them out of range (huge, or past the integer conversion).
HUGE_ANGLES = [1e12, -1e12, 3e9, 4.5e11, 1e38, 2.2e10, 7e8, 123456789.0]
DISTANCES = [10.0, 0.0, 3.5, 100.0, INF, NAN_A, -1.0, 1.0]


def inside_case(cid, self_pose, target_pose, heading, flag, angle, distance, extra_byte=0):
    k = Case(INSIDE_DIRECTION, cid, 'i8')
    character(k, 'self', *self_pose, 0x21)
    character(k, 'target', *target_pose, 0x31)
    word = (struct.unpack('<I', bits(heading) if isinstance(heading, int) else f32(heading))[0]
            | (extra_byte << 32) | (flag << 40))
    k.c['args'] = {'rdi': 'buf:self', 'rsi': 'buf:target', 'rdx': word, 'xmm0': xmm(angle), 'xmm1': xmm(distance)}
    return k.done()


def inside_direction_cases():
    rng = random.Random(0x1693850)
    still = ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0))
    n = 0
    for heading, flag in itertools.product(HEADINGS, (0, 1)):
        for _ in range(4):
            sp = tuple(rng.uniform(-20, 20) for _ in range(3))
            tp = tuple(c + rng.uniform(-8, 8) for c in sp)
            a = rng.uniform(-PI, PI)
            fw = (rng.choice([1, -1]) * abs(rng.uniform(0.1, 1)), rng.uniform(-0.3, 0.3), rng.uniform(-1, 1))
            sd = (fw[2], rng.uniform(-0.1, 0.1), -fw[0])
            yield inside_case(f'heading_{n}', (sp, fw, sd), (tp, (a, 0.0, 1.0), (1.0, 0.0, a)), heading, flag,
                              rng.choice(ANGLES), rng.choice(DISTANCES + [8.0, 12.0, 20.0]))
            n += 1
    for angle, distance in itertools.product(ANGLES + HUGE_ANGLES, DISTANCES):
        yield inside_case(f'cone_{n}', ((0.0, 0.0, 0.0),) + still[1:], ((3.0, 1.0, 2.0),) + still[1:],
                          0.0, 0, angle, distance)
        n += 1
    odd_positions = [(0.0, 0.0, 0.0), (0.0, 5.0, 0.0), (NAN_A, 0.0, 1.0), (INF, 0.0, 0.0), (0.0, 0.0, -INF),
                     (1e-30, 0.0, 1e-30), (1e20, 0.0, 1e20), (-0.0, -0.0, -0.0), (0.0, NAN_B, 0.0)]
    for pos, (heading, flag) in itertools.product(odd_positions, [(0.0, 0), (0.0, 1), (PI, 1), (-1.5707964, 1),
                                                                  (1.5707964, 1), (1.5707964, 0), (100.0, 0)]):
        yield inside_case(f'odd_{n}', ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
                          (pos, (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)), heading, flag, 90.0, INF)
        n += 1
    for fw in [(0.0, 1.0, 0.0), (0.0, 0.0, 0.0), (NAN_A, 0.0, 0.0), (INF, 0.0, 1.0), (1e-25, 0.0, 0.0)]:
        for heading, flag in [(0.0, 0), (PI, 0), (1.5707964, 0), (-1.5707964, 0)]:
            yield inside_case(f'facing_{n}', ((0.0, 0.0, 0.0), fw, fw), ((1.0, 0.0, 1.0), fw, fw), heading, flag,
                              120.0, 10.0)
            n += 1
    for extra, flag in [(0x5a, 0), (0, 2), (0, 0x80), (0xff, 0xff)]:
        yield inside_case(f'packing_{n}', ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
                          ((2.0, 0.0, -1.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)), 1.5707964, flag, 90.0, 10.0, extra)
        n += 1
    # Characters at the same place with a distance limit of 0: the length is exactly 0, so only a
    # `distance >= length` test (not `>`) lets the cone test run.
    for angle in (360.0, 90.0, 720.0):
        yield inside_case(f'same_place_{n}', ((1.0, 2.0, 3.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)),
                          ((1.0, 2.0, 3.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)), 0.0, 0, angle, 0.0)
        n += 1
    for who in ('self', 'target', 'both'):
        k = Case(INSIDE_DIRECTION, f'missing_{who}', 'i8')
        character(k, 'self', (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0), 0x21)
        k.c['args'] = {'rdi': 0 if who != 'target' else 'buf:self', 'rsi': 0 if who != 'self' else 'buf:self',
                       'rdx': 0, 'xmm0': xmm(90.0), 'xmm1': xmm(10.0)}
        yield k.done()


# ---- 0x0162d840: the target speed record ----

def speed_record(k, next_sample, full, target_id, samples, last, think_self, perception=True):
    k.buf('record', 0x210, 0x45)
    k.buf('think', THINK_SIZE, 0x47)
    k.buf('self', 0x90, 0x49)
    k.buf('self_vt', 0x48, 0x4b)
    k.ptr('record', 0x0, 'think')
    k.ptr('think', 0x0, 'self')
    k.ptr('self', 0x0, 'self_vt')
    k.guest('self_vt', 0x40, VECTOR_AT_80)
    k.put('self', 0x80, vec(*think_self))
    for i, s in enumerate(samples):
        k.put('record', 0xc + 4 * i, bits(s) if isinstance(s, int) else f32(s))
    k.put('record', 0x1ec, struct.pack('<i', next_sample))
    k.put('record', 0x1f0, bytes([full]))
    k.put('record', 0x1f4, struct.pack('<i', target_id))
    k.put('record', 0x200, vec(*last))
    if perception:
        k.buf('perception', 0xd0, 0x4d)
        k.ptr('think', 0x560, 'perception')
    else:
        k.ptr('think', 0x560, None)
    world_with_player(k)


def handle(k, name, position, byte=0x51):
    k.buf(name, 0x20, byte)
    k.buf(name + '_vt', 0xb0, byte + 2)
    k.ptr(name, 0, name + '_vt')
    k.guest(name + '_vt', 0x80, VECTOR_AT_10)
    k.guest(name + '_vt', 0xa8, HANDLE_ID)
    k.put(name, 0x10, vec(*position))


def speed_record_cases():
    rng = random.Random(0x162d840)
    n = 0

    def samples():
        return [rng.choice([rng.uniform(-8, 8), 0.0, -0.0]) for _ in range(30)]

    # The usual frame: same target, a step somewhere.
    for _ in range(60):
        nxt, full = rng.randrange(30), rng.choice([0, 1])
        k = Case('0x0162d840', f'step_{n}', 'void')
        start = tuple(rng.uniform(-30, 30) for _ in range(3))
        moved = tuple(c + rng.choice([0.0, rng.uniform(-1, 1)]) for c in start)
        me = tuple(c + rng.uniform(-10, 10) for c in start)
        speed_record(k, nxt, full, 77, samples(), start, me)
        handle(k, 'target', moved)
        k.stub(CURRENT_TARGET, 'buf:target', argc=2, argi=[0, 1])
        k.stub(HANDLE_ID, 77)
        k.c['args'] = {'rdi': 'buf:record', 'xmm0': xmm(rng.choice([1 / 30, 1 / 60, 0.1, 1 / 6]))}
        yield k.done()
        n += 1
    # Ring edges, the index-0 quirk and odd frame times.
    for nxt, full, dt in itertools.product((0, 1, 2, 28, 29), (0, 1), (1 / 30, 0.0, -0.5, NAN_A, INF)):
        k = Case('0x0162d840', f'edge_{n}', 'void')
        speed_record(k, nxt, full, -1, samples(), (1.0, 0.0, 1.0), (4.0, 0.0, 5.0))
        handle(k, 'target', (1.5, 0.25, 0.5))
        k.stub(CURRENT_TARGET, 'buf:target', argc=2, argi=[0, 1])
        k.stub(HANDLE_ID, (-1) & MASK64)
        k.c['args'] = {'rdi': 'buf:record', 'xmm0': xmm(dt)}
        yield k.done()
        n += 1
    # Odd positions (the target on the character, infinities, NaNs).
    for me, there in [((0.0, 0.0, 0.0), (0.0, 0.0, 0.0)), ((1.0, 2.0, 3.0), (1.0, 2.0, 3.0)),
                      ((INF, 0.0, 0.0), (0.0, 0.0, 1.0)), ((NAN_A, 0.0, 0.0), (1.0, 0.0, 1.0)),
                      ((0.0, 0.0, 0.0), (NAN_B, 1.0, 0.0)), ((1e20, 0.0, 0.0), (-1e20, 0.0, 0.0))]:
        k = Case('0x0162d840', f'odd_{n}', 'void')
        speed_record(k, 5, 0, 3, samples(), (0.5, 0.5, 0.5), me)
        handle(k, 'target', there)
        k.stub(CURRENT_TARGET, 'buf:target', argc=2, argi=[0, 1])
        k.stub(HANDLE_ID, 3)
        k.c['args'] = {'rdi': 'buf:record', 'xmm0': xmm(1 / 30)}
        yield k.done()
        n += 1
    # Another target: the record starts over.
    for old, new in [(77, 78), (0, 5), (5, 0), (-1, 0x7fffffff)]:
        k = Case('0x0162d840', f'new_target_{n}', 'void')
        speed_record(k, 12, 1, old, samples(), (0.0, 0.0, 0.0), (3.0, 0.0, 3.0))
        handle(k, 'target', (1.0, 0.0, 2.0))
        k.stub(CURRENT_TARGET, 'buf:target', argc=2, argi=[0, 1])
        k.stub(HANDLE_ID, new & MASK64)
        k.c['args'] = {'rdi': 'buf:record', 'xmm0': xmm(1 / 30)}
        yield k.done()
        n += 1
    # No perception choice: the first kind-11 node's character or place, or nothing.
    for kinds, node_kind in [([3, 11], 1), ([11], 2), ([11], 3), ([11], 0), ([3, 2], 1), ([], 1), ([0x8b], 1)]:
        k = Case('0x0162d840', f'node_{n}', 'void')
        speed_record(k, 4, 0, 9, samples(), (0.0, 0.0, 0.0), (2.0, 0.0, 2.0))
        nodes = [{'node_kind': kd, 'kind': node_kind} for kd in kinds]
        # The character handle (+0x48) keeps its position at +0x58: shift the node to align it.
        shift = 8 if node_kind == 1 else 0
        target_list(k, 'perception', nodes, shift=shift)
        for i, kd in enumerate(kinds):
            name = f'perception_node{i}'
            off, pos = (0x48, (1.0, 0.5, 1.0)) if node_kind == 1 else (0x60, (-1.0, 0.0, 3.0))
            k.ptr(name, shift + off, f'{name}_vt')
            k.buf(f'{name}_vt', 0xb0, 0x61)
            k.guest(f'{name}_vt', 0x80, VECTOR_AT_10)
            k.guest(f'{name}_vt', 0xa8, HANDLE_ID)
            k.put(name, shift + off + 0x10, vec(*pos))
        k.stub(CURRENT_TARGET, 0, argc=2, argi=[0, 1])
        if any(kd & 0x7f == 11 for kd in kinds) and node_kind in (1, 2):
            k.stub(HANDLE_ID, 9)
        k.c['args'] = {'rdi': 'buf:record', 'xmm0': xmm(1 / 30)}
        yield k.done()
        n += 1
    # No perception at all.
    k = Case('0x0162d840', 'no_perception', 'void')
    speed_record(k, 7, 1, 4, samples(), (0.0, 0.0, 0.0), (1.0, 0.0, 1.0), perception=False)
    k.c['args'] = {'rdi': 'buf:record', 'xmm0': xmm(1 / 30)}
    yield k.done()


GENERATORS = [visible_lookup_cases, is_visible_target_cases, is_inside_target_ex_cases, inside_direction_cases,
              speed_record_cases]


def main():
    out = Path(sys.argv[1])
    count = 0
    for generate in GENERATORS:
        for case in generate():
            directory = out / case['address']
            directory.mkdir(parents=True, exist_ok=True)
            (directory / f'{case["id"]}.json').write_text(json.dumps(case, indent=1) + '\n')
            count += 1
    print(f'{count} cases', file=sys.stderr)


if __name__ == '__main__':
    main()
