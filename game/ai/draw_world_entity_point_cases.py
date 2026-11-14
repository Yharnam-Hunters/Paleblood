#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the AI world draw and entity point (game/ai/draw_world_entity_point.cpp), for tools/verify.py run.

usage: draw_world_entity_point_cases.py OUT_DIR world|point

- world: no renderer (the world, passed as null, is not read), the state already at the world
  draw's values, every setting different (alone and all together, with other dirty bits kept), a
  NaN and a negative zero in the float setting, render slot 1 and -1, no section, one, three, a
  section call that swaps the section array (it is read again for every section) and ones that
  lower or raise the count (it was read once). The section draw is a stub scripted once per
  expected call: its calls, arguments and order are compared.
- point: original faces through an empty face map (the index picks the mapped copy), through a
  map entry of -1 (the original) and a map entry (that copy), an added face, the face index -1;
  the same three edge paths and added edges; original, added and negative vertex indices; edge
  counts 0 (NaN), 1, 3, 5, 7 and negative; NaN payloads in the transform and the vertices (the
  operand order decides which comes out; with z finite too, so that x's and y's are not hidden);
  infinities; and a frame prefilled so that only the first row may change.
"""
import json
import os
import struct
import sys

ADDRESSES = {'world': '0x021d6ea0', 'point': '0x029ae440'}
DRAW_SECTION = '0x021d5960'
SECTION_SIZE = 0x30
STATE_SIZE = 0x80
WORLD_VALUES = {'s18': 2, 's1c': 0, 's70': 3, 's78': 16.0}
FACE_SIZE, EDGE_SIZE, VERTEX_SIZE = 0x10, 0x14, 0x10
ARRAY_LEN = 16   # entries per array, after one guard entry for index -1
GUARD = 1
MAP_GUARD = 2   # the map entry at index -1: a valid copy


def f32s(*v):
    return struct.pack(f'<{len(v)}f', *v).hex()


def u32(v):
    return (v & 0xffffffff).to_bytes(4, 'little').hex()


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def state_bytes(s18, s1c, s70, s78, dirty=0, dirty_high=0, s78_raw=None):
    b = bytearray(b'\x33' * STATE_SIZE)
    b[0x8], b[0x9] = dirty, dirty_high
    b[0x18:0x1c] = struct.pack('<I', s18 & 0xffffffff)
    b[0x1c:0x20] = struct.pack('<I', s1c & 0xffffffff)
    b[0x70:0x74] = struct.pack('<I', s70 & 0xffffffff)
    b[0x78:0x7c] = struct.pack('<I', s78_raw) if s78_raw is not None else struct.pack('<f', s78)
    return b.hex()


def world_case(cid, renderer=True, slot=0, states=None, sections=3, swap=False, lower_to=None):
    """states: settings of the state of slot 0, 1 and -1 (the renderer word before slot 0)."""
    c = base('world', cid)
    st = dict(WORLD_VALUES)
    st.update(states or {})
    c['buffers'] = {'view': {'size': 0x90, 'bytes': '44' * 0x90}, 'desc': {'size': 0x60, 'bytes': '55' * 0x60}}
    m = c['memory']
    c['args'] = {'rdi': 'buf:view', 'rsi': 0, 'rdx': 'buf:desc'}
    if not renderer:
        m.append({'addr': 'buf:view+0x8', 'bytes': '00' * 8})
        return c
    other = dict(WORLD_VALUES, s18=7, s70=9)   # the slots not drawn into differ, so a wrong slot shows
    c['buffers'].update({
        'renderer': {'size': 0x28}, 'slot_minus': {'size': 0x48}, 'slot0': {'size': 0x48}, 'slot1': {'size': 0x48},
        'state_minus': {'size': STATE_SIZE, 'bytes': state_bytes(**(st if slot == -1 else other))},
        'state0': {'size': STATE_SIZE, 'bytes': state_bytes(**(st if slot == 0 else other))},
        'state1': {'size': STATE_SIZE, 'bytes': state_bytes(**(st if slot == 1 else other))},
        'world': {'size': 0x58}, 'holder': {'size': 0x28}, 'list': {'size': 0x28},
        'sections': {'size': SECTION_SIZE * max(sections, 1), 'bytes': '66' * (SECTION_SIZE * max(sections, 1))},
        'sections_b': {'size': SECTION_SIZE * max(sections, 1), 'bytes': '77' * (SECTION_SIZE * max(sections, 1))}})
    m += [{'addr': 'buf:view+0x8', 'pointer': 'renderer+0x0'},
          {'addr': 'buf:renderer+0x8', 'pointer': 'slot_minus+0x0'},
          {'addr': 'buf:renderer+0x10', 'pointer': 'slot0+0x0'},
          {'addr': 'buf:renderer+0x18', 'pointer': 'slot1+0x0'},
          {'addr': 'buf:renderer+0x20', 'bytes': u32(slot)},
          {'addr': 'buf:slot_minus+0x40', 'pointer': 'state_minus+0x0'},
          {'addr': 'buf:slot0+0x40', 'pointer': 'state0+0x0'},
          {'addr': 'buf:slot1+0x40', 'pointer': 'state1+0x0'},
          {'addr': 'buf:world+0x50', 'pointer': 'holder+0x0'},
          {'addr': 'buf:holder+0x20', 'pointer': 'list+0x0'},
          {'addr': 'buf:list+0x18', 'pointer': 'sections+0x0'},
          {'addr': 'buf:list+0x20', 'bytes': u32(sections)}]
    for i in range(sections):
        for name in ('sections', 'sections_b'):
            # Each section entry points at its own place in the other array: a distinct, known value.
            m.append({'addr': f'buf:{name}+{SECTION_SIZE * i:#x}',
                      'pointer': f"{'sections_b' if name == 'sections' else 'sections'}+{8 * (i + 1):#x}"})
    c['args']['rsi'] = 'buf:world'
    stub = {'address': DRAW_SECTION, 'argc': 4}
    writes = []
    if swap:
        writes.append({'arg': 1, 'offset': 0x18, 'pointer': 'sections_b+0x0'})
    if lower_to is not None:
        writes.append({'arg': 1, 'offset': 0x20, 'bytes': u32(lower_to)})
    if writes:
        stub['writes'] = writes
    c['stubs'] = [dict(stub) for _ in range(sections)]   # one entry per call: the count read at the start
    return c


def world_cases():
    nan, neg_zero = 0x7fc01234, 0x80000000
    return [world_case('no_renderer', renderer=False),
            world_case('already_set'),
            world_case('already_set_dirty_kept', states={'dirty': 0x5a, 'dirty_high': 0xa6}),
            world_case('setting_0x18_differs', states={'s18': 0}),
            world_case('setting_0x18_all_ones', states={'s18': -1}),
            world_case('setting_0x1c_differs', states={'s1c': 1}),
            world_case('setting_0x70_differs', states={'s70': 2}),
            world_case('setting_0x78_differs', states={'s78': 0.0}),
            world_case('setting_0x78_minus_16', states={'s78': -16.0}),
            world_case('setting_0x78_nan', states={'s78_raw': nan}),
            world_case('setting_0x78_negative_zero', states={'s78_raw': neg_zero}),
            world_case('all_differ', states={'s18': 1, 's1c': 5, 's70': 0, 's78': 1.5}),
            world_case('all_differ_dirty_kept', states={'s18': 3, 's1c': 2, 's70': 4, 's78': 32.0,
                                                        'dirty': 0x73, 'dirty_high': 0xfe}),
            world_case('all_differ_dirty_full', states={'s18': 3, 's1c': 2, 's70': 4, 's78': 32.0,
                                                        'dirty': 0xff, 'dirty_high': 0xff}),
            world_case('slot_1', slot=1, states={'s18': 0, 's78': 1.0}),
            world_case('slot_minus_one', slot=-1, states={'s1c': 1, 's70': 1}),
            world_case('no_section', sections=0, states={'s18': 0}),
            world_case('one_section', sections=1),
            world_case('five_sections', sections=5, states={'s70': 0}),
            world_case('array_swapped_by_draw', sections=3, swap=True),
            world_case('count_lowered_by_draw', sections=3, lower_to=1),
            world_case('count_raised_by_draw', sections=2, lower_to=4)]


class Points:
    """An AiPointSource and its arrays. Index -1 of every array is a guard entry."""

    def __init__(self):
        self.count_a, self.edge_count, self.vertex_count = 3, 6, 6
        self.original_faces = [(0, 3), (3, 1), (4, 2)] + [(1, 2)] * (ARRAY_LEN - 3)
        self.mapped_faces = [(1, 3), (0, 2), (4, 1), (2, 5)] + [(2, 2)] * (ARRAY_LEN - 4)
        self.added_faces = [(5, 2), (0, 4), (8, 3)] + [(3, 3)] * (ARRAY_LEN - 3)
        self.guard_face = (6, 2)
        self.face_map, self.edge_map = [], []
        self.original_edges = [0, 1, 2, 3, 4, 5, 7, 6, 9, 8, 11, 10, 13, 12, 1, 0]
        self.mapped_edges = [6, 7, 8, 9, 10, 11, 12, 13, 0, 1, 2, 3, 4, 5, 6, 7]
        self.added_edges = [13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 2, 4]
        self.guard_edge = 3
        self.original_vertices = [(1.0 + i, -2.0 * i, 0.5 * i + 0.25, 100.0 + i) for i in range(ARRAY_LEN)]
        self.added_vertices = [(-3.0 * i - 1.0, 7.5 + i, 1.0 / (i + 3), -50.0 - i) for i in range(ARRAY_LEN)]
        self.guard_vertex = (11.0, 22.0, 33.0, 44.0)
        self.transform = [(1.5, 0.25, -0.5, 0.0), (0.125, 2.0, 0.75, 0.0), (-1.0, 0.5, 3.0, 0.0),
                          (10.0, -20.0, 30.0, 1.0)]
        self.raw_vertex = {}      # (array, index) -> four raw words
        self.raw_transform = {}   # column -> four raw words

    def face_bytes(self, faces):
        out = b''
        for first, count in [self.guard_face] + faces:
            out += struct.pack('<iIh', first, 0x0badf00d, count) + b'\xee' * 6
        return out.hex()

    def edge_bytes(self, edges):
        return b''.join(struct.pack('<i', v) + b'\xdd' * (EDGE_SIZE - 4) for v in [self.guard_edge] + edges).hex()

    def vertex_bytes(self, name, vertices):
        out = b''
        for i, v in enumerate([self.guard_vertex] + vertices):
            raw = self.raw_vertex.get((name, i - GUARD))
            out += struct.pack('<4I', *raw) if raw else struct.pack('<4f', *v)
        return out.hex()

    def transform_bytes(self):
        raw = self.raw_transform
        return b''.join(struct.pack('<4I', *raw[i]) if i in raw else struct.pack('<4f', *col)
                        for i, col in enumerate(self.transform)).hex()


def point_case(cid, face_index, setup=None):
    c = base('point', cid)
    p = Points()
    if setup:
        setup(p)
    src = bytearray(b'\x99' * 0x140)
    src[0x70:0xb0] = bytes.fromhex(p.transform_bytes())
    for off, value in ((0x18, p.count_a), (0x28, p.edge_count), (0x38, p.vertex_count), (0xd8, len(p.edge_map)),
                       (0xe8, len(p.face_map)), (0x118, 0x1234)):
        src[off:off + 4] = struct.pack('<I', value & 0xffffffff)
    c['buffers'] = {
        'source': {'size': 0x140, 'bytes': src.hex()},
        'original_faces': {'size': FACE_SIZE * (ARRAY_LEN + GUARD), 'bytes': p.face_bytes(p.original_faces)},
        'mapped_faces': {'size': FACE_SIZE * (ARRAY_LEN + GUARD), 'bytes': p.face_bytes(p.mapped_faces)},
        'added_faces': {'size': FACE_SIZE * (ARRAY_LEN + GUARD), 'bytes': p.face_bytes(p.added_faces)},
        'original_edges': {'size': EDGE_SIZE * (ARRAY_LEN + GUARD), 'bytes': p.edge_bytes(p.original_edges)},
        'mapped_edges': {'size': EDGE_SIZE * (ARRAY_LEN + GUARD), 'bytes': p.edge_bytes(p.mapped_edges)},
        'added_edges': {'size': EDGE_SIZE * (ARRAY_LEN + GUARD), 'bytes': p.edge_bytes(p.added_edges)},
        'original_vertices': {'size': VERTEX_SIZE * (ARRAY_LEN + GUARD),
                              'bytes': p.vertex_bytes('original', p.original_vertices)},
        'added_vertices': {'size': VERTEX_SIZE * (ARRAY_LEN + GUARD),
                           'bytes': p.vertex_bytes('added', p.added_vertices)},
        'face_map': {'size': 4 * (ARRAY_LEN + GUARD), 'bytes': struct.pack(f'<{ARRAY_LEN + GUARD}i', *(
            [MAP_GUARD] + p.face_map + [5] * (ARRAY_LEN - len(p.face_map)))).hex()},
        'edge_map': {'size': 4 * (ARRAY_LEN + GUARD), 'bytes': struct.pack(f'<{ARRAY_LEN + GUARD}i', *(
            [MAP_GUARD] + p.edge_map + [9] * (ARRAY_LEN - len(p.edge_map)))).hex()},
        'frame': {'size': 0x30, 'bytes': 'c3' * 0x30}}
    for off, name, size in ((0x10, 'original_faces', FACE_SIZE), (0x20, 'original_edges', EDGE_SIZE),
                            (0x30, 'original_vertices', VERTEX_SIZE), (0xd0, 'edge_map', 4), (0xe0, 'face_map', 4),
                            (0xf0, 'mapped_faces', FACE_SIZE), (0x100, 'mapped_edges', EDGE_SIZE),
                            (0x110, 'added_faces', FACE_SIZE), (0x120, 'added_edges', EDGE_SIZE),
                            (0x130, 'added_vertices', VERTEX_SIZE)):
        c['memory'].append({'addr': f'buf:source+{off:#x}', 'pointer': f'{name}+{size * GUARD:#x}'})
    c['args'] = {'rdi': 'buf:source', 'rsi': face_index & 0xffffffff, 'rdx': 'buf:frame'}
    return c


def setter(**kw):
    def apply(p):
        for k, v in kw.items():
            setattr(p, k, v)
    return apply


def with_face(table, index, first, count, **kw):
    def apply(p):
        faces = list(getattr(p, table))
        faces[index] = (first, count)
        setattr(p, table, faces)
        for k, v in kw.items():
            setattr(p, k, v)
    return apply


def nan_payloads(p, edges=1, vertex=None):
    # Every column has a NaN in one lane (the translation in w) and the vertices' x, y and z are
    # NaNs with their own payloads: where two NaNs meet, the operand order picks the payload.
    p.raw_transform = {0: (0x7fc00011, 0x3f800000, 0x40000000, 0x40400000),
                       1: (0x3f800000, 0x7fc00022, 0x3f000000, 0x3e800000),
                       2: (0x40800000, 0x3f800000, 0x7fc00033, 0x3f800000),
                       3: (0x41200000, 0xc1a00000, 0x41f00000, 0x7fc00044)}
    p.raw_vertex = {('original', 0): (0x7fc000a1, 0x7fc000b2, 0x7fc000c3, 0x7fc000d4),
                    ('original', 1): (0x7fc000e5, 0x7fc000f6, 0x7fc00107, 0x7fc00118)}
    if vertex:
        p.raw_vertex = {('original', 0): vertex}
    p.mapped_edges = list(range(ARRAY_LEN))
    p.mapped_faces = [(0, edges)] + p.mapped_faces[1:]


def cases():
    out = [point_case('empty_face_map_picks_mapped', 0),
           point_case('empty_face_map_index_2', 2),
           point_case('face_map_keeps_original', 1, setter(face_map=[3, -1, 0])),
           point_case('face_map_picks_copy', 0, setter(face_map=[3, -1, 0])),
           point_case('face_map_picks_copy_0', 2, setter(face_map=[3, -1, 0])),
           point_case('added_face_first', 3),
           point_case('added_face_second', 4),
           point_case('added_face_far', 6),
           point_case('face_index_minus_one', -1),
           point_case('face_index_minus_one_with_map', -1, setter(face_map=[3, -1, 0])),
           point_case('count_a_zero', 0, setter(count_a=0)),
           point_case('edge_map_keeps_original', 1, setter(face_map=[-1, -1, -1], edge_map=[-1, 2, -1, 4, -1, -1])),
           point_case('edge_map_picks_copies', 0, setter(face_map=[-1, -1, -1], edge_map=[5, 2, 7, 4, 1, 0])),
           point_case('edge_map_mixed', 0, setter(edge_map=[5, -1, 7, -1, 1, -1])),
           point_case('edges_cross_into_added', 0, with_face('mapped_faces', 0, 4, 5)),
           point_case('added_edges_only', 0, with_face('mapped_faces', 0, 7, 3)),
           point_case('edge_index_minus_one', 0, with_face('mapped_faces', 0, -1, 2)),
           point_case('edge_index_minus_one_with_map', 0,
                      with_face('mapped_faces', 0, -1, 2, edge_map=[-1, -1, -1, -1, -1, -1])),
           point_case('edge_count_zero_nan', 0, with_face('mapped_faces', 0, 2, 0)),
           point_case('edge_count_one', 0, with_face('mapped_faces', 0, 2, 1)),
           point_case('edge_count_five', 0, with_face('mapped_faces', 0, 1, 5)),
           point_case('edge_count_seven', 0, with_face('mapped_faces', 0, 2, 7)),
           point_case('edge_count_negative', 0, with_face('mapped_faces', 0, 2, -3)),
           point_case('edge_count_most_negative', 0, with_face('mapped_faces', 0, 2, -0x8000)),
           point_case('vertex_counts_shifted', 0, setter(vertex_count=2, edge_count=3)),
           point_case('vertex_minus_one', 0, setter(mapped_edges=[-1, 2, -1] + [0] * (ARRAY_LEN - 3))),
           point_case('vertex_count_zero', 0, setter(vertex_count=0)),
           point_case('nan_payloads', 0, nan_payloads),
           point_case('nan_payloads_two_edges', 0, lambda p: nan_payloads(p, edges=2)),
           # z finite, so the x and y payloads are not hidden behind z's; then y alone.
           point_case('nan_payloads_x_y', 0,
                      lambda p: nan_payloads(p, vertex=(0x7fc000a1, 0x7fc000b2, 0x40000000, 0))),
           point_case('nan_payloads_y', 0,
                      lambda p: nan_payloads(p, vertex=(0x3f800000, 0x7fc000b2, 0x40000000, 0))),
           point_case('infinities', 0, setter(transform=[(float('inf'), 0.0, 1.0, 0.0), (0.0, float('-inf'), 1.0, 0.0),
                                                         (1.0, 1.0, 1.0, 0.0), (0.0, 0.0, 0.0, 0.0)])),
           point_case('large_values', 1, setter(transform=[(3e38, 1e-38, -3e38, 1.0), (2.0, 2.0, 2.0, 2.0),
                                                           (-1e30, 1e30, 0.0, 5.0), (1e-45, -1e-45, 0.0, -0.0)])),
           point_case('other_transform', 4, setter(transform=[(0.0, 1.0, 0.0, 9.0), (1.0, 0.0, 0.0, 9.0),
                                                              (0.0, 0.0, -1.0, 9.0), (-4.0, 0.5, 2.0, 9.0)]))]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = world_cases() if sys.argv[2] == 'world' else cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
