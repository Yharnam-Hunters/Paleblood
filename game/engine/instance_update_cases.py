#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the instance update helpers (game/engine/instance_update.cpp), for tools/verify.py run.

usage: instance_update_cases.py OUT_DIR object|map|register

- object / map: request flags with and without the request bit and junk in the other bits;
  queue flags queued (nothing queued) and not queued, with junk bits (and, for map, the byte after
  the flags kept); queue counts 0, in the middle, capacity - 1, at capacity and past it (reported
  through the scripted fatal error stub, which answers junk, then written past the end), capacity 0 and
  ids 0, 0xffff. object's part step (0x01beca10) is a scripted stub compared with the part pointer.
- register: no owner, no item, no earlier item; an earlier item first, in the middle, last and the
  only one; an earlier item missing from the list (the count goes down and the last item lands at
  index -1, kept inside the buffer); an empty list with an earlier item (count -1); a list that
  becomes full (the grow stub 0x00819100, compared with the list allocator, the list and 8, writes
  a new item pointer, so the appended item lands in the new storage); flags in the capacity word's
  top bits; negative counts; a count above the capacity (the list grows only when the count equals
  the capacity, so it does not, and the item lands past the end).
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'object': '0x019a7090', 'map': '0x0198bf70', 'register': '0x01c44210'}
FATAL_ERROR, STEP_PART, GROW = '0x024b55b0', '0x01beca10', '0x00819100'
ALLOCATOR = 0x058018b0


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def queue_case(variant, cid, requests, queue_flags, count, capacity=8, ident=0x1234, report=0, after=0x5a):
    c = base(variant, cid)
    state = bytearray(b'\x88' * 0x18)
    struct.pack_into('<HH', state, 8, ident, requests)
    if variant == 'object':
        struct.pack_into('<H', state, 0xc, queue_flags)
    else:
        state[0xc] = queue_flags
        state[0xd] = after
    c['buffers']['state'] = {'size': len(state), 'bytes': state.hex()}
    queue = bytearray(b'\x37' * 0x20)
    struct.pack_into('<I', queue, 0, capacity)
    struct.pack_into('<I', queue, 0x10, count)
    c['buffers']['queue'] = {'size': 0x20, 'bytes': queue.hex()}
    c['buffers']['ids'] = {'size': 2 * (max(count, capacity) + 2), 'bytes': 'cd' * 2 * (max(count, capacity) + 2)}
    c['memory'] += [{'addr': 'buf:state', 'pointer': 'queue'}, {'addr': 'buf:queue+0x18', 'pointer': 'ids'}]
    holder = 0x10 if variant == 'object' else 0xc8
    c['buffers']['instance'] = {'size': 0x220, 'bytes': '19' * 0x220}
    c['memory'].append({'addr': f'buf:instance+{holder:#x}', 'pointer': 'state'})
    if variant == 'object':
        c['stubs'].append({'address': STEP_PART, 'argc': 1})
    queued = queue_flags & (0x1000 if variant == 'object' else 1)
    if not queued and count >= capacity:
        c['stubs'].append({'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 2], 'argu32': [1], 'ret': hex(report)})
    c['args'] = {'rdi': 'buf:instance'}
    return c


def queue_cases(variant):
    request, queued = (0x20, 0x1000) if variant == 'object' else (0x4, 0x1)
    out = []
    for rname, requests in (('none', 0), ('set', request), ('others', 0xffff & ~request), ('all', 0xffff)):
        out.append(queue_case(variant, f'requests_{rname}_queued', requests, queued, 3))
        out.append(queue_case(variant, f'requests_{rname}_not_queued', requests, 0, 3))
    junk = 0xefff if variant == 'object' else 0xfe
    out += [queue_case(variant, 'queued_with_junk', 0x1111, junk | queued, 3),
            queue_case(variant, 'not_queued_with_junk', 0x1111, junk, 3),
            queue_case(variant, 'count_0', 0, 0, 0), queue_case(variant, 'count_last_free', 0, 0, 7),
            queue_case(variant, 'id_0', 0, 0, 2, ident=0), queue_case(variant, 'id_ffff', 0, 0, 2, ident=0xffff),
            queue_case(variant, 'count_at_capacity_queued', 0, queued, 8)]
    for report in (0, 0xffffffff):
        out.append(queue_case(variant, f'full_report_{report:x}', 0, junk, 8, report=report))
        out.append(queue_case(variant, f'past_full_report_{report:x}', 0, 0, 9, report=report))
        out.append(queue_case(variant, f'capacity_0_report_{report:x}', 0, 0, 0, capacity=0, report=report))
    if variant == 'map':
        out.append(queue_case(variant, 'byte_after_flags_kept', 0, 0, 1, after=0xff))
    return out


def register_case(cid, items, current, item=0x7777, count=None, capacity=8, flags=0, owner=True, grow=False,
                  slack=2):
    """items: list of pointer values; current: value (None: null); the data starts `slack` words into its buffer."""
    c = base('register', cid)
    count = len(items) if count is None else count
    words = max(capacity, len(items)) + slack + 4
    data = bytearray(b'\xee' * 8 * words)
    for i, v in enumerate(items):
        struct.pack_into('<Q', data, 8 * (slack + i), v)
    c['buffers']['data'] = {'size': len(data), 'bytes': data.hex()}
    owner_bytes = bytearray(b'\x21' * 0xb8)
    struct.pack_into('<iI', owner_bytes, 0xb0, count, capacity | flags)
    c['buffers']['owner'] = {'size': 0xb8, 'bytes': owner_bytes.hex()}
    c['memory'].append({'addr': 'buf:owner+0xa8', 'pointer': f'data+{8 * slack:#x}'})
    reg = bytearray(b'\x65' * 0x18)
    struct.pack_into('<Q', reg, 0x10, current or 0)
    c['buffers']['registration'] = {'size': 0x18, 'bytes': reg.hex()}
    c['memory'].append({'addr': 'buf:registration+0x8', **({'pointer': 'owner'} if owner else {'bytes': '00' * 8})})
    if grow:
        c['buffers']['grown'] = {'size': 8 * (2 * capacity + 2), 'bytes': 'aa' * 8 * (2 * capacity + 2)}
        c['stubs'].append({'address': GROW, 'argc': 3, 'argi': [0, 1], 'argu32': [2],
                           'writes': [{'arg': 1, 'offset': 0, 'pointer': 'grown'},
                                      {'arg': 1, 'offset': 12, 'bytes': struct.pack('<I', 2 * capacity | flags).hex()}]})
    c['args'] = {'rdi': 'buf:registration', 'rsi': item}
    return c


def register_cases():
    a, b, d, e = 0x1000a, 0x1000b, 0x1000d, 0x1000e
    return [
        register_case('no_owner', [a, b], a, owner=False),
        register_case('no_item', [a, b], a, item=0),
        register_case('no_earlier_item', [a, b], None),
        register_case('no_earlier_item_empty', [], None),
        register_case('earlier_first', [a, b, d, e], a),
        register_case('earlier_middle', [a, b, d, e], b),
        register_case('earlier_last', [a, b, d, e], e),
        register_case('earlier_only', [a], a),
        register_case('earlier_twice_first_taken', [a, b, a, d], a),
        register_case('earlier_past_count', [a, b, d], d, count=2),
        register_case('earlier_missing', [a, b, d], 0x1000f),
        register_case('earlier_missing_one_item', [a], 0x1000f),
        register_case('earlier_empty_list', [], a),
        register_case('same_item_again', [a, b], b, item=b),
        register_case('becomes_full_no_earlier', [a, b, d], None, capacity=3, grow=True),
        register_case('full_after_removal_not', [a, b, d], b, capacity=3),
        register_case('full_flags_top_bits', [a, b], None, capacity=2, flags=0xc0000000, grow=True),
        register_case('not_full_flags_top_bits', [a, b], None, capacity=3, flags=0x80000000),
        register_case('capacity_0_empty', [], None, capacity=0, grow=True),
        register_case('negative_count', [a, b], None, count=-1, capacity=4),
        register_case('negative_count_with_earlier', [a, b], a, count=-2, capacity=4, slack=4),
        register_case('count_above_items', [a, b], 0x1000f, count=3, capacity=8),
        register_case('count_above_capacity', [a, b, d], None, capacity=2),
    ]


def cases(variant):
    if variant in ('object', 'map'):
        return queue_cases(variant)
    return register_cases()


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
