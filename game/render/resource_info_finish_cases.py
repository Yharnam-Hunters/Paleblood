#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the two steps that end swap-chain resource information processing
(game/render/resource_info_finish.cpp), for tools/verify.py run.

usage: resource_info_finish_cases.py OUT_DIR label|submit

- label: the real counter and packet writer; the counter below, at and above the reserved value,
  a reserved value with high bits set, the 32-bit wrap (also through the skip), a packet buffer
  with exactly enough room, and too little room with the space callback declining (its whole
  result returned) or granting more room; label addresses with high and low bits set.
- submit: a scripted queue that records the range it is given: empty, whole-word and partial-word
  sizes, a cursor below the start, addresses whose difference crosses 32 bits, a queue that
  refuses, and a queue that moves the cursor (the new start is read after the call). Then the
  real queue on a ring: with room, refusing for lack of room (the start still moves), and
  wrapping to the ring's start.
"""
import json
import os
import sys

ADDRESSES = {'label': '0x02ab1930', 'submit': '0x02ab18c0'}
RETURNS = {'label': 'i64', 'submit': 'void'}
GUARD_GOT = '0x057e3be0'
SUBMIT = '0x0148c830'
SPACE_CALLBACK = '0x00500200'
WORD = 4


def q(value):
    return (value & 0xffffffffffffffff).to_bytes(8, 'little').hex()


def d(value):
    return (value & 0xffffffff).to_bytes(4, 'little').hex()


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant],
            'args': {'rdi': 'buf:proc+0x0'}, 'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def label_case(cid, counter, reserved, room_words=0x10, callback=None, address=0x0000004500001000):
    c = base('label', cid)
    c['buffers'] = {'proc': {'size': 0x48, 'bytes': 'cc' * 0x48},
                    'svc': {'size': 0x30, 'bytes': '77' * 0x30},
                    'ctr': {'size': 8, 'bytes': d(counter) + 'ee' * 4},
                    'cmd': {'size': 0x80, 'bytes': '00' * 0x80}}
    c['memory'] = [{'addr': 'buf:proc+0x0', 'pointer': 'cmd+0x0'},
                   {'addr': 'buf:proc+0x8', 'pointer': f'cmd+{8 + room_words * WORD:#x}'},
                   {'addr': 'buf:proc+0x10', 'pointer': 'cmd+0x8'},
                   {'addr': 'buf:proc+0x18', 'guest': SPACE_CALLBACK},
                   {'addr': 'buf:proc+0x20', 'bytes': q(0xa11ce)},
                   {'addr': 'buf:proc+0x40', 'pointer': 'svc+0x0'},
                   {'addr': 'buf:svc+0x10', 'bytes': q(address)},
                   {'addr': 'buf:svc+0x18', 'pointer': 'ctr+0x0'},
                   {'addr': 'buf:svc+0x28', 'bytes': q(reserved)}]
    if callback is not None:
        ret, grant = callback
        stub = {'address': SPACE_CALLBACK, 'argc': 3, 'ret': ret}
        if grant:
            stub['writes'] = [{'arg': 0, 'offset': 8, 'pointer': 'cmd+0x80'}]
        c['stubs'] = [stub]
    return c


def label_cases():
    return [label_case('plain', 5, 0),
            label_case('counter_zero', 0, 0x10),
            label_case('reserved_hit', 7, 7),
            label_case('reserved_zero_hit', 0, 0),
            label_case('reserved_is_next', 7, 8),
            label_case('reserved_one_below', 7, 6),
            label_case('reserved_high_bits', 7, 0x100000007),
            label_case('reserved_all_ones', 0xffffffff, 0xffffffffffffffff),
            label_case('counter_wraps', 0xffffffff, 0),
            label_case('skip_through_wrap', 0xffffffff, 0xffffffff),
            label_case('high_counter', 0x80000000, 0x7fffffff),
            label_case('exact_room', 0x123, 0, room_words=7),
            label_case('no_room_declined', 0x123, 0, room_words=6, callback=(0, False)),
            label_case('no_room_declined_wide', 0x123, 0x123, room_words=2, callback=(0x5a00, False)),
            label_case('no_room_granted', 0x123, 0x123, room_words=0, callback=(1, True)),
            label_case('address_high_bits', 9, 0, address=0xffff123456789abf),
            label_case('address_low_bits', 9, 0, address=0x00000001000000ff)]


def submit_case(cid, begin, cursor, stub_ret=0, moved_cursor=None):
    c = base('submit', cid)
    c['buffers'] = {'proc': {'size': 0x60, 'bytes': 'cc' * 0x60},
                    'guard': {'size': 0x10, 'bytes': '5a' * 0x10}}
    c['memory'] = [{'addr': GUARD_GOT, 'pointer': 'guard'},
                   {'addr': 'buf:proc+0x0', 'bytes': q(begin)},
                   {'addr': 'buf:proc+0x8', 'bytes': q(cursor + 0x1000)},
                   {'addr': 'buf:proc+0x10', 'bytes': q(cursor)}]
    stub = {'address': SUBMIT, 'argc': 2, 'ret': stub_ret,
            'argmem': [{'arg': 2, 'size': 8}, {'arg': 3, 'size': 4}]}
    if moved_cursor is not None:
        stub['writes'] = [{'arg': 0, 'offset': 0x10 - 0x58, 'bytes': q(moved_cursor)}]
    c['stubs'] = [stub]
    return c


def ring_case(cid, written, read_words, pending=0x40, ring_words=0x40):
    """The real queue: a ring of ring_words words, `written` bytes in, the reader at read_words."""
    c = base('submit', cid)
    c['buffers'] = {'proc': {'size': 0xb0, 'bytes': 'cc' * 0xb0},
                    'guard': {'size': 0x10, 'bytes': '5a' * 0x10},
                    'cmd': {'size': 0x100, 'bytes': '00' * 0x100},
                    'ring': {'size': ring_words * WORD, 'bytes': '00' * (ring_words * WORD)},
                    'read': {'size': 8, 'bytes': d(read_words) + '00' * 4}}
    ring = 0x58
    c['memory'] = [{'addr': GUARD_GOT, 'pointer': 'guard'},
                   {'addr': 'buf:proc+0x0', 'pointer': 'cmd+0x10'},
                   {'addr': 'buf:proc+0x8', 'pointer': 'cmd+0x100'},
                   {'addr': 'buf:proc+0x10', 'pointer': f'cmd+{0x10 + pending:#x}'},
                   {'addr': f'buf:proc+{ring + 0x8:#x}', 'bytes': d(3)},
                   {'addr': f'buf:proc+{ring + 0x10:#x}', 'pointer': 'read+0x0'},
                   {'addr': f'buf:proc+{ring + 0x18:#x}', 'pointer': 'ring+0x0'},
                   {'addr': f'buf:proc+{ring + 0x20:#x}', 'pointer': f'ring+{ring_words * WORD - 8:#x}'},
                   {'addr': f'buf:proc+{ring + 0x28:#x}', 'pointer': f'ring+{written:#x}'},
                   {'addr': f'buf:proc+{ring + 0x50:#x}', 'bytes': d(ring_words)}]
    c['imports'] = [{'name': 'sceGnmDingDong', 'argc': 2, 'ret': 0}]
    return c


def submit_cases():
    lo = 0x7f0000100000
    return [submit_case('empty', lo, lo),
            submit_case('whole_words', lo, lo + 0x40),
            submit_case('one_byte_short', lo, lo + 0x3f),
            submit_case('partial_word', lo, lo + 0x43),
            submit_case('single_word', lo + 4, lo + 8),
            submit_case('below_a_word', lo, lo + 3),
            submit_case('cursor_below_start', lo + 0x40, lo),
            submit_case('crosses_32_bits', 0x1fffffff0, 0x200000010),
            submit_case('high_bits_differ', 0x100000000, 0x300000020),
            submit_case('queue_refuses', lo, lo + 0x20, stub_ret=0x80000001),
            submit_case('queue_moves_cursor', lo, lo + 0x20, moved_cursor=lo + 0x64),
            ring_case('ring_room', written=0x10, read_words=4),
            ring_case('ring_full', written=0x10, read_words=6),
            ring_case('ring_wraps', written=0xf8, read_words=0x10)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = label_cases() if sys.argv[2] == 'label' else submit_cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
