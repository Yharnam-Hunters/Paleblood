#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for character_frame_update_01cbdb20 (0x01cbdb20), for tools/verify.py run.

usage: frame_update_cases.py OUT_DIR

Every input sits in a named buffer, as the recordings lay them out. The virtual methods (+0x210
rate factor, +0x168 linked object) and the thread allocator's methods (+0x20 info, +0x70 free)
point at spare addresses that the cases stub. Covers the throw events, the regeneration (rate
and fraction rounding, negative rates with their skip flags, the -50 floor and the maximum,
NaN and huge rates), the linked float with and without its source, the name string (none, both
name tables, empty, long enough to be freed) and the allocator check.
"""
import json
import os
import struct
import sys

GETTER_RATE, GETTER_LINKED, HEAP_INFO, HEAP_FREE = 0x00500000, 0x00500100, 0x00500200, 0x00500300
TLS_SLOT = -0x730


def f32(v):
    return v if isinstance(v, str) else struct.pack('<f', v).hex()


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def i32(v):
    return struct.pack('<i', v).hex()


def case(cid, dt=1 / 30, throw=None, flags5c=0, rate_byte=100, mult=1.0, factor=1.0, frac=0.0,
         points=100, maximum=200, drop_flags=0, world_dbg=True, no_drop=0, linked='source', target280=True,
         bits1e1=0, anim_state=6 + 1, name='table0', info=0x20, assign_cap=7, extra=0):
    """throw: None (no owner) or (anim flag, request flag, kind); name: none / table0 / table1 / empty."""
    b = {'self': 0x3b8, 'vt': 0x218, 'a': 0x288, 'params': 0x88, 'modules': 0x90, 'request': 0x58, 'owner1': 0x3b8,
         'owner2': 0x38, 'data': 0x204}
    m = [('self', 0, 'p', 'vt'), ('self', 0xe0, f32(dt)), ('self', 0x58, 'p', 'a'), ('a', 0x38, 'p', 'params'),
         ('self', 0x3b0, 'p', 'modules'), ('modules', 0x58, struct.pack('<Q', 0x1234560).hex()),
         ('modules', 0x88, 'p', 'request'), ('request', 8, 'p', 'owner1'), ('owner1', 0x3b0, 'p', 'owner2'),
         ('modules', 0x20, 'p', 'data'), ('modules', 0x28, struct.pack('<Q', 0x2345670).hex()),
         ('modules', 0x68, struct.pack('<Q', 0x3456780).hex())]
    stubs = [{'address': '0x01e59250', 'argc': 1}]
    imports = []
    if throw:
        anim, req, kind = throw
        b['anim'] = 0x388
        m += [('owner2', 0x30, 'p', 'anim'), ('anim', 0x384, f'{anim:02x}'), ('request', 0x53, f'{req:02x}'),
              ('request', 0x54, u32(kind))]
        if anim and req and kind in (1, 2):
            stubs.append({'address': '0x01e19e20', 'argc': 2})
    m += [('params', 0x5c, f'{flags5c:02x}'), ('params', 0x34, f'{rate_byte:02x}'), ('self', 0x16c, f32(mult)),
          ('self', 0x168, f32(frac)), ('data', 0x134, i32(points)), ('data', 0x138, i32(maximum)),
          ('data', 0x200, f'{drop_flags:02x}'), ('0x0593e88a', None, f'{no_drop:02x}'),
          ('0x0593e880', None, '0100000000000000' if world_dbg else '0000000000000000'),
          ('vt', 0x210, 'g', GETTER_RATE), ('vt', 0x168, 'g', GETTER_LINKED)]
    if not flags5c & 2:
        ret = '0x' + (bytes.fromhex(factor)[::-1].hex() if isinstance(factor, str) else struct.pack('>f', factor).hex())
        stubs.append({'address': f'0x{GETTER_RATE:08x}', 'argc': 1, 'ret': ret})
        if not world_dbg:
            stubs.append({'address': '0x024b55b0', 'argc': 4})
    stubs += [{'address': '0x01e5c440', 'argc': 2, 'argmem': [{'arg': 1, 'size': 12}]}, {'address': '0x01e4a490', 'argc': 1}]
    if linked:
        b['linked'] = 16
        b['target'] = 0x84
        stubs += [{'address': f'0x{GETTER_LINKED:08x}', 'argc': 1, 'ret': 'buf:linked'}] * 2
        m.append(('a', 0x280 if target280 else 0x80, 'p', 'target'))
        if linked == 'source':
            b['linked_source'] = 0x90
            m += [('linked', 8, 'p', 'linked_source'), ('linked_source', 0x8c, f32(0.75))]
    else:
        stubs.append({'address': f'0x{GETTER_LINKED:08x}', 'argc': 1, 'ret': '0x0'})
    m.append(('self', 0x1e1, f'{bits1e1:02x}'))
    if anim_state is not None:
        b['anim_state'] = 0xd9
        m += [('self', 0x288, 'p', 'anim_state'), ('anim_state', 0xd8, f'{anim_state:02x}')]
    builds = not bits1e1 & 0xa0 and anim_state is not None and anim_state != 6
    gs = None
    if builds:
        below = -TLS_SLOT
        b.update({'tls': below + 8, 'heap': 0x30, 'heap_vt': 0x78})
        m += [('0x057e4568', None, struct.pack('<q', TLS_SLOT).hex()), ('tls', below, 'p', f'tls+{below}'),
              ('tls', 0, 'p', 'heap'), ('heap', 0x28, 'p', 'heap_vt'), ('heap_vt', 0x20, 'g', HEAP_INFO),
              ('heap_vt', 0x70, 'g', HEAP_FREE)]
        gs = f'buf:tls+{below}'
        stubs.append({'address': f'0x{HEAP_INFO:08x}', 'argc': 3, 'writes': [{'arg': 0, 'bytes': f'{info:02x}'}]})
        if not info & 0x20:
            stubs.append({'address': '0x024b55b0', 'argc': 3})
        if name == 'none':
            imports.append({'name': 'wcslen', 'argc': 1, 'ret': 4})
        else:
            b.update({'name': 0x28, 'name_def': 0x90, 'name_version': 4, 'names': 16, 'text': 2})
            m += [('self', 0x350, 'p', 'name'), ('name', 0x18, 'p', 'name_def'), ('name_def', 0x88, 'p', 'name_version'),
                  ('name_version', 0, u32({'table1': 5, 'table_v3': 3}.get(name, 2))), ('name', 0x20, 'p', 'names'),
                  ('names', 8 if name in ('table1', 'table_v3') else 0, 'p', 'text'), ('text', 0, '0000' if name == 'empty' else '6300')]
            if name != 'empty':
                imports.append({'name': 'wcslen', 'argc': 1, 'ret': 5})
        heap_text = struct.pack('<Q', 0x7654320).hex() + '00' * 8
        written = heap_text + struct.pack('<QQ', 12, assign_cap).hex() if assign_cap >= 8 else \
            '63003000300030003000000000000000' + struct.pack('<QQ', 5, 7).hex()
        stubs.append({'address': '0x02a2a310', 'argc': 3, 'argmem': [{'arg': 0, 'offset': 8, 'size': 2}, {'arg': 0, 'offset': 24, 'size': 25}],
                      'writes': [{'arg': 0, 'offset': 8, 'bytes': written}]})
        if assign_cap >= 8:
            stubs.append({'address': f'0x{HEAP_FREE:08x}', 'argc': 2})
    m.append(('params', 0x74, bytes(range(1, 21)).hex()))
    m.append(('self', 0x1f8, struct.pack('<Q', extra).hex()))
    if extra:
        stubs.append({'address': '0x01cd92b0', 'argc': 1})
    stubs.append({'address': '0x01913c10', 'argc': 1, 'argf32': [0]})
    memory = []
    for e in m:
        where = e[0] if e[1] is None else f'buf:{e[0]}+{e[1]}'
        if len(e) == 3:
            memory.append({'addr': where, 'bytes': e[2]})
        elif e[2] == 'p':
            memory.append({'addr': where, 'pointer': e[3]})
        else:
            memory.append({'addr': where, 'guest': f'0x{e[3]:08x}'})
    c = {'schema': 1, 'address': '0x01cbdb20', 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:self'},
         'buffers': {k: {'size': v} for k, v in b.items()}, 'memory': memory, 'stubs': stubs, 'imports': imports}
    if gs:
        c['gs'] = gs
    return c


CASES = [
    case('typical'),
    case('throw_attack', throw=(1, 1, 1)),
    case('throw_defence', throw=(1, 1, 2)),
    case('throw_other_kind', throw=(1, 1, 3)),
    case('throw_not_requested', throw=(1, 0, 1)),
    case('throw_anim_off', throw=(0, 1, 2)),
    case('regen_disabled', flags5c=2),
    case('regen_fraction_carries', rate_byte=7, mult=0.9, factor=1.3, frac=0.95),
    case('regen_hits_maximum', points=199, rate_byte=255, mult=4.0),
    case('regen_maximum_below_current', points=300, maximum=200, rate_byte=1),
    case('negative_rate', factor=-2.0, rate_byte=200, points=10),
    case('negative_rate_floor', factor=-50.0, rate_byte=255, points=-40),
    case('negative_rate_blocked_flag', factor=-2.0, drop_flags=0x80),
    case('negative_rate_debug_no_drop', factor=-2.0, no_drop=1),
    case('negative_rate_singleton_missing', factor=-2.0, world_dbg=False),
    case('negative_fraction', factor=-0.3, rate_byte=3, frac=-0.5),
    case('nan_factor', factor=float('nan')),
    case('huge_rate', factor=3.0e9),
    case('points_overflow', points=0x7ffffff0, maximum=0x7fffffff, rate_byte=255, mult=100.0),
    case('linked_without_source', linked='nosource'),
    case('linked_none', linked=None),
    case('linked_target_fallback', target280=False),
    case('name_skipped_flags', bits1e1=0x20),
    case('name_skipped_flag80', bits1e1=0x80),
    case('name_skipped_state6', anim_state=6),
    case('name_no_anim_state', anim_state=None),
    case('name_none', name='none'),
    case('name_table1', name='table1'),
    case('name_empty', name='empty'),
    case('name_freed', assign_cap=15),
    case('heap_check_fails', info=0x00),
    case('extra_module', extra=0x4567890),
    # Both factors NaN with different payloads: the product keeps the first one's.
    case('nan_payloads', mult='0100c07f', factor='0200c07f'),
    # A maximum below the floor: exactly -50 is kept against the maximum, below it is the floor.
    case('floor_exact_low_maximum', dt=1.0, rate_byte=100, factor=-5.0, points=-45, maximum=-60),
    case('below_floor_low_maximum', dt=1.0, rate_byte=100, factor=-6.0, points=-45, maximum=-60),
    case('name_table_version3', name='table_v3'),
    case('name_capacity8', assign_cap=8),
    case('zero_dt', dt=0.0, rate_byte=50),
    case('long_dt', dt=0.25, rate_byte=90, mult=1.7),
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
