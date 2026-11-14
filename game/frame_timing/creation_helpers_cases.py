#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the gate's creation helpers (game/frame_timing/creation_helpers.cpp), for
tools/verify.py run.

usage: creation_helpers_cases.py OUT_DIR fill|create

- fill (0x0231fbc0): no record (the word is left all ones, nothing past it written), numbers that
  are not negative around every two-digit group's edge and up to INT32_MAX, and negative numbers
  (-1, -100, INT32_MIN) replaced from the 16-bit value (0, 1, 2016, 0xffff) and the count (0, 1, 2,
  3, 0x80, 0xff) with limits at 0, 1, 2, around the count, 0x7fffffff, the sign bit and all ones
  (the comparison is unsigned). The record's other bytes are set, so a wider read shows, and the
  limit's register carries high bits the original ignores.
- create (0x02265580): FrpgNetMan missing until the report stores it, and each of its four bytes
  refusing (with other nonzero values), which leaves null and makes no call; the clock failing,
  a tick of 0, the packing failing (also with a nonzero result whose low byte is 0) or packing
  dates with and without the top bit; reference counts of 0 (the last reference is dropped and the
  handle destroyed), 1, 5 and -1 (the drop is reported); and an entry start that leaves another
  pointer in its slot (the reference taken is still the one dropped). The heap's allocate slot,
  the constructor, the list append, the entry's maker and start, the date packing, the destroy
  slot and the report are stubbed; the stamp, the date and the packing's zeroed block are compared
  by content. Not covered: a failed allocation, after which the original writes through null.
"""
import json
import os
import struct
import sys

ADDRESSES = {'fill': '0x0231fbc0', 'create': '0x02265580'}
RETURNS = {'fill': 'i64', 'create': 'i64'}

# fill
RECORD_SIZE, VALUE_002, COUNT_005, NUMBER = 0x20, 0x2, 0x5, 0x18

# create
FRPG_NET_MAN, FATAL_ERROR, GUARD_GOT, HANDLE_HEAP = '0x0593b120', '0x024b55b0', '0x057e3be0', '0x05940438'
REPORT_FILE = 0x04d3b369  # the singleton check's source file name, the report's first argument
CONSTRUCT, APPEND, MAKE_ENTRY, START, PACK_DATE = '0x02267200', '0x02266b50', '0x02264640', '0x0226a130', '0x024ee200'
ALLOCATE, DESTROY = '0x00400090', '0x00400080'  # unused guest addresses: vtable slots, stubbed
ALLOCATE_SLOT = 0x58
FLAG_OFFSETS = (0x8, 0x9f6, 0x9f8, 0xa50)
ALLOWED = (1, 0, 0, 0)
NET_HANDLES, NET_SIZE = 0xca8, 0xcb0
LIST = 0x50
HANDLE_SIZE, REFERENCES = 0x130, 0x8
DATE = bytes(range(0x31, 0x41)).hex()  # what the clock writes: 16 distinct bytes


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def fill_case(cid, number=None, value=0, count=0, limit=0, filler=0xa5, limit_high=0x12345678):
    c = base('fill', cid)
    c['args'] = {'rdi': 'buf:out', 'rsi': 'buf:source', 'rdx': (limit_high << 32) | (limit & 0xffffffff)}
    c['buffers'] = {'out': {'size': 0x10, 'bytes': '5a' * 0x10}, 'source': {'size': 0x18, 'bytes': '3c' * 0x18}}
    if number is None:
        c['memory'] = [{'addr': 'buf:source+8', 'bytes': '00' * 8}]
        return c
    record = bytearray([filler] * RECORD_SIZE)
    record[VALUE_002:VALUE_002 + 2] = struct.pack('<H', value)
    record[COUNT_005] = count
    record[NUMBER:NUMBER + 4] = struct.pack('<i', number)
    c['buffers']['record'] = {'size': RECORD_SIZE, 'bytes': record.hex()}
    c['memory'] = [{'addr': 'buf:source+8', 'pointer': 'record'}]
    return c


def fill_cases():
    out = [fill_case('no_record'), fill_case('no_record_limit_all_ones', limit=0xffffffff)]
    for n in (0, 1, 9, 10, 99, 100, 101, 9999, 10000, 10001, 999999, 1000000, 1000001, 12345678, 20160324,
              99999999, 100000000, 123456789, 0x40000000, 0x7fffffff):
        out.append(fill_case(f'number_{n}', number=n, value=0x1234, count=7, limit=3))
    out.append(fill_case('number_12345678_filler_0', number=12345678, filler=0))
    for value in (0, 1, 2016, 0xffff):
        for count in (0, 1, 2, 3, 0x80, 0xff):
            for limit in (0, 1, 2, 0x7f, 0xfe, 0x7fffffff, 0x80000000, 0xffffffff):
                out.append(fill_case(f'negative_value_{value:#x}_count_{count:#x}_limit_{limit:#x}', number=-1,
                                     value=value, count=count, limit=limit))
    for n in (-100, -0x80000000):
        for count, limit in ((5, 2), (5, 0xffffffff), (0, 0)):
            out.append(fill_case(f'number_{n}_count_{count}_limit_{limit:#x}', number=n, value=2016, count=count,
                                 limit=limit))
    out.append(fill_case('negative_filler_0', number=-1, value=7, count=9, limit=4, filler=0))
    out.append(fill_case('negative_limit_high_zero', number=-1, value=7, count=9, limit=4, limit_high=0))
    return out


def create_case(cid, net='present', flags=ALLOWED, clock=0, tick=0x00dcbd8f2bfe5400, pack=1,
                packed=0x0000000312345678, references=1, start_swaps=False, tick_status=0):
    """net: 'present' or 'late' (missing until the report, which stores it). clock: the clock's
    status (nonzero fails). pack: the packing's result (None: not called, as for a tick of 0)."""
    c = base('create', cid)
    c['args'] = {'rdi': 'buf:out', 'rsi': 'buf:levels'}
    c['buffers'] = {'out': {'size': 0x10, 'bytes': '77' * 0x10},
                    'levels': {'size': 0x10, 'bytes': d(9) + d(3) + d(0) + d(1)},
                    'net': {'size': NET_SIZE}, 'handles': {'size': 0x80, 'bytes': '4e' * 0x80},
                    'heap': {'size': 8}, 'heap_vt': {'size': 0x80},
                    'handle': {'size': HANDLE_SIZE, 'bytes': '68' * HANDLE_SIZE}, 'handle_vt': {'size': 0x10},
                    'entry': {'size': 0xe0}, 'other': {'size': 0x10}, 'guard': {'size': 8}}
    memory = [{'addr': GUARD_GOT, 'pointer': 'guard'}, {'addr': 'buf:guard', 'bytes': q(0x0123456789abcdef)},
              {'addr': HANDLE_HEAP, 'pointer': 'heap'}, {'addr': 'buf:heap', 'pointer': 'heap_vt'},
              {'addr': f'buf:heap_vt+{ALLOCATE_SLOT}', 'guest': ALLOCATE},
              {'addr': 'buf:handle', 'pointer': 'handle_vt'}, {'addr': 'buf:handle_vt', 'guest': DESTROY},
              {'addr': f'buf:handle+{REFERENCES}', 'bytes': d(references)},
              {'addr': f'buf:net+{NET_HANDLES}', 'pointer': 'handles'}]
    memory += [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip(FLAG_OFFSETS, flags)]
    memory.append({'addr': FRPG_NET_MAN, 'pointer': 'net'} if net == 'present'
                  else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8})
    c['memory'] = memory
    stubs = []
    if net == 'late':
        # Stubs write only through their arguments: the report's first one is the source file's
        # name in the image, so the singleton's slot is reached from it.
        stubs.append({'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3],
                      'writes': [{'arg': 0, 'offset': int(FRPG_NET_MAN, 16) - REPORT_FILE, 'pointer': 'net'}]})
    if not flags[0] or any(flags[1:]):  # refused: null, and no call
        c['stubs'] = stubs
        return c
    imports = [{'name': 'sceRtcGetCurrentClockLocalTime', 'argc': 1, 'ret': clock,
                'writes': [{'arg': 0, 'offset': 0, 'bytes': DATE}]}]
    if clock == 0:
        imports.append({'name': 'sceRtcGetTick', 'argc': 2, 'ret': tick_status, 'argmem': [{'arg': 0, 'size': 0x10}],
                        'writes': [{'arg': 1, 'offset': 0, 'bytes': q(tick)}]})
        if tick != 0:
            stubs.append({'address': PACK_DATE, 'argc': 2, 'argi': [1], 'argmem': [{'arg': 0, 'size': 0x10}],
                          'ret': pack, 'writes': [{'arg': 0, 'offset': 0, 'bytes': q(packed) + q(0x6666666666666666)}]})
    c['imports'] = imports
    stubs += [{'address': ALLOCATE, 'argc': 3, 'argi': [0, 1, 2], 'ret': 'buf:handle'},
              {'address': CONSTRUCT, 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 8}]},
              {'address': APPEND, 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 8}]},
              {'address': MAKE_ENTRY, 'argc': 1, 'argi': [0], 'ret': 'buf:entry'}]
    start = {'address': START, 'argc': 3, 'argi': [0, 2], 'argmem': [{'arg': 1, 'size': 8}]}
    if start_swaps:
        start['writes'] = [{'arg': 1, 'offset': 0, 'pointer': 'other'}]
    stubs.append(start)
    taken = (references + 1) & 0xffffffff  # the count the drop sees (it wraps)
    taken = taken - (1 << 32) if taken >= 1 << 31 else taken
    if taken == 1:  # the entry's reference was the last one
        stubs.append({'address': DESTROY, 'argc': 1, 'argi': [0]})
    elif taken <= 0:
        stubs.append({'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]})
    c['stubs'] = stubs
    return c


def create_cases():
    out = [create_case('allowed'), create_case('late_net', net='late'),
           create_case('late_net_refuses', net='late', flags=(0, 0, 0, 0))]
    for name, flags in (('flag_008_clear', (0, 0, 0, 0)), ('flag_9f6_set', (1, 1, 0, 0)), ('flag_9f8_set', (1, 0, 1, 0)),
                        ('flag_a50_set', (1, 0, 0, 1)), ('all_set', (0xff, 0xff, 0xff, 0xff)),
                        ('flag_9f6_0x80', (1, 0x80, 0, 0)), ('flag_9f8_0x80', (1, 0, 0x80, 0)),
                        ('flag_a50_0x80', (1, 0, 0, 0x80)), ('flag_008_clear_others_set', (0, 0x80, 0x80, 0x80))):
        out.append(create_case(f'refused_{name}', flags=flags))
    for name, flags in (('flag_008_0x80', (0x80, 0, 0, 0)), ('flag_008_0xff', (0xff, 0, 0, 0))):
        out.append(create_case(f'allowed_{name}', flags=flags))
    out += [create_case('clock_fails', clock=0x80b50002), create_case('clock_fails_negative_one', clock=-1),
            create_case('clock_status_1', clock=1),
            create_case('tick_status_fails', tick_status=0x80b50002),
            create_case('tick_zero', tick=0), create_case('tick_one', tick=1),
            create_case('tick_all_ones', tick=0xffffffffffffffff),
            create_case('pack_fails', pack=0), create_case('pack_result_low_byte_zero', pack=0x100),
            create_case('pack_result_0xff', pack=0xff),
            create_case('packed_zero', packed=0), create_case('packed_top_bit', packed=0x8000000000000001),
            create_case('packed_all_ones', packed=0xffffffffffffffff)]
    for references in (0, 1, 5, 0x7fffffff, 0xffffffff, 0x80000000):
        out.append(create_case(f'references_{references:#x}', references=references))
        out.append(create_case(f'references_{references:#x}_start_swaps', references=references, start_swaps=True))
    out.append(create_case('clock_fails_last_reference', clock=-1, references=0))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = fill_cases() if sys.argv[2] == 'fill' else create_cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
