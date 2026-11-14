#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the physics instance constructor's helpers (game/physics/instance_construct_helpers.cpp),
for tools/verify.py run.

usage: instance_construct_helpers_cases.py OUT_DIR defaults|tracker|transform|shape|summary|root

Objects are named buffers filled with a pattern, so every byte a function leaves or writes is
compared. The original callees are stubbed (their arguments and the memory they see recorded);
the transform also runs its real quaternion callee.
- defaults: a setup full of a pattern, already clear, and one at an unaligned address.
- tracker: shape flag counts 0, negative, 1, 3 and 9; a reserve that leaves entries (only those
  from the current size up get 1), one that hands back a new buffer.
- transform: stubbed rotation; the real callee over identity, half turns (each diagonal branch),
  NaN and -0 axes; a position with NaN payloads and -0.
- shape: modes 0, 1, -1 (upper register bits not part of it), null set and table, a pattern in
  the object (the byte at +0x38 and the padding are kept).
- summary: counts 0, negative, 1, 3; the count read before the refresh, the entries read after it;
  NaN payloads on either side and in a row, -0/+0 ties, infinities, values beyond the start bounds.
- root: weights 1.5, 0, -0, -1, infinity, a denormal and NaN payloads, with other lanes of xmm0 set.
"""
import json
import os
import struct
import sys

ADDRESSES = {'defaults': '0x00ae6dc0', 'tracker': '0x00ae6d00', 'transform': '0x0083d6d0', 'shape': '0x00ac46a0',
             'summary': '0x00ac5e00', 'root': '0x00ae6180'}
GUARD_GOT = '0x057e3be0'
SHAPE_HEAP = '0x058018b0'
NAN_A, NAN_B, NAN_NEG = '0100c07f', '0200c0ff', '0000c0ff'
PATTERN = 'cd'


def f32s(*v):
    return b''.join(bytes.fromhex(x) if isinstance(x, str) else struct.pack('<f', x) for x in v).hex()


def h(fmt, *v):
    return struct.pack(fmt, *v).hex()


class Case:
    def __init__(self, variant, cid):
        self.variant, self.cid = variant, cid
        self.args, self.buffers, self.memory, self.stubs = {}, {}, [], []
        self.buf('guard', 0x10, '5a' * 0x10)
        self.memory.append({'addr': GUARD_GOT, 'pointer': 'guard'})

    def buf(self, name, size, fill=PATTERN):
        self.buffers[name] = {'size': size, 'bytes': (fill * size)[:2 * size] if len(fill) == 2 else fill}
        return name

    def put(self, name, off, data):
        self.memory.append({'addr': f'buf:{name}+{off:#x}', 'bytes': data})

    def ptr(self, name, off, target):
        self.memory.append({'addr': f'buf:{name}+{off:#x}', 'pointer': target})

    def stub(self, address, argc, **kw):
        self.stubs.append({'address': address, 'argc': argc, **kw})

    def json(self):
        return {'schema': 1, 'address': ADDRESSES[self.variant], 'id': self.cid, 'returns': 'void', 'args': self.args,
                'buffers': self.buffers, 'memory': self.memory, 'imports': [], 'stubs': self.stubs}


def defaults_cases():
    out = []
    for cid, fill, off in (('pattern', PATTERN, 0), ('already_clear', '00', 0), ('unaligned', 'ab', 8)):
        c = Case('defaults', cid)
        c.buf('setup', 0x30, fill)
        c.args = {'rdi': f'buf:setup+{off:#x}'}
        out.append(c)
    return out


def tracker_case(cid, count, reserved_size=0, new_buffer=False, pair='buf:pair+0x10'):
    c = Case('tracker', cid)
    c.buf('self', 0x50)
    c.buf('setup', 0x20, 'ee')
    c.buf('shapes', 0x40, '77')
    c.put('shapes', 0x30, h('<i', count))
    c.buf('pair', 0x20, '11')
    c.ptr('setup', 0, 'shapes')
    if pair.startswith('buf:'):
        c.ptr('setup', 8, pair[4:])
    else:
        c.put('setup', 8, h('<Q', int(pair, 16)))
    c.buf('data', 4 * max(count, 1) + 0x10, '99')
    c.args = {'rdi': 'buf:self', 'rsi': 'buf:setup'}
    if count > 0:
        writes = [{'arg': 1, 'offset': 0, 'pointer': 'data'},
                  {'arg': 1, 'offset': 8, 'bytes': h('<iI', reserved_size, count)}]
        if new_buffer:
            writes[1] = {'arg': 1, 'offset': 8, 'bytes': h('<iI', reserved_size, 2 * count)}
        c.stub('0x00819040', 4, argmem=[{'arg': 1, 'size': 16}], writes=writes)
    return c


def tracker_cases():
    return [tracker_case('count_0', 0), tracker_case('count_negative', -3), tracker_case('count_1', 1),
            tracker_case('count_3', 3), tracker_case('count_9_new_buffer', 9, new_buffer=True),
            tracker_case('reserve_leaves_two', 5, reserved_size=2), tracker_case('reserve_leaves_all', 3, reserved_size=3),
            tracker_case('reserve_leaves_more', 2, reserved_size=4), tracker_case('null_pair', 2, pair='0x0'),
            tracker_case('negative_large', -0x80000000)]


AXES_IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)


def transform_case(cid, axes=AXES_IDENTITY, position=(1.0, -2.0, 3.5, 7.0), real=False, out_off=0):
    c = Case('transform', cid)
    c.buf('out', 0x40)
    c.buf('frame', 0x40, '33')
    c.put('frame', 0, f32s(*axes))
    c.put('frame', 0x30, f32s(*position))
    c.args = {'rdi': f'buf:out+{out_off:#x}', 'rsi': 'buf:frame'}
    if not real:
        c.stub('0x00849900', 2, argmem=[{'arg': 1, 'size': 0x40}, {'arg': 0, 'size': 0x10}],
               writes=[{'arg': 0, 'bytes': f32s(0.25, -0.5, NAN_A, -0.0)}])
    return c


def transform_cases():
    half_x = (1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0)
    half_y = (-1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0)
    half_z = (-1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    quarter = (0.0, 1.0, 0.0, 9.0, -1.0, 0.0, 0.0, 9.0, 0.0, 0.0, 1.0, 9.0)
    return [transform_case('stubbed'),
            transform_case('stubbed_position_nan', position=(NAN_A, -0.0, NAN_B, float('inf'))),
            transform_case('stubbed_unaligned_pattern', out_off=0x10),
            transform_case('real_identity', real=True),
            transform_case('real_half_turn_x', half_x, real=True),
            transform_case('real_half_turn_y', half_y, real=True),
            transform_case('real_half_turn_z', half_z, real=True),
            transform_case('real_quarter_turn_z', quarter, real=True),
            transform_case('real_nan_axes', (NAN_A,) + AXES_IDENTITY[1:5] + (NAN_B,) + AXES_IDENTITY[6:], real=True),
            transform_case('real_negative_zero', (-0.0,) * 12, position=(-0.0, 0.0, -0.0, NAN_NEG), real=True)]


def shape_case(cid, mode=0, rsi_high=0, shapes='buf:shapes', table='buf:table', fill=PATTERN):
    c = Case('shape', cid)
    c.buf('self', 0x60, fill)
    c.buf('shapes', 0x40, '44')
    c.buf('table', 0x10, '55')
    c.args = {'rdi': 'buf:self', 'rsi': hex((rsi_high << 32) | (mode & 0xffffffff)), 'rdx': shapes, 'rcx': table}
    c.stub('0x00ac4700', 4, argi=[0, 2, 3], argu32=[1], argmem=[{'arg': 0, 'size': 0x60}])
    return c


def shape_cases():
    return [shape_case('mode_0'), shape_case('mode_1', 1), shape_case('mode_negative', -1),
            shape_case('mode_high_bits', 2, rsi_high=0xdeadbeef), shape_case('null_set_and_table', shapes='0x0', table='0x0'),
            shape_case('zero_object', fill='00'), shape_case('ones_object', fill='ff')]


def summary_case(cid, rows, count=None, refreshed_rows=None, refreshed_count=None):
    """rows: each entry's first row (four floats or hex lanes). refreshed_rows: the entries the
    refresh puts in place; refreshed_count: a count the refresh writes over the set's."""
    c = Case('summary', cid)
    c.buf('self', 0xa0)
    c.buf('out', 0x30, 'ee')
    # The set lives inside the object's buffer (+0x60), so the stubbed refresh can change its count.
    c.ptr('self', 0, 'self+96')
    c.put('self', 0x90, h('<i', len(rows) if count is None else count))
    n = max(len(rows), len(refreshed_rows or ()), 1)
    for name, entries in (('entries', rows), ('fresh', refreshed_rows or ())):
        c.buf(name, 0x30 * n + 0x10, '22')
        for i, row in enumerate(entries):
            c.put(name, 0x30 * i, f32s(*row))
    c.ptr('self', 0x18, 'entries')
    writes = []
    if refreshed_rows is not None:
        writes.append({'arg': 0, 'offset': 0x18, 'pointer': 'fresh'})
    if refreshed_count is not None:
        writes.append({'arg': 0, 'offset': 0x90, 'bytes': h('<i', refreshed_count)})
    c.stub('0x00ac4f20', 1, argmem=[{'arg': 0, 'size': 0x20}], writes=writes)
    c.args = {'rdi': 'buf:self', 'rsi': 'buf:out+0x10'}
    return c


def summary_cases():
    inf = float('inf')
    return [
        summary_case('count_0', []),
        summary_case('count_negative', [(1.0, 2.0, 3.0, 4.0)], count=-2),
        summary_case('count_1', [(1.0, -2.0, 3.0, -4.0)]),
        summary_case('count_3', [(1.0, -2.0, 3.0, -4.0), (-5.0, 6.0, 0.5, 0.25), (2.0, 2.0, -7.0, 9.0)]),
        summary_case('nan_first', [(NAN_A, 1.0, NAN_NEG, 2.0), (3.0, 4.0, 5.0, 6.0)]),
        summary_case('nan_last', [(3.0, 4.0, 5.0, 6.0), (NAN_A, 1.0, NAN_B, 2.0)]),
        summary_case('nan_twice', [(NAN_A, NAN_B, 1.0, 1.0), (NAN_B, NAN_A, 1.0, 1.0)]),
        summary_case('signed_zero_ties', [(0.0, -0.0, 0.0, -0.0), (-0.0, 0.0, -0.0, 0.0)]),
        summary_case('infinities', [(inf, -inf, inf, -inf), (1.0, 1.0, 1.0, 1.0)]),
        summary_case('at_start_bounds', [(3.4028234663852886e38, -3.4028234663852886e38, 0.0, 0.0)]),
        summary_case('refresh_replaces_entries', [(1.0, 1.0, 1.0, 1.0)] * 2, refreshed_rows=[(-9.0, 9.0, 0.5, -0.5)] * 2),
        summary_case('refresh_changes_count', [(1.0, 2.0, 3.0, 4.0), (-1.0, -2.0, -3.0, -4.0), (8.0, 8.0, 8.0, 8.0)],
                     count=1, refreshed_count=3),
    ]


def root_case(cid, weight, upper='aabbccdd' * 3):
    c = Case('root', cid)
    c.buf('root', 0x50, '66')
    c.buf('posevalue', 0x10, '77')
    c.buf('parts', 0x10, '88')
    c.args = {'rdi': 'buf:root', 'xmm0': f32s(weight) + upper}
    c.stub('0x00ae6620', 2, writes=[{'arg': 0, 'offset': 0, 'pointer': 'posevalue+0x4'},
                                    {'arg': 0, 'offset': 8, 'pointer': 'parts'},
                                    {'arg': 0, 'offset': 0x10, 'bytes': h('<iI', 2, 0x80000002)}])
    c.stub('0x00ae61f0', 3, argmem=[{'arg': 2, 'size': 0x10}])
    c.stub('0x00ae6510', 1, argmem=[{'arg': 0, 'size': 0x18}])
    return c


def root_cases():
    return [root_case('weight_1_5', 1.5), root_case('weight_0', 0.0, upper='00' * 12), root_case('weight_negative_zero', -0.0),
            root_case('weight_negative', -1.0), root_case('weight_infinity', float('inf')),
            root_case('weight_denormal', '01000000'), root_case('weight_nan', NAN_A), root_case('weight_nan_negative', NAN_B)]


CASES = {'defaults': defaults_cases, 'tracker': tracker_cases, 'transform': transform_cases, 'shape': shape_cases,
         'summary': summary_cases, 'root': root_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in CASES:
        print(__doc__, file=sys.stderr)
        return 2
    out = [c.json() for c in CASES[sys.argv[2]]()]
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
