#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for physics_instance_construct_01c0c2b0 (0x01c0c2b0), for tools/verify.py run.

usage: instance_construct_cases.py OUT_DIR

Builds the physics manager and world (body table 0x90 bytes per body, motions 0x80, constraints
0x38, the listener list at +0x538), the thread's allocator (thread-local storage), the heaps and
the body set the set constructor would fill, in named buffers; virtual methods point at spare
addresses the cases stub (the instance's own listener keeps its real method, 0x01c00f80).
Covers: the manager's special modes (as in the game) and normal mode (motion values rescaled,
listeners told, kept, removed before or after the call, the root update), inactive bodies,
16-bit motion values that saturate or come from NaN patterns, offsets with NaN and -0, the
heap check, the shape table copy (empty, granted in full, granted short and reallocated with or
without a free), no shape flags, no bodies or constraints, and the motion types collected into
the id vector (below 5, repeated, already there).
"""
import json
import os
import struct
import sys

STUB = {'thread': 0x00500000, 'info': 0x00500100, 'alloc50': 0x00500200, 'alloc58': 0x00500300,
        'shape_alloc': 0x00500400, 'shape_free': 0x00500500, 'notify': 0x00500600, 'destroy': 0x00500700,
        'arg_alloc': 0x00500800}
OWN_LISTENER_MODE = 0x01c00f80
TLS_SLOT = -0x748


def f32s(*v):
    return struct.pack(f'<{len(v)}f', *v).hex()


def h(fmt, *v):
    return struct.pack(fmt, *v).hex()


class Case:
    def __init__(self, cid):
        self.cid, self.buffers, self.memory, self.stubs, self.imports = cid, {}, [], [], []

    def buf(self, name, size):
        self.buffers[name] = max(self.buffers.get(name, 0), (size + 15) // 16 * 16)
        return name

    def at(self, where, off):
        return where if where.startswith('0x') else f'buf:{where}+{off}'

    def bytes(self, where, off, data):
        self.memory.append({'addr': self.at(where, off), 'bytes': data})

    def ptr(self, where, off, target):
        self.memory.append({'addr': self.at(where, off), 'pointer': target})

    def guest(self, where, off, address):
        self.memory.append({'addr': self.at(where, off), 'guest': f'0x{address:08x}'})

    def stub(self, name_or_address, argc=None, **kw):
        a = STUB.get(name_or_address, name_or_address)
        e = {'address': a if isinstance(a, str) else f'0x{a:08x}', **kw}
        if argc is not None:
            e['argc'] = argc
        self.stubs.append(e)

    def vtable(self, obj, slots):
        vt = self.buf(obj + '_vt', 0x60)
        self.ptr(obj, 0, vt)
        for off, name in slots.items():
            self.guest(vt, off, STUB[name])


def case(cid, special=(1, 0), bodies=((1, 0, (0x10, 0x20, -0x30, 0x40), 7), (1, 1, (0, 0, 0, 0), 3)),
         constraints=(5, 0), offset=(1.0, -2.0, 3.0, 9.0), heap_ok=True, shapes=2, granted=None, realloc=None,
         shape_flags=4, listeners=(), ids_initial=(), alloc_null=False, links_garbage=False, body_lane=None):
    """bodies: (active, table index, four 16-bit motion values, motion type), active 0/1 or the raw
    flag byte; listeners: 'keep', 'flagged' / 'flagged2' (tag 1 / 2: removed before the call) or
    'drop' (removed after it). links_garbage: the link records' flag bytes start as 0xff (the
    harness's memset is scripted and leaves them). body_lane: hex bytes of a body's position."""
    c = Case(cid)
    s = c.buf('self', 0x1f0)
    c.bytes(s, 0xa, 'ff')
    # Manager and world.
    m = c.buf('mgr', 0x280)
    c.ptr('0x0593d700', None, m)
    c.ptr(m, 0x28, c.buf('mgr28', 0x10))
    world = c.buf('world', 0x540)
    c.ptr('mgr28', 8, world)
    c.bytes(m, 0x274, h('<BB', *special))
    n_table = max([b[1] for b in bodies] + [0]) + 1
    c.ptr(world, 0x20, c.buf('table', 0x90 * n_table))
    c.ptr(world, 0xe0, c.buf('motions', 0x80 * (len(bodies) + 1)))
    c.ptr(world, 0x128, c.buf('cons', 0x38 * 8))
    for i, (active, index, motion, mtype) in enumerate(bodies):
        c.bytes('table', index * 0x90 + 0x30, body_lane or f32s(0.5 * i, -1.0, 2.5, -0.0))
        c.bytes('table', index * 0x90 + 0x40, f'{active:02x}' if active > 1 else ('03' if active else '04'))
        c.bytes('table', index * 0x90 + 0x68, h('<i', i))
        c.bytes('motions', i * 0x80 + 0x20, h('<4h', *motion))
        c.bytes('motions', i * 0x80 + 0x38, h('<H', mtype))
    # Thread allocator.
    below = -TLS_SLOT
    tls = c.buf('tls', below + 8)
    c.bytes('0x057e40f8', None, h('<q', TLS_SLOT))
    c.ptr(tls, below, f'tls+{below}')
    c.ptr(tls, 0, c.buf('holder', 0x60))
    c.ptr('holder', 0x58, c.buf('talloc', 0x10))
    c.vtable('talloc', {0x10: 'thread'})
    # Body set (what the set constructor leaves).
    n1, n2 = len(bodies), len(constraints)
    c.buf('idx', 4 * max(n1, 1))
    c.bytes('idx', 0, ''.join(h('<i', b[1]) for b in bodies))
    c.buf('cidx', 4 * max(n2, 1))
    c.bytes('cidx', 0, ''.join(h('<I', x) for x in constraints))
    shapes_obj = c.buf('shapes', 0x48)
    c.bytes(shapes_obj, 0x30, h('<i', shape_flags))
    c.bytes(shapes_obj, 0x40, h('<i', shapes))
    if shapes > 0:
        src = c.buf('src', 0x30 * shapes)
        c.ptr(shapes_obj, 0x38, src)
        c.bytes(src, 0, ''.join(f32s(*[k + 10 * n for k in range(12)]) for n in range(shapes)))
    c.buf('rootset', 0x48)
    c.ptr('rootset', 0x40, c.buf('root', 0x40))
    c.bytes('root', 0x30, f32s(-0.0, 4.0, float('nan'), 1.0))
    # Arguments.
    alloc9 = c.buf('alloc9', 0x10)
    c.vtable(alloc9, {0x50: 'arg_alloc'})
    c.buf('offset', 16)
    c.bytes('offset', 0, f32s(*offset) if isinstance(offset[0], float) else offset[0])
    # Heaps.
    ih = c.buf('idsheap', 0x10)
    c.ptr('0x05940450', None, ih)
    c.vtable(ih, {0x20: 'info'})
    fh = c.buf('flagheap', 0x10)
    c.ptr('0x05940408', None, fh)
    c.vtable(fh, {0x50: 'alloc50', 0x58: 'alloc58'})
    c.ptr('0x058018b0', None, c.buf('shapeheap_vt', 0x30))
    c.guest('shapeheap_vt', 0x20, STUB['shape_alloc'])
    c.guest('shapeheap_vt', 0x28, STUB['shape_free'])
    # Listeners at world +0x538 (tagged pointers).
    nodes = [c.buf(f'node{i}', 0x10) for i in range(len(listeners))]
    if nodes:
        c.ptr(world, 0x538, nodes[0])
    for i, kind in enumerate(listeners):
        c.vtable(nodes[i], {0x18: 'notify', 0x8: 'destroy'})
        nxt = nodes[i + 1] if i + 1 < len(nodes) else None
        tag = {'flagged': 1, 'flagged2': 2}.get(kind, 0)
        if nxt:
            c.ptr(nodes[i], 8, f'{nxt}+{tag}')
        else:
            c.bytes(nodes[i], 8, h('<Q', tag))
    # Stubs, in call order per address.
    c.stub('thread', 2, ret='buf:set')
    c.buf('set', 0x58)
    w = [{'arg': 0, 'offset': 0x10, 'pointer': 'rootset'}, {'arg': 0, 'offset': 0x20, 'pointer': 'idx'},
         {'arg': 0, 'offset': 0x30, 'pointer': 'cidx'}, {'arg': 0, 'offset': 0x40, 'pointer': 'shapes'},
         {'arg': 0, 'offset': 0x28, 'bytes': h('<i', n1)}, {'arg': 0, 'offset': 0x38, 'bytes': h('<i', n2)}]
    c.stub('0x00ae8700', 6, writes=w)
    for _ in bodies:
        c.stub('0x00f67880', 4, argmem=[{'arg': 2, 'size': 16}])
    c.stub('info', 3, writes=[{'arg': 0, 'bytes': '20' if heap_ok else '00'}])
    c.stub('0x024b55b0', 3)
    c.buf('ids', 0x80)
    c.bytes('ids', 0, ''.join(h('<I', v) for v in ids_initial))
    c.stub('0x02bc1ca0', 2, writes=[{'arg': 0, 'offset': 8, 'pointer': 'ids'},
                                    {'arg': 0, 'offset': 16, 'pointer': f'ids+{4 * len(ids_initial)}'},
                                    {'arg': 0, 'offset': 24, 'pointer': 'ids+128'}])
    if n1 + n2:
        c.buf('links', 16 * (n1 + n2))
        if links_garbage:
            for r in range(n1 + n2):
                c.bytes('links', 16 * r + 10, 'ff')
        c.stub('arg_alloc', ret='buf:links', argi=[0, 1])
        c.imports.append({'name': 'memset', 'argc': 3, 'ret': 'buf:links'})
    # Shape copy: grants in bytes (first allocation, then the reallocation).
    grants = granted if granted is not None else [0x30 * shapes]
    for k, g in enumerate(grants):
        c.buf(f'shapes{k}', max(g, 0x30 * shapes, 16))
        c.stub('shape_alloc', 2, ret=f'buf:shapes{k}', writes=[{'arg': 1, 'bytes': h('<i', g)}])
    c.stub('shape_free', 3)
    c.stub('shape_free', 3)
    c.stub('thread', 2, ret='buf:shapeobj')
    c.buf('shapeobj', 0x50)
    c.stub('0x00ac46a0', 4, argmem=[{'arg': 3, 'size': 16}],
           writes=[{'arg': 3, 'offset': 0, 'pointer': 'shapes0'} if realloc != 'take' else
                   {'arg': 3, 'offset': 0, 'bytes': '00' * 8},
                   {'arg': 3, 'offset': 8, 'bytes': h('<iI', shapes, 0x80000000 if realloc == 'take' else max(shapes, 1))}])
    c.stub('thread', 2, ret='buf:controller')
    c.buf('controller', 0xe0)
    c.stub('0x00ae3fc0', 3, argmem=[{'arg': 3, 'size': 48}])
    c.stub('0x0083d6d0', 2, argmem=[{'arg': 1, 'size': 64}], writes=[{'arg': 0, 'bytes': f32s(*range(12))}])
    c.buf('bodyflags', 16)
    c.stub('alloc58', 3, ret='buf:bodyflags')
    c.imports.append({'name': 'memset', 'argc': 3, 'ret': 'buf:bodyflags'})
    if shape_flags > 0:
        c.buf('shapeflags', 16)
        c.stub('alloc50', ret='buf:shapeflags', argi=[0, 1])
        c.imports.append({'name': 'memset', 'argc': 3, 'ret': 'buf:shapeflags'})
    for kind in listeners * len(bodies):
        if kind == 'drop':
            c.stub('notify', 3, writes=[{'arg': 0, 'offset': 8, 'bytes': h('<Q', 2)}])
        else:
            c.stub('notify', 3)
    for _ in range(4 * len(bodies)):
        c.stub('destroy', 1)
    c.stub('0x00ae6180', 1, argf32=[0])
    c.stub('0x00ac5e00', 1, writes=[{'arg': 1, 'bytes': f32s(*range(20, 28))}])
    if alloc_null:
        c.stub('alloc58', 3, ret='0x0')
    else:
        c.buf('listener', 0x38)
        c.stub('alloc58', 3, ret='buf:listener')
        c.stub('thread', 2, ret='buf:linkobj')
        c.buf('linkobj', 0x50)
    c.stub(OWN_LISTENER_MODE, 2)
    c.stub('thread', 2, ret='buf:pair')
    c.buf('pair', 0x18)
    c.stub('0x00ae6dc0', 1)
    c.stub('thread', 2, ret='buf:tracker')
    c.buf('tracker', 0x40)
    c.stub('0x00ae6d00', 1, argmem=[{'arg': 1, 'size': 16}])
    pushed = list(ids_initial)
    for b in bodies:
        if b[3] >= 5 and b[3] not in pushed:
            pushed.append(b[3])
            c.stub('0x02bc1bb0', 1, argmem=[{'arg': 1, 'size': 4}],
                   writes=[{'arg': 0, 'offset': 16, 'pointer': f'ids+{4 * len(pushed)}'}])
            c.bytes('ids', 4 * (len(pushed) - 1), h('<I', b[3]))
    return {'schema': 1, 'address': '0x01c0c2b0', 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:self', 'rsi': '0x1111', 'rdx': '0x2222', 'rcx': '0x3333', 'r8': '0x4444', 'r9': '0x5555'},
            'stack': ['0x2a', '0x6666', 'buf:alloc9', 'buf:offset'], 'gs': f'buf:tls+{below}',
            'buffers': {k: {'size': v} for k, v in c.buffers.items()}, 'memory': c.memory, 'stubs': c.stubs,
            'imports': c.imports}


SAT = ((1, 0, (0x7fff, -0x8000, 0x7fc0, -0x0001), 9), (1, 1, (0x4000, 0x0100, -0x7f80, 0x7f80), 9))
CASES = [
    case('special_mode'),
    case('special_mode_275', special=(0, 1)),
    case('normal_mode', special=(0, 0)),
    case('normal_mode_listeners', special=(0, 0), listeners=('keep', 'flagged', 'drop', 'keep')),
    case('normal_mode_single_drop', special=(0, 0), listeners=('drop',)),
    case('normal_mode_saturating', special=(0, 0), bodies=SAT),
    case('normal_mode_inactive', special=(0, 0), bodies=((0, 0, (1, 2, 3, 4), 6), (1, 1, (5, 6, 7, 8), 6))),
    case('normal_mode_no_bodies', special=(0, 0), bodies=(), constraints=()),
    case('offset_nan_negzero', offset=(float('nan'), -0.0, 1.5, float('nan'))),
    case('heap_check_fails', heap_ok=False),
    case('no_shapes', shapes=0, granted=[]),
    case('shapes_granted_short', shapes=3, granted=[0x30, 0x90]),
    case('shapes_granted_none', shapes=2, granted=[0, 0x60]),
    case('shapes_taken_by_shape_object', realloc='take'),
    case('no_shape_flags', shape_flags=0),
    case('constraints_only', bodies=(), constraints=(1, 2, 7)),
    case('link_flags_kept', links_garbage=True),
    case('listener_tag2', special=(0, 0), listeners=('keep', 'flagged2', 'keep')),
    case('active_by_bit1', special=(0, 0), bodies=((2, 0, (9, 8, 7, 6), 6), (1, 1, (1, 1, 1, 1), 6))),
    # NaN on both sides of the offset add: the result keeps the offset's payload.
    case('offset_and_body_nan', offset=(f32s(0.0, 0.0, 0.0, 0.0).replace('00000000', '0100c07f', 1),),
         body_lane='0200c07f' + f32s(1.0, 2.0, 3.0)),
    case('motion_types', bodies=((1, 0, (0,) * 4, 4), (1, 1, (0,) * 4, 5), (1, 2, (0,) * 4, 5), (1, 3, (0,) * 4, 12)),
         ids_initial=(12,)),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for cs in CASES:
        with open(os.path.join(sys.argv[1], f"{cs['id']}.json"), 'w') as f:
            json.dump(cs, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
