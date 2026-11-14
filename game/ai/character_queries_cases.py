#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the per-frame AI character queries in character_queries.cpp (invented
data, not game recordings): character views, effect lists, link ids, pending events and the
target list's main-target matching.

usage: character_queries_cases.py OUT_DIR

Writes cases for every function; verify.py keeps the ones whose address matches the function.
"""
import itertools
import json
from pathlib import Path
import struct
import sys

MASK64 = (1 << 64) - 1
# Game addresses the fake vtables point at; the cases stub them, so they never run.
VIEW_CHARACTER = '0x01624220'
CHARACTER_ID = '0x01624250'
QUERY_1A8 = '0x01624290'
QUERY_1C0 = '0x016242c0'
LINKED = '0x01624340'
HANDLE_ID = '0x01624370'
FIND_CHARACTER = '0x017c9930'
POST_EVENT = '0x0173a1f0'
LOOKUP_TABLE = '0x0593b0c8'

CHARACTER_SIZE = 0x3b8
VTABLE_SIZE = 0x1c8
NAN_A, NAN_B = 0x7fc12345, 0xffc54321


def f32(value):
    return struct.pack('<f', value)


def bits(word):
    return struct.pack('<I', word & 0xffffffff)


def i32(value):
    return struct.pack('<i', value)


def pattern(size, byte):
    return bytearray((byte + i * 7) & 0xff for i in range(size))


def base(address, cid, returns):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


class Case:
    """A case under construction: buffers as bytearrays, pointers as memory presets."""

    def __init__(self, address, cid, returns):
        self.c = base(address, cid, returns)
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


# ---- 0x016243a0: the transform's third column, negated ----

COLUMN_VALUES = [(1.0, 2.0, 3.0), (0.0, -0.0, 0.0), (-1.5, 1e30, -1e-40),
                 (float('inf'), float('-inf'), 0.25)]


def negated_column_cases():
    for n, values in enumerate(COLUMN_VALUES + ['nan']):
        k = Case('0x016243a0', f'column_{n}', 'vec')
        k.buf('view', 0x18)
        k.buf('chr', CHARACTER_SIZE, 0x23 + n)
        if values == 'nan':
            for row, word in enumerate((NAN_A, NAN_B, 0x7f800001)):
                k.put('chr', 0x90 + row * 16 + 8, bits(word))
        else:
            for row, v in enumerate(values):
                k.put('chr', 0x90 + row * 16 + 8, f32(v))
        k.ptr('view', 0x10, 'chr')
        k.c['args'] = {'rdi': 'buf:view'}
        yield k.done()
    k = Case('0x016243a0', 'no_character', 'vec')
    k.buf('view', 0x18)
    k.ptr('view', 0x10, None)
    k.c['args'] = {'rdi': 'buf:view'}
    yield k.done()


# ---- 0x01624760: the character's id ----

def view_with_vtable(k):
    k.buf('view', 0x18)
    k.buf('view_vt', 0x40, 0x31)
    k.ptr('view', 0, 'view_vt')
    k.guest('view_vt', 0x30, VIEW_CHARACTER)
    k.c['args'] = {'rdi': 'buf:view'}


def character_id_cases():
    for n, ident in enumerate([0, 1, -1, 0x7fffffff, -0x80000000, 2410010]):
        k = Case('0x01624760', f'id_{n}', 'i32')
        view_with_vtable(k)
        k.buf('chr', 0x10)
        k.buf('chr_vt', 0x90, 0x41)
        k.ptr('chr', 0, 'chr_vt')
        k.guest('chr_vt', 0x88, CHARACTER_ID)
        k.stub(VIEW_CHARACTER, 'buf:chr')
        k.stub(CHARACTER_ID, ident & MASK64)
        yield k.done()
    k = Case('0x01624760', 'no_character', 'i32')
    view_with_vtable(k)
    k.stub(VIEW_CHARACTER, 0)
    yield k.done()


# ---- 0x01624e50: the flag module ----

def flag_module_cases():
    for n, (enabled, f37f, f380) in enumerate(itertools.product((0, 1, 0x80), (0, 1, 0xff), (0, 2))):
        k = Case('0x01624e50', f'flags_{n}', 'i8')
        view_with_vtable(k)
        k.buf('chr', CHARACTER_SIZE, 0x13)
        k.buf('mods', 0x80, 0x15)
        k.buf('mod08', 0x10, 0x17)
        k.buf('owner', CHARACTER_SIZE, 0x19)
        k.buf('owner_mods', 0x80, 0x1b)
        k.buf('flags', 0x388, 0x1d)
        k.ptr('chr', 0x3b0, 'mods')
        k.ptr('mods', 0x08, 'mod08')
        k.ptr('mod08', 0x08, 'owner')
        k.ptr('owner', 0x3b0, 'owner_mods')
        k.ptr('owner_mods', 0x30, 'flags')
        k.put('flags', 0x37f, bytes([f37f, f380]))
        k.put('flags', 0x384, bytes([enabled]))
        k.stub(VIEW_CHARACTER, 'buf:chr')
        k.stub(VIEW_CHARACTER, 'buf:chr')
        yield k.done()
    k = Case('0x01624e50', 'no_flag_module', 'i8')
    view_with_vtable(k)
    k.buf('chr', CHARACTER_SIZE)
    k.buf('mods', 0x80)
    k.buf('mod08', 0x10)
    k.buf('owner', CHARACTER_SIZE)
    k.buf('owner_mods', 0x80)
    k.ptr('chr', 0x3b0, 'mods')
    k.ptr('mods', 0x08, 'mod08')
    k.ptr('mod08', 0x08, 'owner')
    k.ptr('owner', 0x3b0, 'owner_mods')
    k.ptr('owner_mods', 0x30, None)
    k.stub(VIEW_CHARACTER, 'buf:chr')
    k.stub(VIEW_CHARACTER, 'buf:chr')
    yield k.done()
    k = Case('0x01624e50', 'no_character', 'i8')
    view_with_vtable(k)
    k.stub(VIEW_CHARACTER, 0)
    yield k.done()


# ---- 0x0169acf0 / 0x0169ad80: the effect list ----

# Each entry: (entry flags, row present, byte 0x162, byte 0x163).
EFFECT_LISTS = [
    [],
    [(0, True, 0x40, 0x00)],
    [(0, True, 0x80, 0x00)],
    [(0, True, 0x00, 0x01)],
    [(0, True, 0x3f, 0xfe)],
    [(1, True, 0xff, 0xff), (0, True, 0x00, 0x00)],
    [(0x80000000, True, 0xc0, 0x01), (0x40000, True, 0xc0, 0x01), (2, True, 0xc0, 0x01)],
    [(0, False, 0, 0), (0, True, 0x40, 0x00)],
    [(0x7ff3fffc, True, 0x00, 0x00), (0, True, 0x80, 0x01), (0, False, 0, 0)],
    [(0, True, 0x00, 0x00), (0, True, 0x00, 0x00), (0, True, 0xc0, 0x01), (0, True, 0x40, 0x00)],
]


def effect_view(k, entries, no_character=False):
    k.buf('view', 0x18)
    k.c['args'] = {'rdi': 'buf:view'}
    if no_character:
        k.ptr('view', 0x10, None)
        return
    k.buf('chr', CHARACTER_SIZE, 0x27)
    k.buf('list', 0x10, 0x29)
    k.ptr('view', 0x10, 'chr')
    k.ptr('chr', 0x1c8, 'list')
    previous = ('list', 0x8)
    for n, (flags, has_row, b162, b163) in enumerate(entries):
        name = f'entry{n}'
        k.buf(name, 0x60, 0x31 + n)
        k.put(name, 0x1c, struct.pack('<I', flags))
        if has_row:
            k.buf(f'row{n}', 0x168, 0x51 + n)
            k.put(f'row{n}', 0x162, bytes([b162, b163]))
            k.ptr(name, 0x48, f'row{n}')
        else:
            k.ptr(name, 0x48, None)
        k.ptr(previous[0], previous[1], name)
        previous = (name, 0x58)
    k.ptr(previous[0], previous[1], None)


def effect_state_cases():
    for n, entries in enumerate(EFFECT_LISTS):
        for state in (0x27, 0xb0, 0xb1, 0, 0x28, 0xffffffff, 0x1000000b1):
            k = Case('0x0169acf0', f'list_{n}_state_{state:x}', 'i8')
            effect_view(k, entries)
            k.c['args']['rsi'] = state
            yield k.done()
    k = Case('0x0169acf0', 'no_character', 'i8')
    effect_view(k, [], no_character=True)
    k.c['args']['rsi'] = 0x27
    yield k.done()


def gather_cases():
    for n, entries in enumerate(EFFECT_LISTS):
        for start in (b'\0\0\0', b'\1\1\1', b'\xfe\x80\x02'):
            k = Case('0x0169ad80', f'list_{n}_start_{start.hex()}', 'void')
            effect_view(k, entries)
            k.buf('out', 4, 0)
            k.put('out', 0, start + b'\x5a')
            k.c['args']['rsi'] = 'buf:out'
            yield k.done()
    k = Case('0x0169ad80', 'no_character', 'void')
    effect_view(k, [], no_character=True)
    k.buf('out', 4, 0x66)
    k.c['args']['rsi'] = 'buf:out'
    yield k.done()


# ---- 0x0169af00: the ready check ----

def ready_cases():
    combos = itertools.product((0, 1), (0, 1), (None, 0, 1, 2, -1), (0xff, 0, 0xfe))
    for n, (a8, c0, mode, slot) in enumerate(combos):
        k = Case('0x0169af00', f'ready_{n}', 'i8')
        k.buf('view', 0x18)
        k.buf('chr', CHARACTER_SIZE, 0x21)
        k.buf('mods', 0x80, 0x23)
        k.buf('mod78', 0x10, 0x25)
        k.buf('subj', CHARACTER_SIZE, 0x27)
        k.buf('subj_vt', VTABLE_SIZE, 0x29)
        k.buf('subj_mods', 0x80, 0x2b)
        k.buf('mode_mod', 0x30, 0x2d)
        k.buf('ai_block', 0x40, 0x2f)
        k.buf('ai_state', 0x88, 0x31)
        k.ptr('view', 0x10, 'chr')
        k.ptr('chr', 0x3b0, 'mods')
        k.ptr('mods', 0x78, 'mod78')
        k.ptr('mod78', 0x8, 'subj')
        k.ptr('subj', 0, 'subj_vt')
        k.guest('subj_vt', 0x1a8, QUERY_1A8)
        k.guest('subj_vt', 0x1c0, QUERY_1C0)
        k.ptr('subj', 0x3b0, 'subj_mods')
        k.ptr('subj_mods', 0x58, 'mode_mod')
        if mode is None:
            k.ptr('mode_mod', 0x28, None)
        else:
            k.buf('mode', 4)
            k.put('mode', 0, i32(mode))
            k.ptr('mode_mod', 0x28, 'mode')
        k.ptr('subj', 0x58, 'ai_block')
        k.ptr('ai_block', 0x38, 'ai_state')
        k.put('ai_state', 0x60, bytes([slot]))
        k.stub(QUERY_1A8, a8 | 0x100)   # only al counts
        if a8:
            k.stub(QUERY_1C0, c0 | 0xff00)
        k.c['args'] = {'rdi': 'buf:view'}
        yield k.done()
    k = Case('0x0169af00', 'no_character', 'i8')
    k.buf('view', 0x18)
    k.ptr('view', 0x10, None)
    k.c['args'] = {'rdi': 'buf:view'}
    yield k.done()


# ---- 0x0169fad0: link ids ----

def link_cases():
    variants = [('none',)] + [('link', None)] + [
        ('full', active, blocked, ids)
        for active, blocked in itertools.product((0, 1, 0x80), (0, 1))
        for ids in ((5, 6), (-1, 7), (0x7fffffff, -0x80000000))]
    for n, v in enumerate(variants):
        k = Case('0x0169fad0', f'link_{n}', 'i64')
        k.buf('out', 8, 0x77)
        k.buf('src', CHARACTER_SIZE, 0x21)
        k.buf('src_vt', VTABLE_SIZE, 0x23)
        k.ptr('src', 0, 'src_vt')
        k.guest('src_vt', 0x108, LINKED)
        k.c['args'] = {'rdi': 'buf:out', 'rsi': 'buf:src'}
        if v[0] == 'none':
            k.stub(LINKED, 0)
            yield k.done()
            continue
        k.buf('linked', 0x200, 0x25)
        k.stub(LINKED, 'buf:linked')
        if v[0] == 'link':
            k.ptr('linked', 0x1f8, None)
            yield k.done()
            continue
        _, active, blocked, (first, second) = v
        k.buf('link', 0x38, 0x27)
        k.buf('partner', CHARACTER_SIZE, 0x29)
        k.buf('partner_mods', 0x80, 0x2b)
        k.buf('link_mod', 0x328, 0x2d)
        k.ptr('linked', 0x1f8, 'link')
        k.put('link', 0x18, i32(first) + i32(second))
        k.ptr('link', 0x30, 'partner')
        k.ptr('partner', 0x3b0, 'partner_mods')
        k.ptr('partner_mods', 0x68, 'link_mod')
        k.put('link_mod', 0x1b8, bytes([blocked]))
        k.put('link_mod', 0x321, bytes([active]))
        yield k.done()


# ---- 0x01708410: pending event ----

def pending_event_cases():
    variants = [('no_table',), ('not_found',)] + [
        ('found', pending, event) for pending, event in ((7, 7), (7, 8), (-1, -1), (-1, 3), (0, 0))]
    for n, v in enumerate(variants):
        for value in (0, 0x1234, -5):
            k = Case('0x01708410', f'event_{n}_value_{value & 0xffff:x}', 'i8')
            k.buf('event', 0x30, 0x41)
            k.put('event', 0x10, i32(value))
            k.put('event', 0x20, i32(2410010) + i32(v[2] if v[0] == 'found' else 9) + i32(3))
            k.buf('receiver', 0x20, 0x43)
            k.buf('table', 0x10, 0x45)
            k.c['args'] = {'rdi': 'buf:event', 'rsi': 'buf:receiver'}
            if v[0] == 'no_table':
                k.c['memory'].append({'addr': LOOKUP_TABLE, 'bytes': bytes(8).hex()})
                yield k.done()
                continue
            k.c['memory'].append({'addr': LOOKUP_TABLE, 'pointer': 'table'})
            find = {'argc': 2, 'argu32': [0], 'argi': [1]}
            if v[0] == 'not_found':
                k.stub(FIND_CHARACTER, 0, **find)
                yield k.done()
                continue
            k.buf('chr', CHARACTER_SIZE, 0x47)
            k.buf('mods', 0x80, 0x49)
            k.buf('mode_mod', 0x30, 0x4b)
            k.buf('owner', CHARACTER_SIZE, 0x4d)
            k.buf('ai_block', 0x40, 0x4f)
            k.buf('ai_state', 0x88, 0x51)
            k.ptr('chr', 0x3b0, 'mods')
            k.ptr('mods', 0x58, 'mode_mod')
            k.ptr('mode_mod', 0x8, 'owner')
            k.ptr('owner', 0x58, 'ai_block')
            k.ptr('ai_block', 0x38, 'ai_state')
            k.put('ai_state', 0x84, i32(v[1]))
            k.stub(FIND_CHARACTER, 'buf:chr', **find)
            if v[1] == v[2]:
                k.stub(POST_EVENT, 0, argc=5, argi=[0], argu32=[1, 2, 3, 4])
            yield k.done()


# ---- 0x016b30a0: target list ----

def is_entry(node):
    return node['node_kind'] & 0x7f == 1


def main_of(nodes):
    best, best_priority = None, -1.0
    for node in nodes:
        if is_entry(node) and node['priority'] > best_priority:
            best, best_priority = node, node['priority']
    return best


def handle_calls(nodes):
    """The handle ids the original asks for, in order (the cases' stub sequence)."""
    calls = []
    for node in nodes:
        if not is_entry(node):
            continue
        main = main_of(nodes)
        if node['kind'] != main['kind'] or node['kind'] != 1:
            continue
        calls.append(main['id'])
        if main['id'] != -1:
            calls.append(node['id'])
    return calls


def node(node_kind=1, kind=1, priority=0.0, ident=1, place=(0.0, 0.0, 0.0), valid=1, matches=0, timer=0.0):
    return {'node_kind': node_kind, 'kind': kind, 'priority': priority, 'id': ident, 'place': place,
            'valid': valid, 'matches': matches, 'timer': timer}


TARGET_LISTS = [
    [],
    [node(priority=1.0)],
    [node(priority=1.0, ident=5), node(priority=0.5, ident=5), node(priority=0.2, ident=6)],
    [node(priority=1.0, ident=-1), node(priority=0.5, ident=-1)],
    [node(node_kind=0x81, priority=2.0, ident=4, timer=-1.0), node(ident=4, matches=1, timer=3.0),
     node(ident=4, timer=float('nan')), node(node_kind=3, priority=9.0)],
    [node(kind=2, priority=1.0, place=(1.0, 2.0, 3.0)), node(kind=2, place=(1.2, 2.2, 3.2)),
     node(kind=2, place=(1.5, 2.0, 3.0)), node(kind=2, place=(1.0, 2.0, 3.4999)), node(kind=2, valid=0)],
    [node(kind=2, priority=1.0, valid=0, place=(0, 0, 0)), node(kind=2, place=(0, 0, 0))],
    [node(kind=2, priority=3.0, place=(5, 5, 5)), node(kind=1, priority=1.0, ident=2), node(kind=1, ident=2)],
    [node(kind=3, priority=1.0), node(kind=3, matches=1), node(kind=3)],
    [node(priority=1.0, ident=8), node(priority=1.0, ident=9), node(priority=float('nan'), ident=8, timer=-0.0)],
    [node(kind=2, priority=0.0, place=(float('nan'), 0, 0)), node(kind=2, place=(0, 0, 0), matches=1, timer=1.0)],
    [node(priority=-0.5, ident=3, timer=8.5), node(ident=3, timer=0.0), node(ident=3, matches=1, timer=-2.0)],
    # Places differing in one lane only, so a distance that drops y or z (or adds w) is caught:
    # 0.6 m away in y or in z is another place, 0.4 m is the same; the w lanes never count.
    [node(kind=2, priority=1.0, place=(1.0, 2.0, 3.0)), node(kind=2, place=(1.0, 2.6, 3.0)),
     node(kind=2, place=(1.0, 2.0, 3.6)), node(kind=2, place=(1.0, 2.4, 3.0)),
     node(kind=2, place=(1.0, 2.0, 2.6)), node(kind=2, place=(1.0, 1.4, 3.0), matches=1)],
    [node(kind=2, priority=1.0, place=(0.0, 0.0, 0.0, 5.0)), node(kind=2, place=(0.0, 0.0, 0.0, -7.0)),
     node(kind=2, place=(0.1, 0.0, 0.0, 0.0), matches=1), node(kind=2, place=(0.0, 0.0, 0.0, 0.3))],
    # Just inside half a metre: squared distance one step below 0.25 (as floats), so the limit must
    # be exactly 0.25; the mirrored entry is already marked.
    [node(kind=2, priority=1.0, place=(0.0, 0.0, 0.0)),
     node(kind=2, place=(0.2723987102508545, 0.35387587547302246, 0.22487951815128326)),
     node(kind=2, place=(-0.2723987102508545, 0.35387587547302246, 0.22487951815128326), matches=1)],
    # A place not yet valid never matches, even 0.1 m from the main target.
    [node(kind=2, priority=1.0, place=(0, 0, 0)), node(kind=2, valid=0, place=(0.1, 0, 0))],
    # Node kind 0x41 is not a target entry (only the low seven bits are compared, against 1).
    [node(priority=1.0, ident=2), node(node_kind=0x41, priority=5.0, ident=3), node(ident=3)],
    # Just under half a metre, where (x+z)+y or x+(y+z) rounds differently from (x+y)+z.
    [node(kind=2, priority=1.0, place=(0.0, 0.0, 0.0)),
     node(kind=2, place=(0.2291375697, 0.3826622963, 0.2259768248)),
     node(kind=2, place=(0.33681029081344604, 0.33434823155403137, 0.15738511085510254))],
    # Each lane alone is within half a metre, together they are not (0.27 squared).
    [node(kind=2, priority=1.0, place=(0.0, 0.0, 0.0)), node(kind=2, place=(0.3, 0.3, 0.3), matches=1),
     node(kind=2, place=(0.0, 0.3, 0.3)), node(kind=2, place=(0.3, 0.0, 0.3)), node(kind=2, place=(0.3, 0.3, 0.0))],
]


def target_list_cases():
    for n, nodes in enumerate(TARGET_LISTS):
        if any(is_entry(x) for x in nodes) and main_of(nodes) is None:
            continue   # the original reads through a null pointer there
        k = Case('0x016b30a0', f'targets_{n}', 'void')
        k.buf('list', 0x10, 0x61)
        k.buf('handle_vt', 0xb0, 0x63)
        k.guest('handle_vt', 0xa8, HANDLE_ID)
        previous = ('list', 0x8)
        for i, x in enumerate(nodes):
            name = f'node{i}'
            k.buf(name, 0x90, 0x71 + i)
            k.put(name, 0x2, bytes([x['node_kind']]))
            k.put(name, 0x28, i32(x['kind']))
            k.put(name, 0x30, f32(x['priority']) + f32(x['timer']))
            k.put(name, 0x41, bytes([x['matches']]))
            k.ptr(name, 0x48, 'handle_vt')
            place = tuple(x['place']) + (1.0,) * (4 - len(x['place']))   # w is 1 unless given
            k.put(name, 0x70, b''.join(f32(v) for v in place))
            k.put(name, 0x80, bytes([x['valid']]))
            k.ptr(previous[0], previous[1], name)
            previous = (name, 0x8)
        k.ptr(previous[0], previous[1], None)
        for ident in handle_calls(nodes):
            k.stub(HANDLE_ID, ident & MASK64)
        k.c['args'] = {'rdi': 'buf:list'}
        yield k.done()


GENERATORS = [negated_column_cases, character_id_cases, flag_module_cases, effect_state_cases,
              gather_cases, ready_cases, link_cases, pending_event_cases, target_list_cases]


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
