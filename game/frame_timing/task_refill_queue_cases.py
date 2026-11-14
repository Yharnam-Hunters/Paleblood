#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for refilling a task's queue (game/frame_timing/task_refill_queue.cpp).

usage: task_refill_queue_cases.py OUT_ROOT

Writes OUT_ROOT/frame_timing_task_refill_queue_0143ed00/edge/*.json. The queue is emptied by the
real 0x0143ec40 (its blocks and block map freed through the task's allocator), and the deque's
push_back (0x02dd7150) is stubbed, so each push shows which item it was given. Cases: nothing
queued and no items; one, three and seven items; an item list whose ends are equal but not null;
queued blocks (some null) with a start offset; a queue size with no block map; a block map with a
zero size; items and queued blocks together.
"""
import json
import os
import struct
import sys

FUNCTION = 'frame_timing_task_refill_queue_0143ed00'
LOCK, UNLOCK, FREE = 0x00500100, 0x00500200, 0x00500300   # spare addresses for the vtable slots
PUSH_BACK = '0x02dd7150'
ITEM = 0x10


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def case(cid, items=0, blocks=None, offset=0, size=None, empty_list_at=None):
    """items: item count; blocks: None (no block map) or a list of True (a block) / False (null);
    size: the queue size (defaults to the block count); empty_list_at: begin == end at this offset."""
    b = {'task': 0xa0, 'lock_vt': 0x30, 'alloc': 8, 'alloc_vt': 0x78, 'items': ITEM * max(1, items)}
    m = [{'addr': 'buf:task+144', 'pointer': 'lock_vt'},
         {'addr': 'buf:lock_vt+24', 'guest': f'0x{LOCK:08x}'}, {'addr': 'buf:lock_vt+40', 'guest': f'0x{UNLOCK:08x}'},
         {'addr': 'buf:task+128', 'pointer': 'alloc'}, {'addr': 'buf:alloc+0', 'pointer': 'alloc_vt'},
         {'addr': 'buf:alloc_vt+112', 'guest': f'0x{FREE:08x}'},
         {'addr': 'buf:task+88', 'bytes': u64(0x5858)}]
    for i in range(items):
        m.append({'addr': f'buf:items+{ITEM * i}', 'bytes': u64(0x1000 + i) + u64(0x2000 + i)})
    if empty_list_at is not None:
        m += [{'addr': 'buf:task+56', 'pointer': f'items+{empty_list_at}'},
              {'addr': 'buf:task+64', 'pointer': f'items+{empty_list_at}'}]
    elif items:
        m += [{'addr': 'buf:task+56', 'pointer': 'items'}, {'addr': 'buf:task+64', 'pointer': f'items+{ITEM * items}'}]
    stubs = [{'address': f'0x{LOCK:08x}', 'argc': 2}, {'address': f'0x{LOCK:08x}', 'argc': 2}]
    count = len(blocks) if blocks else 0
    if size is None:
        size = count
    m += [{'addr': 'buf:task+112', 'bytes': u64(offset)}, {'addr': 'buf:task+120', 'bytes': u64(size)},
          {'addr': 'buf:task+104', 'bytes': u64(count)}]
    if blocks is not None:
        b['map'] = 8 * max(1, count)
        m.append({'addr': 'buf:task+96', 'pointer': 'map'})
        for i, present in enumerate(blocks):
            if present:
                b[f'block{i}'] = ITEM
                m.append({'addr': f'buf:map+{8 * i}', 'pointer': f'block{i}'})
        stubs += [{'address': f'0x{FREE:08x}', 'argc': 2} for present in blocks if present]
        stubs.append({'address': f'0x{FREE:08x}', 'argc': 2})   # the block map
    stubs.append({'address': f'0x{UNLOCK:08x}', 'argc': 1})
    pushed = 0 if empty_list_at is not None else items
    stubs += [{'address': PUSH_BACK, 'argc': 2} for _ in range(pushed)]
    stubs.append({'address': f'0x{UNLOCK:08x}', 'argc': 1})
    return {'schema': 1, 'address': '0x0143ed00', 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:task'},
            'buffers': {k: {'size': v} for k, v in b.items()}, 'memory': m, 'stubs': stubs, 'imports': []}


CASES = [
    case('nothing'),
    case('one_item', items=1),
    case('three_items', items=3),
    case('seven_items', items=7),
    case('equal_ends_not_null', items=2, empty_list_at=ITEM),
    case('queued_blocks_only', blocks=[True, False, True, True], offset=1, size=3),
    case('size_without_map', size=5),
    case('map_with_zero_size', blocks=[True, True], size=0),
    case('items_and_queue', items=4, blocks=[False, True, True, False], offset=2, size=2),
    case('one_item_one_block', items=1, blocks=[True]),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    d = os.path.join(sys.argv[1], FUNCTION, 'edge')
    os.makedirs(d, exist_ok=True)
    for c in CASES:
        with open(os.path.join(d, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {d}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
