#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the regular heap's free (game/engine/regular_heap_free.cpp), for
tools/verify.py run.

usage: regular_heap_cases.py OUT_DIR

Each case lays out a heap in two buffers: the heap's state with its 64 list sentinels (`heap`)
and a chain of blocks (`arena`), used and free, ending in a used block. Free blocks are filed as
the heap files them: by whole size into list floor(log2(size)), in the order given (the first one
listed is the list's first), except 16-byte blocks, which are in no list. The non-empty mask has
the lists' bits plus any extra bits a case gives, which the function must leave alone (and a
case may clear a bit to see that a non-empty list's bit is never set again).
"""
import json
import os
import struct
import sys

FREE, FATAL_ERROR = '0x024858a0', '0x024b55b0'
ARENA_START = 0x10
HEAP_SIZE = 0x428
LISTS = 0x28


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def list_index(size):
    return size.bit_length() - 1


class Layout:
    """blocks: (size, 'used' | 'free'); sizes are whole block sizes, multiples of 16."""

    def __init__(self, blocks, extra_mask=0, clear_mask=0, available=0x100000, count=7, extra_list=()):
        self.memory = []
        self.offsets = []
        at = ARENA_START
        for size, _ in blocks:
            assert size % 16 == 0 and size >= 16, size
            self.offsets.append(at)
            at += size
        self.end = at
        self.arena_size = at + 0x40
        for i, (_, kind) in enumerate(blocks):
            if kind == 'free':
                assert i == 0 or blocks[i - 1][1] == 'used', 'free blocks are never adjacent'
        for i, (size, kind) in enumerate(blocks):
            off = self.offsets[i]
            nxt = self.offsets[i + 1] if i + 1 < len(blocks) else self.end
            prev_used = i == 0 or blocks[i - 1][1] == 'used'
            flags = (1 if kind == 'used' else 0) | (2 if prev_used else 0)
            self.ptr(f'arena+{off + 8}', f'arena+{nxt + flags}')
            if not prev_used:
                self.ptr(f'arena+{off}', f'arena+{self.offsets[i - 1]}')
        # A final block in use, so the last block's successor has flags to read.
        last_used = blocks[-1][1] == 'used'
        self.ptr(f'arena+{self.end + 8}', f'arena+{self.end + 0x20 + 1 + (2 if last_used else 0)}')
        if not last_used:
            self.ptr(f'arena+{self.end}', f'arena+{self.offsets[-1]}')
        lists = {}
        for i, (size, kind) in enumerate(blocks):
            if kind == 'free' and size > 16:
                lists.setdefault(list_index(size), []).append(('arena', self.offsets[i]))
        # Blocks outside the arena (a list's other members), as (list, buffer offset in `others`).
        for index, off in extra_list:
            lists.setdefault(index, []).append(('others', off))
        mask = extra_mask
        for k in range(64):
            nodes = lists.get(k, [])
            if nodes:
                mask |= 1 << k
            chain = [('heap', LISTS - 0x10 + 16 * k)] + nodes
            for j, (name, off) in enumerate(chain):
                after, before = chain[(j + 1) % len(chain)], chain[j - 1]
                self.ptr(f'{name}+{off + 0x10}', f'{before[0]}+{before[1]}')
                self.ptr(f'{name}+{off + 0x18}', f'{after[0]}+{after[1]}')
        mask &= ~clear_mask
        self.memory += [{'addr': 'buf:heap+0', 'bytes': u64(mask)},
                        {'addr': 'buf:heap+24', 'bytes': u64(available)},
                        {'addr': 'buf:heap+32', 'bytes': u64(count)}]

    def ptr(self, where, target):
        self.memory.append({'addr': f'buf:{where}', 'pointer': target})


def case(cid, blocks, index, errors=(), heap_range=None, payload=None, **kw):
    """Frees the payload of block `index` (or `payload`, an arena offset or 0 for null). heap_range
    is the payload range (arena offsets; None for the arena's end)."""
    layout = Layout(blocks, **kw)
    low, high = heap_range if heap_range else (ARENA_START + 0x10, None)
    high = layout.end if high is None else high
    layout.memory += [{'addr': 'buf:heap+8', 'pointer': f'arena+{low}'},
                      {'addr': 'buf:heap+16', 'pointer': f'arena+{high}'}]
    if payload is None:
        payload = layout.offsets[index] + 0x10
    stubs = [{'address': FATAL_ERROR, 'argc': argc} for argc in errors]
    return {'schema': 1, 'address': FREE, 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:heap', 'rsi': f'buf:arena+{payload}' if payload else 0},
            'buffers': {'heap': {'size': HEAP_SIZE}, 'arena': {'size': layout.arena_size},
                        'others': {'size': 0x100}},
            'memory': layout.memory, 'stubs': stubs, 'imports': []}


U, F = 'used', 'free'
OUTSIDE, NOT_IN_USE = 6, 3   # the error report's argument counts


def free_cases():
    return [
        case('between_used', [(0x20, U), (0x60, U), (0x20, U)], 1),
        case('first_block', [(0x60, U), (0x20, U)], 0),
        case('last_block', [(0x20, U), (0x60, U)], 1),
        case('sixteen_byte_block', [(0x20, U), (0x10, U), (0x20, U)], 1),
        case('merge_prev_sole', [(0x40, F), (0x40, U), (0x20, U)], 1),
        case('merge_prev_keeps_list', [(0x40, F), (0x40, U), (0x50, F), (0x20, U)], 1),
        case('merge_prev_from_middle', [(0x20, U), (0x50, F), (0x40, U), (0x40, F), (0x20, U),
                                        (0x60, F), (0x20, U)], 2),
        case('merge_prev_sixteen', [(0x10, F), (0x40, U), (0x20, U)], 1),
        case('merge_next_sole', [(0x20, U), (0x40, U), (0x40, F), (0x20, U)], 1),
        case('merge_next_shared_list', [(0x20, U), (0x40, U), (0x40, F), (0x20, U), (0x70, F),
                                        (0x20, U)], 1),
        case('merge_next_sixteen', [(0x20, U), (0x40, U), (0x10, F), (0x20, U)], 1),
        case('merge_next_last', [(0x20, U), (0x40, U), (0x80, F)], 1),
        case('merge_both', [(0x20, U), (0x30, F), (0x40, U), (0x50, F), (0x20, U)], 2),
        case('merge_both_sixteen', [(0x10, F), (0x10, U), (0x10, F), (0x20, U)], 1),
        case('merge_both_same_list', [(0x20, U), (0x40, F), (0x20, U), (0x50, F), (0x20, U)], 2),
        case('into_nonempty_list', [(0x20, U), (0x80, U), (0x20, U), (0xa0, F), (0x20, U)], 1),
        # A list whose bit is clear although it holds a block: the bit is set only for a list
        # that was empty, so it stays clear.
        case('stale_clear_bit', [(0x20, U), (0x80, U), (0x20, U), (0xa0, F), (0x20, U)], 1,
             clear_mask=1 << 7),
        case('into_list_outside', [(0x20, U), (0x80, U), (0x20, U)], 1, extra_list=[(7, 0x40)]),
        case('large_block', [(0x20, U), (0x4000, U), (0x20, U)], 1),
        case('large_merge', [(0x20, U), (0x1ff0, F), (0x10, U), (0x2000, F), (0x20, U)], 2),
        case('extra_mask_bits', [(0x40, F), (0x40, U), (0x20, U)], 1,
             extra_mask=(1 << 63) | (1 << 40) | (1 << 3)),
        case('counters_wrap', [(0x20, U), (0x60, U), (0x20, U)], 1, available=-0x20, count=0),
        case('null', [(0x20, U), (0x60, U)], 0, payload=0),
        case('below_range', [(0x20, U), (0x60, U), (0x20, U)], 1, errors=(OUTSIDE,),
             heap_range=(0x50, None)),
        case('above_range', [(0x20, U), (0x60, U), (0x20, U)], 1, errors=(OUTSIDE,),
             heap_range=(0x10, 0x30)),
        case('range_edges', [(0x20, U), (0x60, U), (0x20, U)], 1, heap_range=(0x40, 0x40)),
        case('not_in_use', [(0x20, U), (0x60, F), (0x20, U)], 1, errors=(NOT_IN_USE,)),
        case('outside_and_not_in_use', [(0x20, U), (0x60, F), (0x20, U)], 1,
             errors=(OUTSIDE, NOT_IN_USE), heap_range=(0x48, None)),
    ]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for c in free_cases():
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(free_cases())} cases in {out}')


if __name__ == '__main__':
    main()
