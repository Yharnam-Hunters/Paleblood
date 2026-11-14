#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the AI sight functions in sight.cpp (invented data, not game
recordings): the sight test, the sight query's target reset and the target-list refresh.

usage: sight_cases.py OUT_DIR

Writes cases for every function; verify.py keeps the ones whose address matches the function.
Think-parameter rows here are made-up numbers chosen to reach each branch, not the game's.
"""
import json
import math
from pathlib import Path
import random
import struct
import sys

MASK64 = (1 << 64) - 1
SIGHT_TEST = '0x016a52b0'
QUERY_RESET = '0x016a5a50'
REFRESH = '0x016b2760'
CAST_RAY = '0x00f212a0'
PHYSICS_MANAGER = '0x0593d700'
RECORD_NOTICE = '0x016b2af0'
REPORT_NOTICE = '0x023f36f0'
ALLOCATOR = '0x05940420'
HANDLE_ID = '0x01624370'   # a game address the fake handle vtables point at; stubbed
SOURCE_ID = '0x01624250'
ALLOC_FN = '0x00400020'
RAY_FILTER = 0x13579bdf2468ace0   # an opaque value the ray input carries

NAN_A, NAN_B = 0x7fc12345, 0xffc54321
INF = float('inf')


def f32(value):
    return struct.pack('<f', value)


def bits(word):
    return struct.pack('<I', word & 0xffffffff)


def fl(value):
    return bits(value) if isinstance(value, int) else f32(value)


def vec(x, y, z, w=1.0):
    return b''.join(fl(v) for v in (x, y, z, w))


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


# ---- 0x016a52b0: the sight test ----

# Row offsets (NpcThinkParam): eye_dist, nose_dist, eye_angX, eye_angY, eyeDist_forDark,
# eyeDist_forPitchDark, eye_BackOffsetDist, eye_BeginDist.
ROW_U16 = {'eye': 0x30, 'nose': 0x36, 'dark': 0x64, 'pitch': 0x68, 'back': 0x74, 'begin': 0x76}
ROW_U8 = {'ang_x': 0x51, 'ang_y': 0x52}

# The ray input's bytes the original sets (padding left out), and the collector's.
RAY_INPUT_FIELDS = [(0x0, 0xa), (0xc, 0xd), (0x20, 0x34)]
RAY_HIT_FIELDS = [(0x0, 0xc), (0x10, 0x34), (0x48, 0x6), (0x50, 0x16), (0x68, 0x19)]


def physics(k, world=True):
    k.buf('physics', 0x30, 0x71)
    k.buf('holder', 0x10, 0x73)
    k.global_ptr(PHYSICS_MANAGER, 'physics')
    k.ptr('physics', 0x28, 'holder')
    if not world:
        k.ptr('holder', 0x8, None)
        return
    k.buf('world', 0x160, 0x75)
    k.buf('filter_owner', 0x5f0, 0x77)
    k.ptr('holder', 0x8, 'world')
    k.ptr('world', 0x150, 'filter_owner')
    k.put('filter_owner', 0x5e8, struct.pack('<Q', RAY_FILTER))


def ray_stub(k, hit):
    k.stub(CAST_RAY, 0, argc=3, argi=[0],
           argmem=[{'arg': 1, 'offset': o, 'size': n} for o, n in RAY_INPUT_FIELDS] +
                  [{'arg': 2, 'offset': o, 'size': n} for o, n in RAY_HIT_FIELDS],
           writes=[{'arg': 2, 'offset': 0x80, 'bytes': f'{hit:02x}'}])


def sight_case(cid, q, row, check=1, world=True, hit=0, byte=0x21):
    """q: the query's fields; row: dict of row fields, or None for no row."""
    k = Case(SIGHT_TEST, cid, 'i8')
    k.buf('query', 0x70, byte)
    k.buf('holder_params', 0x10, byte + 2)
    k.ptr('query', 0x0, 'holder_params')
    if row is None:
        k.ptr('holder_params', 0x8, None)
    else:
        k.buf('row', 0x80, byte + 4)
        k.ptr('holder_params', 0x8, 'row')
        for name, value in row.items():
            if name in ROW_U16:
                k.put('row', ROW_U16[name], struct.pack('<H', value))
            else:
                k.put('row', ROW_U8[name], bytes([value]))
    k.put('query', 0x10, vec(*q['pos']))
    k.put('query', 0x20, vec(*q['facing'], q.get('facing_w', 0.0)))
    k.put('query', 0x30, fl(q.get('eye_h', 1.5)) + fl(q.get('size', 0.4)) + fl(q.get('scale', 1.0)))
    k.put('query', 0x40, vec(*q['target'], q.get('target_w', 1.0)))
    k.put('query', 0x50, fl(q.get('target_eye_h', 1.2)) + fl(q.get('target_size', 0.3)) +
          fl(q.get('visibility', 1.0)))
    k.put('query', 0x5c, bytes([q.get('dark', 0)]))
    k.put('query', 0x64, bytes([q.get('unseeable', 0)]))
    physics(k, world)
    ray_stub(k, hit)
    k.c['args'] = {'rdi': 'buf:query', 'rsi': check}
    return k.done()


def plain_row(**extra):
    row = {'eye': 12, 'nose': 2, 'ang_x': 60, 'ang_y': 45, 'dark': 8, 'pitch': 5, 'back': 1, 'begin': 1}
    row.update(extra)
    return row


AHEAD = {'pos': (0.0, 0.0, 0.0), 'facing': (0.0, 0.0, 1.0)}


def at(target, **extra):
    q = dict(AHEAD, target=target)
    q.update(extra)
    return q


def sight_test_cases():
    n = 0
    # Nose: closer than nose + both sizes, at any angle; the boundary is strict (squared).
    for target in [(0.0, 0.0, -2.0), (0.0, 0.0, -2.69), (0.0, 0.0, -2.7), (0.0, 0.0, -2.71), (2.0, 1.0, -1.0),
                   (0.0, 0.0, 0.0), (0.0, 2.6, 0.0), (1.9, 0.0, 1.9)]:
        for unseeable in (0, 1):
            yield sight_case(f'nose_{n}', at(target, unseeable=unseeable), plain_row(eye=0))
            n += 1
    # Eye distance by darkness (0 light, 1 dark, 2 pitch dark, others light), measured from the
    # moved-back eye point; begin distance is a minimum.
    for dark in (0, 1, 2, 3, 0xff):
        for z in (0.5, 0.79, 0.81, 1.2, 4.0, 5.9, 6.1, 7.9, 8.1, 11.0, 11.6, 11.75, 12.0, 13.0):
            yield sight_case(f'eye_dist_{n}', at((0.0, 0.0, z), dark=dark, size=0.0, target_size=0.0),
                             plain_row(nose=0))
            n += 1
    # Scales: the target's visibility and the observer's sight scale multiply every eye distance.
    for vis, scale in [(0.5, 1.0), (1.0, 0.5), (2.0, 2.0), (0.0, 1.0), (-1.0, 1.0), (INF, 1.0), (NAN_A, 1.0),
                       (1.0, NAN_B), (0.25, 3.0)]:
        for z in (2.0, 5.0, 10.0, 20.0, 40.0):
            yield sight_case(f'scale_{n}', at((0.0, 0.0, z), visibility=vis, scale=scale), plain_row(nose=0))
            n += 1
    # Width: the flat angle between the facing and the line (cosine of eye_angY); heights ignored.
    for ang_y in (0, 30, 45, 89, 90, 135, 180, 200, 255):
        for x in (0.0, 1.0, 2.0, 2.9, 3.0, 3.1, 5.0, -3.0, -5.0, 8.0):
            yield sight_case(f'width_{n}', at((x, 0.0, 3.0)), plain_row(nose=0, ang_y=ang_y, ang_x=89))
            n += 1
    # Height: the elevation of the line (cosine of eye_angX).
    for ang_x in (0, 20, 45, 60, 89, 90, 180, 255):
        for y in (0.0, 0.5, 1.0, 2.9, 3.0, 3.1, 6.0, -3.0, -6.0, 9.0):
            yield sight_case(f'height_{n}', at((0.0, y, 3.0)), plain_row(nose=0, ang_x=ang_x, ang_y=90, eye=30))
            n += 1
    # The ray: asked or not, a world or none, hit or clear.
    for check in (0, 1):
        for world in (True, False):
            for hit in (0, 1, 0x80):
                for target in [(0.0, 0.0, 5.0), (0.5, -0.25, 3.0), (0.0, 0.0, 1.0), (-0.0, 2.0, 4.0)]:
                    yield sight_case(f'ray_{n}', at(target), plain_row(nose=0), check=check, world=world, hit=hit)
                    n += 1
    # The ray's direction lanes: zero, negative zero, negative, tiny, huge, NaN.
    for pos, target in [((0.0, 0.0, 0.0), (0.0, 0.3, 4.0)), ((1.0, 0.0, 1.0), (1.0, 0.0, 5.0)),
                        ((-0.0, -1.5, -0.0), (-0.0, -0.3, 4.0)), ((3.0, 0.0, 0.0), (-1.0, -1.0, 3.0)),
                        ((1e-30, 0.0, 0.0), (2e-30, 0.0, 3.0)), ((0.0, 0.0, 0.0), (1e-38, 0.0, 3.0)),
                        ((0.0, 0.0, 0.0), (0.0, NAN_A, 3.0)), ((0.0, 0.0, 0.0), (2.0, 0.3, 2.0))]:
        for eye_h, target_h in [(1.5, 1.2), (0.0, 0.0), (-0.0, 0.3), (1.2, 1.5)]:
            q = {'pos': pos, 'facing': (0.0, 0.0, 1.0), 'target': target, 'eye_h': eye_h, 'target_eye_h': target_h}
            yield sight_case(f'ray_dir_{n}', q, plain_row(nose=0, ang_x=90, ang_y=90, eye=30, begin=0, back=0))
            n += 1
    # Far from the origin: the ray's end point, rebuilt from the start plus the difference, rounds.
    rng = random.Random(0x2fd5250)
    for _ in range(12):
        pos = tuple(rng.uniform(-3000, 3000) for _ in range(3))
        target = (pos[0] + rng.uniform(-2, 2), pos[1] + rng.uniform(-1, 1), pos[2] + rng.uniform(3, 6))
        q = {'pos': pos, 'facing': (0.0, 0.0, 1.0), 'target': target, 'eye_h': rng.uniform(0, 2),
             'target_eye_h': rng.uniform(0, 2)}
        yield sight_case(f'ray_far_{n}', q, plain_row(nose=0, ang_x=90, ang_y=90, eye=30, begin=0, back=0))
        n += 1
    # Near the origin looking far away: start and end of very different size, so that the
    # difference rounds and start + difference is not the end.
    for _ in range(10):
        pos = (rng.uniform(-0.5, 0.5), 0.0, rng.uniform(-0.5, 0.5))
        target = (rng.uniform(-50, 50), rng.uniform(-20, 20), rng.uniform(900, 3000))
        q = {'pos': pos, 'facing': (0.0, 0.0, 1.0), 'target': target, 'eye_h': rng.uniform(0.1, 2),
             'target_eye_h': rng.uniform(0.1, 2)}
        yield sight_case(f'ray_long_{n}', q, plain_row(nose=0, ang_x=90, ang_y=90, eye=0xffff, begin=0, back=0))
        n += 1
    # No row: every distance and angle reads as 0.
    for target in [(0.0, 0.0, 0.5), (0.0, 0.0, 0.69), (0.0, 0.0, 0.71), (0.0, 0.0, 3.0), (0.0, 0.0, 0.0)]:
        for size in (0.4, 0.0, -1.0):
            yield sight_case(f'no_row_{n}', at(target, size=size), None)
            n += 1
    # Odd facings and positions.
    odd = [((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 4.0)), ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 4.0)),
           ((0.0, 0.0, 0.0), (NAN_A, 0.0, 1.0), (0.0, 0.0, 4.0)), ((0.0, 0.0, 0.0), (INF, 0.0, 0.0), (4.0, 0.0, 0.0)),
           ((0.0, 0.0, 0.0), (1e-25, 0.0, 0.0), (4.0, 0.0, 0.0)), ((INF, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 4.0)),
           ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, INF)), ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, INF, 4.0)),
           ((NAN_B, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 4.0)), ((1e20, 0.0, 1e20), (1.0, 0.0, 1.0), (1e20, 0.0, 1.1e20)),
           ((0.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 0.0, 4.0)), ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 4.0))]
    for pos, facing, target in odd:
        for w in (0.0, NAN_A, 5.0):
            q = {'pos': pos, 'facing': facing, 'target': target, 'facing_w': w, 'target_w': w}
            yield sight_case(f'odd_{n}', q, plain_row(nose=1))
            n += 1
    # Huge or odd angle and distance fields.
    for row in [plain_row(nose=0, ang_x=255, ang_y=255), plain_row(nose=0xffff), plain_row(nose=0, eye=0xffff, begin=0xffff),
                plain_row(nose=0, back=0xffff), plain_row(nose=0, back=20, eye=30), plain_row(nose=0, begin=0, back=0),
                plain_row(nose=0, eye=0), plain_row(nose=0, ang_x=0, ang_y=0)]:
        for target in [(0.0, 0.0, 3.0), (1.0, 0.5, 6.0), (0.0, 0.0, -3.0), (0.0, 0.0, 25.0)]:
            yield sight_case(f'fields_{n}', at(target), row)
            n += 1
    # Random scenes.
    rng = random.Random(0x16a52b0)
    for _ in range(400):
        pos = tuple(rng.uniform(-50, 50) for _ in range(3))
        a = rng.uniform(-3.2, 3.2)
        facing = (math.sin(a), rng.choice([0.0, rng.uniform(-0.2, 0.2)]), math.cos(a))
        target = tuple(c + rng.uniform(-15, 15) for c in pos)
        q = {'pos': pos, 'facing': facing, 'target': target, 'eye_h': rng.uniform(0, 2), 'size': rng.uniform(0, 1),
             'scale': rng.choice([1.0, rng.uniform(0.5, 1.5)]), 'target_eye_h': rng.uniform(0, 2),
             'target_size': rng.uniform(0, 1), 'visibility': rng.choice([1.0, 0.5, rng.uniform(0, 2)]),
             'dark': rng.choice([0, 0, 1, 2]), 'unseeable': rng.choice([0] * 9 + [1])}
        row = None if rng.random() < 0.05 else {
            'eye': rng.randrange(0, 30), 'nose': rng.randrange(0, 6), 'ang_x': rng.randrange(0, 181),
            'ang_y': rng.randrange(0, 181), 'dark': rng.randrange(0, 20), 'pitch': rng.randrange(0, 10),
            'back': rng.randrange(0, 5), 'begin': rng.randrange(0, 5)}
        yield sight_case(f'random_{n}', q, row, check=rng.choice([0, 1, 1]), world=rng.random() > 0.1,
                         hit=rng.choice([0, 1]), byte=rng.randrange(0x10, 0xf0))
        n += 1


# ---- 0x016a5a50: the sight query's target reset ----

def query_reset_cases():
    for n, byte in enumerate((0x00, 0x5a, 0xff, 0x13)):
        k = Case(QUERY_RESET, f'reset_{n}', 'void')
        k.buf('query', 0x70, byte)
        k.c['args'] = {'rdi': 'buf:query'}
        yield k.done()


# ---- 0x016b2760: refresh or add a target entry ----

NODE_SIZE = 0xa0


def entry_node(k, name, node_kind, kind, byte):
    """A node whose handles (character at +0x48, place at +0x60) both point at id vtables."""
    k.buf(name, NODE_SIZE, byte)
    k.put(name, 0x2, bytes([node_kind]))
    k.put(name, 0x28, struct.pack('<i', kind))
    for off in (0x48, 0x60):
        k.buf(f'{name}_vt{off:x}', 0xb0, byte + 1)
        k.ptr(name, off, f'{name}_vt{off:x}')
        k.guest(f'{name}_vt{off:x}', 0xa8, HANDLE_ID)


def refresh_case(cid, nodes, wanted, with_character=True, source=True, alloc=True, sense=3, byte=0x31):
    """nodes: (node_kind, kind, id) per node, first first; ids are what the handle stub answers."""
    k = Case(REFRESH, cid, 'i8')
    k.buf('list', 0x18, byte)
    k.buf('character', 0x10, byte + 2)
    k.put('character', 0x8, struct.pack('<i', wanted))
    previous, offset = 'list', 0x8
    for i, (node_kind, kind, _) in enumerate(nodes):
        name = f'node{i}'
        entry_node(k, name, node_kind, kind, byte + 3 + 5 * i)
        k.ptr(previous, offset, name)
        previous, offset = name, 0x8
    k.ptr(previous, offset, None)
    if source:
        k.buf('source', 0x10, byte + 4)
        k.buf('source_vt', 0x90, byte + 6)
        k.ptr('source', 0, 'source_vt')
        k.guest('source_vt', 0x88, SOURCE_ID)
    k.buf('allocator', 0x10, byte + 8)
    k.buf('allocator_vt', 0x60, byte + 9)
    k.buf('new_node', NODE_SIZE, byte + 10)
    k.global_ptr(ALLOCATOR, 'allocator')
    k.ptr('allocator', 0, 'allocator_vt')
    k.guest('allocator_vt', 0x58, ALLOC_FN)
    # The search asks each character or place entry's handle for its id until one matches.
    found = False
    for node_kind, kind, ident in nodes:
        if node_kind & 0x7f == 1 and kind in (1, 2) and wanted != -1:
            k.stub(HANDLE_ID, ident & MASK64)
            if ident == wanted:
                found = True
                break
    if not found:
        k.stub(ALLOC_FN, 'buf:new_node' if alloc else 0, argc=3, argi=[0, 1, 2])
    k.stub(RECORD_NOTICE, 0, argc=5, argi=[0, 1], argu32=[2, 3, 4])
    k.stub(SOURCE_ID, 0x4242)
    k.stub(REPORT_NOTICE, 0, argc=2, argu32=[0], argi=[1])
    k.c['args'] = {'rdi': 'buf:list', 'rsi': 'buf:character' if with_character else 0, 'rdx': sense & MASK64,
                   'rcx': 'buf:source' if source else 0}
    return k.done()


def refresh_cases():
    n = 0
    lists = [
        [],
        [(1, 1, 7)], [(1, 2, 7)], [(1, 1, 8)], [(1, 3, 7)], [(2, 1, 7)], [(0x81, 1, 7)], [(0x82, 2, 7)],
        [(1, 1, 5), (1, 2, 7)], [(1, 2, 7), (1, 1, 7)], [(4, 1, 7), (1, 1, 6), (1, 1, 7)],
        [(1, 0, 7), (1, -1, 7), (1, 1, 9)], [(1, 1, -1), (1, 2, 7)], [(0x7f, 1, 7)], [(1, 1, 6), (1, 1, 5)],
    ]
    for nodes in lists:
        for wanted in (7, -1, 0):
            for source in (True, False):
                yield refresh_case(f'refresh_{n}', nodes, wanted, source=source, sense=n % 7)
                n += 1
    for nodes in ([], [(1, 1, 5)]):
        for source in (True, False):
            yield refresh_case(f'no_memory_{n}', nodes, 7, source=source, alloc=False)
            n += 1
    yield refresh_case(f'no_character_{n}', [(1, 1, 7)], 7, with_character=False)
    n += 1
    for sense in (-1, 6, 7, 0x7fffffff):
        yield refresh_case(f'sense_{n}', [(1, 1, 7)], 7, sense=sense)
        n += 1
        yield refresh_case(f'sense_{n}', [], 7, sense=sense)
        n += 1


GENERATORS = [sight_test_cases, query_reset_cases, refresh_cases]


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
