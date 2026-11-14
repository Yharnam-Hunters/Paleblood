#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the generator's calls in game/frame_timing/part_0x10_update.cpp, for
tools/verify.py run.

usage: part_0x10_generator_cases.py OUT_DIR size|fill|refresh

Every case lays out an invented generator: its bindings, each with a binding set (nodes and
groups) and two tables of record targets, the groups' node ids and record lists, all filled with
patterns so a field read or written by mistake shows. The record targets share a fake vtable
whose slots 5 and 7 are scripted stubs; the layout (0x00b685f0) and the fill proper (0x00b694d0)
are scripted too, each callee with a spare scripted call so an unexpected call is recorded.
- size (frame_timing_generator_buffer_size_00b68770): the layout writes sizes 0, small, large and
  0xffffffff (and a flag), for entry counts 1, 7, 0, -1 and the largest.
- fill (frame_timing_generator_fill_00b69450): no bindings, a negative count, one and three
  bindings sharing or not sharing a set; nodes of kind 1 (cleared), 0, 2, -1 and 0x101, listed
  once or twice; the arguments passed on to the fill proper, the seconds with junk upper lanes.
- refresh (frame_timing_generator_refresh_00b6a030): short and long records in either list or
  both, empty lists, negative counts, records naming the same target twice, three bindings, and
  a target living inside its group whose call shrinks the group's short count, and one living inside the generator whose
  call drops the binding count (both read again).
No game data is used.
"""
import json
import os
import struct
import sys

ADDRESSES = {'size': '0x00b68770', 'fill': '0x00b69450', 'refresh': '0x00b6a030'}
RETURNS = {'size': 'i32', 'fill': 'void', 'refresh': 'void'}
LAYOUT, FILL_ENTRIES = '0x00b685f0', '0x00b694d0'
TAKE_LONG, TAKE_SHORT = '0x00500500', '0x00500700'   # fake vtable targets (stubbed)
GUARD_GOT = '0x057e3be0'
JUNK_LANES = struct.pack('<3I', 0x7fa11111, 0xdeadbeef, 0xff812345)


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


class Case:
    def __init__(self, mode, cid):
        self.c = {'schema': 1, 'address': ADDRESSES[mode], 'id': cid, 'returns': RETURNS[mode],
                  'args': {'rdi': 'buf:generator'}, 'buffers': {}, 'memory': [], 'stubs': [], 'imports': []}
        self.buf('generator', 0x20, 0x5a)
        self.buf('guard', 0x10, 0x5a)
        self.mem(GUARD_GOT, ptr='guard', raw=True)
        self.buf('target_vtable', 0x40, 0x6c)
        self.mem('target_vtable+0x28', guest=TAKE_LONG)
        self.mem('target_vtable+0x38', guest=TAKE_SHORT)

    def buf(self, name, size, fill):
        self.c['buffers'][name] = {'size': size, 'bytes': f'{fill:02x}' * size}

    def mem(self, at, ptr=None, raw=False, bytes_=None, guest=None):
        addr = at if raw else f'buf:{at}'
        if ptr is not None:
            self.c['memory'].append({'addr': addr, 'pointer': ptr})
        elif guest is not None:
            self.c['memory'].append({'addr': addr, 'guest': guest})
        else:
            self.c['memory'].append({'addr': addr, 'bytes': bytes_})

    def generator(self, bindings, count=None):
        """bindings: a list of (set name, group index, short targets, long targets)."""
        self.buf('bindings', max(8, 8 * len(bindings)), 0x77)
        self.mem('generator+0x10', ptr='bindings')
        self.mem('generator+0x18', bytes_=d(len(bindings) if count is None else count))
        for k, (set_name, group, shorts, longs) in enumerate(bindings):
            b = f'binding{k}'
            self.buf(b, 0x48, 0x3c)
            self.mem(f'bindings+{8 * k:#x}', ptr=b)
            self.mem(f'{b}+0x10', ptr=set_name)
            self.mem(f'{b}+0x18', bytes_=d(group))
            for table, offset, targets in (('short', 0x30, shorts), ('long', 0x40, longs)):
                t = f'{b}_{table}'
                self.buf(t, max(8, 8 * len(targets)), 0x99)
                self.mem(f'{b}+{offset:#x}', ptr=t)
                for i, target in enumerate(targets):
                    self.mem(f'{t}+{8 * i:#x}', ptr=target)

    def binding_set(self, name, groups, nodes=()):
        """groups: names of group buffers; nodes: (kind) per node."""
        self.buf(name, 0x60, 0xc6)
        self.buf(f'{name}_groups', max(8, 8 * len(groups)), 0x88)
        self.buf(f'{name}_nodes', max(8, 8 * len(nodes)), 0x88)
        self.mem(f'{name}+0x48', ptr=f'{name}_nodes')
        self.mem(f'{name}+0x58', ptr=f'{name}_groups')
        for i, g in enumerate(groups):
            self.mem(f'{name}_groups+{8 * i:#x}', ptr=g)
        for i, kind in enumerate(nodes):
            n = f'{name}_node{i}'
            self.buf(n, 0x40, 0x5a)
            self.mem(f'{name}_nodes+{8 * i:#x}', ptr=n)
            self.mem(f'{n}+0x18', bytes_=d(kind))

    def group(self, name, ids=(), shorts=(), longs=(), counts=None, size=0x48):
        """shorts / longs: the target index of each record; counts overrides the three counts."""
        self.buf(name, size, 0xe1)
        counts = counts or (len(ids), len(shorts), len(longs))
        for offset, count in zip((0x20, 0x30, 0x40), counts):
            self.mem(f'{name}+{offset:#x}', bytes_=d(count))
        self.buf(f'{name}_ids', max(4, 4 * len(ids)), 0xab)
        self.mem(f'{name}+0x18', ptr=f'{name}_ids')
        for i, node in enumerate(ids):
            self.mem(f'{name}_ids+{4 * i:#x}', bytes_=d(node))
        for kind, offset, stride, records in (('shorts', 0x28, 0x10, shorts), ('longs', 0x38, 0x20, longs)):
            r = f'{name}_{kind}'
            self.c['buffers'][r] = {'size': max(stride, stride * len(records)),
                                    'bytes': bytes((i * 7 + stride) & 0xff for i in range(max(stride, stride * len(records)))).hex()}
            self.mem(f'{name}+{offset:#x}', ptr=r)
            for i, index in enumerate(records):
                self.mem(f'{r}+{stride * i:#x}', bytes_=d(index))

    def target(self, name, inside=None):
        if inside:
            self.mem(inside, ptr='target_vtable')
        else:
            self.buf(name, 0x10, 0x42)
            self.mem(name, ptr='target_vtable')

    def done(self, spares):
        self.c['stubs'] += [{'address': a, 'argc': n} for a, n in spares]
        return self.c


def size_cases():
    out = []
    for cid, entries, size, flag in (('one_entry', 1, 0x40, 1), ('seven_entries', 7, 0x1c0, 0),
                                     ('zero_entries', 0, 0, 1), ('negative_entries', -1, 0x10, 0xff),
                                     ('largest_entries', 0x7fffffff, 0xfffffff0, 2), ('all_bits_size', 3, 0xffffffff, 0)):
        c = Case('size', cid)
        c.c['args']['rsi'] = hex(entries & 0xffffffff)
        c.c['stubs'].append({'address': LAYOUT, 'argc': 4, 'argi': [0], 'argu32': [1],
                             'writes': [{'arg': 3, 'offset': 0, 'bytes': d(size)},
                                        {'arg': 2, 'offset': 0, 'bytes': f'{flag:02x}'}]})
        out.append(c.done([(LAYOUT, 4)]))
    return out


def fill_case(cid, bindings, sets, groups, count=None, entries=5, seconds=1 / 30):
    c = Case('fill', cid)
    for name, args in sets.items():
        c.binding_set(name, *args)
    for name, ids in groups.items():
        c.group(name, ids=ids)
    c.generator(bindings, count)
    c.buf('target', 0x10, 0x42)
    c.buf('buffer', 0x40, 0x24)
    c.c['args'].update({'rsi': 'buf:target', 'rdx': hex(entries & 0xffffffff), 'rcx': 'buf:buffer',
                        'xmm0': (struct.pack('<f', seconds) + JUNK_LANES).hex()})
    c.c['stubs'].append({'address': FILL_ENTRIES, 'argc': 4, 'argi': [0, 1, 3], 'argu32': [2], 'argf32': [0]})
    return c.done([(FILL_ENTRIES, 4)])


def fill_cases():
    kinds = (1, 0, 2, -1, 0x101, 1)
    one = [('set0', 0, [], [])]
    return [
        fill_case('no_bindings', [], {}, {}),
        fill_case('negative_count', one, {'set0': (['g0'], kinds)}, {'g0': [0, 1]}, count=-3),
        fill_case('every_kind', one, {'set0': (['g0'], kinds)}, {'g0': [0, 1, 2, 3, 4, 5]}),
        fill_case('listed_twice', one, {'set0': (['g0'], kinds)}, {'g0': [5, 0, 5, 2]}),
        fill_case('empty_group', one, {'set0': (['g0'], kinds)}, {'g0': []}),
        fill_case('second_group', [('set0', 1, [], [])], {'set0': (['g0', 'g1'], kinds)},
                  {'g0': [0, 5], 'g1': [1, 3, 4]}),
        fill_case('three_bindings_shared_set', [('set0', 0, [], []), ('set0', 1, [], []), ('set1', 0, [], [])],
                  {'set0': (['g0', 'g1'], kinds), 'set1': (['g2'], (1, 1, 0))},
                  {'g0': [0], 'g1': [5, 2], 'g2': [0, 1, 2]}),
        fill_case('arguments_passed_on', one, {'set0': (['g0'], kinds)}, {'g0': [0]}, entries=-7,
                  seconds=-0.0),
        fill_case('seconds_nan', one, {'set0': (['g0'], kinds)}, {'g0': [3]}, seconds=float('nan')),
    ]


def refresh_case(cid, bindings, sets, groups, targets=('t0', 't1', 't2'), shrink=False, count=None):
    """groups: name -> (shorts, longs) target indexes."""
    c = Case('refresh', cid)
    for name, group_names in sets.items():
        c.binding_set(name, group_names)
    for name, (shorts, longs) in groups.items():
        c.group(name, shorts=shorts, longs=longs, size=0x60 if shrink else 0x48)
    for t in targets:
        c.target(t)
    stubs = []
    if shrink:   # a target at g0+0x50 whose first call shrinks g0's short count to 1
        c.target(None, inside='g0+0x50')
    c.generator(bindings, count)
    for k, (set_name, group_index, shorts, longs) in enumerate(bindings):
        if count is not None and k >= count:
            break
        g = sets[set_name][group_index]
        short_records, long_records = groups[g]
        n_short = len(short_records) if not shrink else 1
        for r, index in enumerate(short_records[:n_short]):
            s = {'address': TAKE_SHORT, 'argc': 2, 'argmem': [{'arg': 1, 'size': 0xc}]}
            if shrink and r == 0:
                s['writes'] = [{'arg': 0, 'offset': 0x30 - 0x50, 'bytes': d(1)}]
            stubs.append(s)
        for index in long_records:
            stubs.append({'address': TAKE_LONG, 'argc': 2, 'argmem': [{'arg': 1, 'size': 0x18}]})
    c.c['stubs'] = stubs
    return c.done([(TAKE_SHORT, 2), (TAKE_LONG, 2)])


def refresh_cases():
    targets = ['t0', 't1', 't2']
    return [
        refresh_case('no_bindings', [], {}, {}),
        refresh_case('negative_count', [('set0', 0, targets, targets)], {'set0': ['g0']}, {'g0': ([0], [1])},
                     count=-1),
        refresh_case('short_only', [('set0', 0, targets, targets)], {'set0': ['g0']}, {'g0': ([0, 2, 1], [])}),
        refresh_case('long_only', [('set0', 0, targets, targets)], {'set0': ['g0']}, {'g0': ([], [2, 0])}),
        refresh_case('both', [('set0', 0, targets, targets)], {'set0': ['g0']}, {'g0': ([1, 1], [0, 2, 2])}),
        refresh_case('empty', [('set0', 0, targets, targets)], {'set0': ['g0']}, {'g0': ([], [])}),
        refresh_case('second_group', [('set0', 1, targets, ['t2', 't1', 't0'])], {'set0': ['g0', 'g1']},
                     {'g0': ([0], [0]), 'g1': ([2], [0, 1])}),
        refresh_case('three_bindings', [('set0', 0, targets, targets), ('set0', 1, ['t2'], ['t1']),
                                        ('set1', 0, ['t1', 't0'], targets)],
                     {'set0': ['g0', 'g1'], 'set1': ['g2']},
                     {'g0': ([0], [1]), 'g1': ([0], [0]), 'g2': ([1, 0], [2])}),
        refresh_case('short_count_shrinks', [('set0', 0, ['g0+0x50', 't1', 't2'], targets)], {'set0': ['g0']},
                     {'g0': ([0, 1, 2], [1])}, shrink=True),
        binding_count_shrinks(),
    ]


def binding_count_shrinks():
    """A target living inside the generator whose call drops the binding count to 1 (read again)."""
    c = Case('refresh', 'binding_count_shrinks')
    c.binding_set('set0', ['g0', 'g1'])
    c.group('g0', shorts=[0], longs=[1])
    c.group('g1', shorts=[0, 1], longs=[0])
    for t in ('t0', 't1'):
        c.target(t)
    c.target(None, inside='generator+0x8')
    c.generator([('set0', 0, ['generator+0x8'], ['t0', 't1']), ('set0', 1, ['t0', 't1'], ['t1'])])
    c.c['stubs'] = [{'address': TAKE_SHORT, 'argc': 2, 'argmem': [{'arg': 1, 'size': 0xc}],
                     'writes': [{'arg': 0, 'offset': 0x18 - 0x8, 'bytes': d(1)}]},
                    {'address': TAKE_LONG, 'argc': 2, 'argmem': [{'arg': 1, 'size': 0x18}]}]
    return c.done([(TAKE_SHORT, 2), (TAKE_LONG, 2)])


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__.strip().splitlines()[3], file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    cases = {'size': size_cases, 'fill': fill_cases, 'refresh': refresh_cases}[sys.argv[2]]()
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
