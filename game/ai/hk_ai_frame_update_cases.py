#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for ai_hk_frame_update_0222bc10 (0x0222bc10), for tools/verify.py run.

usage: hk_ai_frame_update_cases.py OUT_DIR

Builds the manager, render manager, groups of AI objects and the two maps (MSVC trees: left
+0x0, parent +0x8, right +0x10, nil flag +0x19, key +0x20, value +0x28) in named buffers.
Covers both flags, the matrix copy with and without its objects, the debug position from the
frame object or the player character (and their fallbacks), the debug marks (flags, the
distance map below / above / NaN), the manager-wide draw, empty groups and null objects, the
axis frames (id -1, negative index, ids missing or found, null links, points inside and outside
the model), render states already at or away from their defaults, and box modes 3, 2 and 1.
"""
import json
import os
import struct
import sys


def f32s(*v):
    return struct.pack(f'<{len(v)}f', *v).hex()


class Case:
    def __init__(self, cid):
        self.cid = cid
        self.buffers = {}
        self.memory = []
        self.stubs = []

    def buf(self, name, size):
        self.buffers[name] = max(self.buffers.get(name, 0), size)
        return name

    def bytes(self, where, off, data):
        addr = where if where.startswith('0x') else f'buf:{where}+{off}'
        self.memory.append({'addr': addr, 'bytes': data})

    def ptr(self, where, off, target):
        addr = where if where.startswith('0x') else f'buf:{where}+{off}'
        self.memory.append({'addr': addr, 'pointer': target})

    def stub(self, address, argc, **kw):
        self.stubs.append({'address': address, 'argc': argc, **kw})

    def tree(self, name, entries):
        """A head and up to three nodes: (key bytes, value buffer or None, extra bytes at +0x28)."""
        head = self.buf(name, 0x30)
        self.bytes(head, 0x19, '01')
        nodes = []
        for i, (key, value, extra) in enumerate(entries):
            n = self.buf(f'{name}_n{i}', 0x30)
            self.bytes(n, 0x20, key)
            if value:
                self.ptr(n, 0x20 if extra is not None else 0x28, value)
            if extra is not None:
                self.bytes(n, 0x28, extra)
            nodes.append(n)
        if not nodes:
            for off in (0x0, 0x8, 0x10):
                self.ptr(head, off, head)
            return head
        # Sorted entries: middle as root.
        order = nodes
        if len(order) == 1:
            root, left, right = order[0], None, None
        elif len(order) == 2:
            root, left, right = order[1], order[0], None
        else:
            left, root, right = order
        self.ptr(head, 0x8, root)
        self.ptr(head, 0x0, left or root)
        self.ptr(head, 0x10, right or root)
        self.ptr(root, 0x8, head)
        self.ptr(root, 0x0, left or head)
        self.ptr(root, 0x10, right or head)
        for child in (left, right):
            if child:
                for off in (0x0, 0x10):
                    self.ptr(child, off, head)
                self.ptr(child, 0x8, root)
        return head

    def json(self, args):
        return {'schema': 1, 'address': '0x0222bc10', 'id': self.cid, 'returns': 'void', 'args': args,
                'buffers': {k: {'size': v} for k, v in self.buffers.items()}, 'memory': self.memory,
                'stubs': self.stubs, 'imports': []}


def case(cid, reset=1, full=1, frame=True, view=True, groups=((2, 1), (0, 0), (1, 3)), null_entries=False,
         debug=None, chr_man=None, marks=(0, 0, 0, 0), distances=(), draw_manager=False, pairs=(),
         state_default=True, mode=0, box_vectors=None):
    """debug: None, 'frame', 'origin' (frame null) for the debug position with WorldChrMan absent;
    chr_man: None, 'dbg' (WorldChrManDbg +0x108), 'man' (WorldChrMan +0x60), 'none'.
    groups: (elements, objects per element) per group. pairs: (id, index) at +0x2cb8."""
    c = Case(cid)
    s = c.buf('self', 0x2ed0)
    args = {'rdi': 'buf:self', 'rsi': 'buf:frame' if frame else '0x0', 'rdx': str(reset), 'rcx': str(full)}
    if frame:
        c.buf('frame', 0x50)
        c.bytes('frame', 0x10, f32s(*[0.5 * i - 3 for i in range(16)]))
        c.bytes('frame', 0x30, f32s(1.0, -2.0, 3.5, 1.0) + f32s(10.0, 20.0, -30.0, 0.0))
    m = c.buf('mgr', 0x30)
    c.ptr('0x059401a0', None, m)
    c.ptr(m, 0x10, c.buf('world', 0x20))
    if view:
        c.ptr(m, 0x20, c.buf('view', 0x80))
    # Groups of AI objects.
    c.bytes(s, 0x2bc8, struct.pack('<i', len(groups)).hex())
    if groups:
        g = c.buf('groups', 32 * len(groups))
        c.ptr(s, 0x2bd0, g)
        for i, (elements, objects) in enumerate(groups):
            c.bytes(g, 32 * i + 8, struct.pack('<i', elements).hex())
            if elements:
                e = c.buf(f'elements{i}', 0xb0 * elements)
                c.ptr(g, 32 * i + 0x10, e)
                for j in range(elements):
                    c.bytes(e, 0xb0 * j + 0x58, struct.pack('<i', objects).hex())
                    if objects:
                        lst = c.buf(f'list{i}_{j}', 8 * objects)
                        c.ptr(e, 0xb0 * j + 0x60, lst)
                        for k in range(objects):
                            if null_entries and k % 2:
                                continue
                            c.ptr(lst, 8 * k, c.buf(f'obj{i}_{j}_{k}', 16))
    # Debug display.
    if debug or chr_man:
        c.ptr(s, 0x2c18, c.buf('dbgupd', 16))
        disp = c.buf('display', 16)
        c.ptr(s, 0x2c20, disp)
        if chr_man:
            cm = c.buf('chrman', 0x68)
            c.ptr('0x0593e878', None, cm)
            dbg = c.buf('chrdbg', 0x110)
            c.ptr('0x0593e880', None, dbg)
            if chr_man in ('dbg', 'man'):
                chr_ = c.buf('chr', 0x60)
                c.ptr(dbg if chr_man == 'dbg' else cm, 0x108 if chr_man == 'dbg' else 0x60, chr_)
                for a, off, b in (('chr', 0x58, 'p1'), ('p1', 8, 'p2'), ('p2', 0x3b0, 'p3'), ('p3', 0x68, 'p4')):
                    c.ptr(a, off, c.buf(b, 0x3c0 if b == 'p2' else 0x1f0 if b == 'p4' else 0x70))
                c.bytes('p4', 0x1e0, f32s(4.0, 5.0, 6.0, 1.0))
        a91, a92, a93, a90 = marks
        c.bytes(s, 0x2c90, f'{a90:02x}{a91:02x}{a92:02x}{a93:02x}')
        if a91 or a92:
            c.ptr(disp, 8, c.buf('dispobj', 0x750))
            c.bytes('dispobj', 0x740, '41')
        if a93:
            c.bytes(s, 0x2c6c, f32s(10.0))
            entries = []
            for i, d in enumerate(distances):
                if d is None:
                    entries.append(('0' * 16, None, f32s(1.0)))   # the object slot is the key's
                    continue
                o = c.buf(f'mark{i}', 0x750)
                c.bytes(o, 0x740, '10')
                entries.append((struct.pack('<I', i).hex(), o, f32s(d) if isinstance(d, float) else d))
            c.ptr(s, 0x2c78, c.tree('dmap', entries))
    elif draw_manager:
        c.bytes(s, 0x2c90, '01')
    c.bytes(s, 0x2c94, '0102030405000000' + 'aabbccdd')
    # Render manager.
    rm = c.buf('rendman', 0x28)
    c.ptr('0x05940298', None, rm)
    r = c.buf('renderer', 0x28)
    c.ptr(rm, 0x20, r)
    c.bytes(r, 0x20, '01000000')
    slot = c.buf('slot', 0x48)
    c.ptr(r, 0x18, slot)
    st = c.buf('state', 0x40)
    c.ptr(slot, 0x40, st)
    if state_default:
        c.bytes(st, 0x8, '81')
        c.bytes(st, 0x18, '0000000001000000')
        k = 'c00b0d04' if False else None
    else:
        c.bytes(st, 0x8, '00')
        c.bytes(st, 0x18, '0500000000000000' + f32s(9, 9, 9, 9) + f32s(1, 2, 3, 4))
    # Axis frames.
    if pairs:
        c.bytes(s, 0x2ec0, struct.pack('<Q', len(pairs)).hex())
        c.bytes(s, 0x2cb8, ''.join(struct.pack('<Ii', i, x).hex() for i, x in pairs))
        ents = []
        for key, kind in ((10, 'model_big'), (20, 'null_entity'), (30, 'null_a'), (40, 'model_small')):
            if kind == 'null_entity':
                ents.append((struct.pack('<I', key).hex(), None, None))
                continue
            e = c.buf(f'ent{key}', 0xc0)
            ents.append((struct.pack('<I', key).hex(), e, None))
            if kind == 'null_a':
                continue
            for a, off, b in ((e, 0xb8, f'a{key}'), (f'a{key}', 0x50, f'b{key}'), (f'b{key}', 0x28, f'model{key}')):
                c.ptr(a, off, c.buf(b, 0x120))
            c.bytes(f'model{key}', 0x18, struct.pack('<i', 3 if kind == 'model_big' else 0).hex())
            c.bytes(f'model{key}', 0x118, struct.pack('<i', 2 if kind == 'model_big' else 1).hex())
        # A tree of four: a root with a left and a right child, and the right child's right child.
        head = c.tree('idmap', ents[:3])
        n3 = 'idmap_n3'
        c.buf(n3, 0x30)
        c.bytes(n3, 0x20, ents[3][0])
        c.ptr(n3, 0x28, ents[3][1])
        c.bytes(n3, 0x19, '00')
        c.ptr('idmap_n2', 0x10, n3)
        c.ptr(n3, 0x8, 'idmap_n2')
        c.ptr(n3, 0x0, head)
        c.ptr(n3, 0x10, head)
        c.ptr(head, 0x10, n3)
        c.ptr('world', 0x10, head)
        for i in range(len(pairs)):
            c.stub('0x029ae440', 3, writes=[{'arg': 2, 'bytes': f32s(1.5 + i, -2.0, 0.25 * i, 7.0)}])
    if mode:
        c.bytes(s, 0x2c68, struct.pack('<i', mode).hex())
        v = box_vectors or (f32s(1, 2, 3, 0), f32s(-1, -2, -3, 0), f32s(5, 6, 7, 0))
        c.bytes(s, 0x2c30, v[0])
        c.bytes(s, 0x2c40, v[1])
        c.bytes(s, 0x2c50, v[2])
        c.bytes(s, 0x2c60, f32s(0.75))
    # Stubs (queues per address; unused ones are left over).
    c.stub('0x024b55b0', 4)
    for _ in range(4):
        c.stub('0x02224090', 3)
    c.stub('0x021cdad0', 2, argmem=[{'arg': 1, 'size': 12}])
    c.stub('0x02116830', 2, writes=[{'arg': 0, 'bytes': f32s(*[float(i) for i in range(40)])}])
    c.stub('0x016334b0', 4, argmem=[{'arg': 1, 'size': 16}])
    for addr, n in (('0x01638ad0', 1), ('0x01633ac0', 1), ('0x021d34b0', 2), ('0x0163bb50', 1)):
        c.stub(addr, n)
    for _ in range(16):
        c.stub('0x022211b0', 2, argmem=[{'arg': 1, 'size': 83}])
    c.stub('0x021d6ea0', 3, argmem=[{'arg': 2, 'size': 83}])
    for _ in range(8):
        c.stub('0x0135d810', 2, argmem=[{'arg': 1, 'size': 48}])
    c.stub('0x02dadec0', 3, argf32=[0], argmem=[{'arg': 1, 'size': 16}, {'arg': 2, 'size': 16}])
    return c.json(args)


NAN = '0100c07f'
CASES = [
    case('clinic_like'),
    case('no_reset', reset=0),
    case('not_full', full=0),
    case('not_full_no_reset', reset=0, full=0),
    case('no_frame', frame=False),
    case('no_view', view=False),
    case('no_groups', groups=()),
    case('empty_lists', groups=((3, 0), (0, 0))),
    case('null_entries', groups=((2, 4),), null_entries=True),
    case('debug_from_frame', debug='frame'),
    case('debug_origin', debug='origin', frame=False),
    case('debug_chr_dbg', chr_man='dbg'),
    case('debug_chr_man', chr_man='man'),
    case('debug_chr_none', chr_man='none'),
    case('marks_91', debug='frame', marks=(1, 0, 0, 0)),
    case('marks_92', debug='frame', marks=(0, 1, 0, 0)),
    case('marks_skipped_by_90', debug='frame', marks=(1, 1, 1, 1), distances=(5.0,)),
    case('distance_map', debug='frame', marks=(0, 0, 1, 0), distances=(5.0, 10.0, 20.0)),
    case('distance_map_nan_and_null', debug='frame', marks=(0, 0, 1, 0), distances=(NAN, None, 9.5)),
    case('distance_map_single', debug='frame', marks=(0, 0, 1, 0), distances=(-1.0,)),
    case('distance_map_empty', debug='frame', marks=(0, 0, 1, 0), distances=()),
    case('draw_manager', draw_manager=True),
    case('axes', pairs=((10, 0), (10, 4), (10, 5))),
    case('axes_skipped', pairs=((0xffffffff, 0), (10, -1))),
    case('axes_missing', pairs=((5, 0), (15, 0), (50, 0), (25, 0))),
    case('axes_null_links', pairs=((20, 0), (30, 0))),
    case('axes_small_model', pairs=((40, 0), (40, 1))),
    case('axes_state_changed', pairs=((10, 1),), state_default=False),
    case('box_mode3', mode=3),
    case('box_mode2', mode=2),
    case('box_mode2_state_changed', mode=2, state_default=False),
    case('box_mode1', mode=1),
    case('everything', debug='frame', marks=(1, 1, 1, 0), distances=(1.0, 50.0), pairs=((10, 2), (40, 0)), mode=3),
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
