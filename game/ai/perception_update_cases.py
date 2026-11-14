#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for perception_update.cpp (invented data, not game recordings): the
perception update and the current target's handle.

usage: perception_update_cases.py OUT_DIR

Writes one directory per function address. The target-list update the perception update calls
first is stubbed (it is compared as a call with its perception pointer and frame time); so are
the owner's "pauses sensing" query and its battle notice. Positions and facings come from small
game functions that return the vector at a fixed offset of their object.
"""
import itertools
import json
from pathlib import Path
import random
import struct
import sys

UPDATE = '0x016a85c0'
CURRENT_HANDLE = '0x016acfa0'
TARGET_LIST_UPDATE = '0x016ab4c0'
# Game functions that return the 16 bytes at a fixed offset of their argument.
VECTOR_AT_10 = '0x016a2750'
VECTOR_AT_20 = '0x016a2910'
# Game addresses the owner's fake vtable points at; the cases stub them.
PAUSES_SENSING = '0x01624370'
BATTLE_NOTICE = '0x01691e10'

NODE_BUFFER = 0xa0
NAN_A, NAN_B = 0x7fc12345, 0xffc54321
INF = float('inf')

BATTLE, CAUTION, FIND, NORMAL = 3, 1, 2, 0
STATES = [0, 1, 2, 3, 4, -1, 0x101, 0x7fffffff]


def f32(value):
    return struct.pack('<f', value)


def word(value):
    return struct.pack('<I', value & 0xffffffff) if isinstance(value, int) else f32(value)


def vec(x, y, z, w=1.0):
    return b''.join(word(v) for v in (x, y, z, w))


def xmm(value):
    return word(value).hex()


def pattern(size, byte):
    return bytearray((byte + i * 7) & 0xff for i in range(size))


class Case:
    def __init__(self, address, cid, returns):
        self.c = {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {},
                  'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}
        self.raw = {}

    def buf(self, name, size, byte):
        self.raw[name] = pattern(size, byte)

    def put(self, name, offset, data):
        self.raw[name][offset:offset + len(data)] = data

    def ptr(self, name, offset, target):
        if target is None:
            self.put(name, offset, bytes(8))
        else:
            self.c['memory'].append({'addr': f'buf:{name}+{offset}', 'pointer': target})

    def guest(self, name, offset, address):
        self.c['memory'].append({'addr': f'buf:{name}+{offset}', 'guest': address})

    def stub(self, address, ret, argc, **extra):
        entry = {'address': address, 'argc': argc, 'ret': ret}
        entry.update(extra)
        self.c['stubs'].append(entry)

    def done(self):
        self.c['buffers'] = {n: {'size': len(b), 'bytes': bytes(b).hex()} for n, b in self.raw.items()}
        return self.c


def node(kind_byte, kind=1, priority=0.0, state=FIND, notice=1.0, position=(0.0, 0.0, 5.0),
         forward=(0.0, 0.0, -1.0)):
    return {'node_kind': kind_byte, 'kind': kind, 'priority': priority, 'state': state, 'notice': notice,
            'position': position, 'forward': forward}


def perception(k, nodes, current=NORMAL, previous=NORMAL, target_type=7, flags=0, countdown=0.0,
               owner_position=(0.0, 0.0, 0.0)):
    """The perception record, its owner (vtable: position, battle notice, pauses sensing) and
    its target list. A character node is laid out so that its handle at +0x48 is 16-byte
    aligned, a place node so that its handle at +0x60 is (the vector getters use aligned loads)."""
    k.buf('perception', 0xe0, 0x41)
    k.buf('owner', 0x20, 0x43)
    k.buf('owner_vt', 0xb0, 0x45)
    k.ptr('perception', 0x8, 'owner')
    k.ptr('owner', 0x0, 'owner_vt')
    k.guest('owner_vt', 0x18, VECTOR_AT_10)
    k.guest('owner_vt', 0x90, BATTLE_NOTICE)
    k.guest('owner_vt', 0xa8, PAUSES_SENSING)
    k.put('owner', 0x10, vec(*owner_position))
    k.put('perception', 0xb0, word(countdown))
    k.put('perception', 0xb8, struct.pack('<H', flags))
    k.put('perception', 0xc4, struct.pack('<iii', current, previous, target_type))
    k.buf('handle_vt', 0x90, 0x47)
    k.guest('handle_vt', 0x80, VECTOR_AT_10)
    k.guest('handle_vt', 0x88, VECTOR_AT_20)
    previous_buf, offset = 'perception', 0x18
    for n, nd in enumerate(nodes):
        name = f'node{n}'
        shift = 0 if nd['kind'] == 2 else 8
        handle = shift + (0x60 if nd['kind'] == 2 else 0x48)
        k.buf(name, NODE_BUFFER, 0x50 + n * 5)
        k.put(name, shift + 0x2, bytes([nd['node_kind']]))
        k.put(name, shift + 0x20, word(nd['state']))
        k.put(name, shift + 0x28, struct.pack('<i', nd['kind']))
        k.put(name, shift + 0x30, word(nd['priority']))
        k.put(name, shift + 0x38, word(nd['notice']))
        if nd['kind'] in (1, 2):
            k.ptr(name, handle, 'handle_vt')
            k.put(name, handle + 0x10, vec(*nd['position']))
            forward = nd['forward']
            k.put(name, handle + 0x20, vec(*forward) if len(forward) == 4 else vec(*forward, 0.0))
        k.ptr(previous_buf, offset, f'{name}+{shift}' if shift else name)
        previous_buf, offset = name, shift + 0x8
    k.ptr(previous_buf, offset, None)


def update_case(cid, nodes, frame=1 / 30, paused=0, **fields):
    k = Case(UPDATE, cid, 'void')
    perception(k, nodes, **fields)
    k.stub(PAUSES_SENSING, paused, 1, argi=[0])
    k.stub(TARGET_LIST_UPDATE, 0, 1, argi=[0], argf32=[0])
    k.stub(BATTLE_NOTICE, 0, 2, argi=[0, 1])
    k.c['args'] = {'rdi': 'buf:perception', 'xmm0': xmm(frame)}
    return k.done()


def handle_case(cid, nodes):
    k = Case(CURRENT_HANDLE, cid, 'i64')
    perception(k, nodes)
    k.c['args'] = {'rdi': 'buf:perception'}
    return k.done()


# Node lists by kind byte; every list with entries has one above priority -1 (else both sides
# read address 0x20).
KIND_SETS = [[], [1], [4], [5], [3], [11], [2], [7, 2], [1, 4], [4, 1], [5, 1], [1, 5], [3, 1], [1, 3],
             [11, 3], [3, 11], [1, 11], [11], [0x81], [0x84], [0x85, 4], [4, 5], [5, 4], [2, 3], [2, 11, 1],
             [1, 1, 1], [6, 8, 9], [0x8b], [0x83, 1], [9, 4, 1, 5]]
HANDLE_KINDS = [1, 2, 0, 3, -1]


def kind_set_nodes(rng, kinds, state=None):
    nodes = []
    for kb in kinds:
        nodes.append(node(kb, kind=rng.choice(HANDLE_KINDS), priority=rng.choice([0.0, 2.5, 7.0, -0.5]),
                          state=rng.choice(STATES) if state is None else state,
                          notice=rng.choice([1.0, 0.0, -1.0, NAN_A, 1e-30])))
    return nodes


def handle_cases():
    rng = random.Random(0x16acfa0)
    n = 0
    for kinds in KIND_SETS:
        for _ in range(4):
            yield handle_case(f'kinds_{n}', kind_set_nodes(rng, kinds))
            n += 1
    # The main entry is the highest priority above -1, first of equals; -1 and NaN never win.
    for priorities in [(1.0, 2.0), (2.0, 1.0), (3.0, 3.0), (-1.0, 0.0), (NAN_A, 0.5), (0.5, NAN_B),
                       (-0.9999999, -1.0), (INF, 1e30)]:
        for kinds in [(1, 2), (2, 1), (1, 0)]:
            nodes = [node(1, kind=kd, priority=p) for kd, p in zip(kinds, priorities)]
            yield handle_case(f'priority_{n}', nodes)
            n += 1
    # Entries, none above -1: the result is null (no crash here).
    yield handle_case(f'no_main_{n}', [node(1, priority=-1.0), node(3, kind=1)])


def update_cases():
    rng = random.Random(0x16a85c0)
    n = 0
    # Which node sets the state and type, from every starting state.
    for kinds in KIND_SETS:
        for current, previous in [(0, 0), (3, 1), (2, 2), (1, 3), (4, 0), (-1, 7)]:
            yield update_case(f'kinds_{n}', kind_set_nodes(rng, kinds), current=current, previous=previous,
                              target_type=rng.choice([0, 3, 5, -1]))
            n += 1
    # The main entry's own state, including values outside 0..3, which leave both states alone.
    for state, current in itertools.product(STATES + [-2147483648, 5], [0, 1, 2, 3, 9]):
        yield update_case(f'entry_state_{n}', [node(1, priority=1.0, state=state)], current=current, previous=current)
        n += 1
    # The battle notice: on a change, with the main entry in battle and its value above 0, and a
    # current target that has a handle (a kind-4/5 node's or the main entry's).
    for notice, kinds, handle_kind, current in itertools.product(
            [1.0, 0.0, -0.0, -1.0, NAN_A, INF, 1e-38], [(1,), (4, 1), (5, 1), (3, 1), (11, 1), (4,)],
            [1, 2, 0], [0, 3]):
        nodes = [node(kb, kind=handle_kind, priority=1.0, state=BATTLE, notice=notice) for kb in kinds]
        yield update_case(f'notice_{n}', nodes, current=current, previous=current)
        n += 1
    for state in [FIND, CAUTION, NORMAL, 4]:
        yield update_case(f'notice_state_{n}', [node(4, kind=1), node(1, priority=0.0, state=state)])
        n += 1
    # The countdown runs only while positive; the frame time may be anything.
    for countdown, frame in itertools.product([1.0, 0.0, -0.0, -2.0, 1e-30, INF, NAN_A, 0.01],
                                              [1 / 30, 0.0, -1.0, NAN_B, INF, 2.0]):
        yield update_case(f'countdown_{n}', [], countdown=countdown, frame=frame)
        n += 1
    # The owner pausing sensing sets the target-list update's skip flag.
    for paused in (0, 1, 0x100, 0xff):
        yield update_case(f'paused_{n}', [node(1)], paused=paused)
        n += 1
    # Flags: the facing answer is cleared each think; force battle sets the main entry's state.
    for flags, kinds in itertools.product([0, 0x400, 0x8000, 0x8400, 0x7bff, 0xffff, 0x0001, 0x8000 | 0x2],
                                          [(), (1,), (4, 1), (3,), (1, 1)]):
        nodes = [node(kb, priority=float(i), state=CAUTION) for i, kb in enumerate(kinds)]
        yield update_case(f'flags_{n}', nodes, flags=flags)
        n += 1
    yield from facing_cases(rng, n)


def facing_cases(rng, n):
    """The facing check: the main target's forward within 45 degrees of the line to the owner."""
    s = 0.70710677
    edge = [(0.0, 0.0, -1.0), (s, 0.0, -s), (-s, 0.0, -s), (0.7071068, 0.0, -0.7071068), (0.70710683, 0.0, -0.7071067),
            (0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 0.0), (NAN_A, 0.0, -1.0),
            (INF, 0.0, -1.0), (0.0, -0.7, -0.7), (1e-30, 0.0, -1e-30), (3.0, 0.0, -3.0000002)]
    positions = [(0.0, 0.0, 5.0), (0.0, 0.0, 0.0), (0.0, 3.0, 5.0), (INF, 0.0, 0.0), (NAN_B, 0.0, 1.0),
                 (1e20, 0.0, 1e20)]
    for forward, position, kind in itertools.product(edge, positions[:3], (1, 2)):
        yield update_case(f'facing_{n}', [node(1, kind=kind, priority=1.0, position=position, forward=forward)],
                          flags=0x800)
        n += 1
    for position in positions[3:]:
        yield update_case(f'facing_{n}', [node(1, priority=1.0, position=position)], flags=0x800)
        n += 1
    for _ in range(120):
        own = tuple(rng.uniform(-30, 30) for _ in range(3))
        nodes = [node(1, kind=rng.choice([1, 2]), priority=rng.uniform(-0.5, 9.0), state=rng.choice(STATES),
                      position=tuple(rng.uniform(-30, 30) for _ in range(3)),
                      forward=(rng.uniform(-1, 1), rng.uniform(-0.2, 0.2), rng.uniform(-1, 1)))
                 for _ in range(rng.randint(1, 3))]
        yield update_case(f'facing_random_{n}', nodes, flags=rng.choice([0x800, 0x8800, 0xc00, 0x800 | 0x7]),
                          owner_position=own, current=rng.choice([0, 1, 2, 3]), previous=rng.choice([0, 3]))
        n += 1
    # The dot product takes all four lanes: positions and facings whose w lanes are not 1 and 0.
    for position, forward, own in [((0.0, 0.0, 5.0, 0.5), (0.0, 0.0, -1.0), (0.0, 0.0, 0.0)),
                                   ((0.0, 0.0, 5.0), (0.0, 0.0, -1.0, 2.0), (0.0, 0.0, 0.0)),
                                   ((3.0, 0.0, 5.0), (-0.5, 0.0, -1.0, -3.0), (0.0, 0.0, 0.0, 4.0)),
                                   ((1.0, 0.0, 1.0, 0.0), (0.2, 0.0, -1.0, 0.9), (0.0, 0.0, 0.0, 1.0)),
                                   ((0.0, 0.0, 2.0), (1.0, 0.0, -0.3, -1.0), (0.0, 0.0, 0.0)),
                                   ((0.0, 0.0, 2.0, -1.0), (1.2, 0.0, -1.0, 1.0), (0.0, 0.0, 0.0))]:
        for kind in (1, 2):
            yield update_case(f'facing_w_{n}', [node(1, kind=kind, priority=1.0, position=position, forward=forward)],
                              flags=0x800, owner_position=own)
            n += 1
    # No main entry, or one whose handle kind is neither character nor place: no check.
    for kinds in [(), (4,), (3,)]:
        yield update_case(f'facing_none_{n}', [node(kb) for kb in kinds], flags=0x800)
        n += 1
    for kind in (0, 3):
        yield update_case(f'facing_none_{n}', [node(1, kind=kind, priority=1.0)], flags=0xc00)
        n += 1


GENERATORS = [update_cases, handle_cases]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    count = 0
    for generate in GENERATORS:
        for case in generate():
            directory = out / case['address']
            directory.mkdir(parents=True, exist_ok=True)
            (directory / f"{case['id']}.json").write_text(json.dumps(case, indent=1) + '\n')
            count += 1
    print(f'{count} cases')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
