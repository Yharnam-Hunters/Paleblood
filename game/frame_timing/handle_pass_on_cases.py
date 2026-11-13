#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for passing the gate's handle on (game/frame_timing/handle_pass_on.cpp), for
tools/verify.py run.

usage: handle_pass_on_cases.py OUT_DIR pass|with_items|append

- pass / with_items: no handle; a missing FrpgNetMan (reported, then the handle is read again: the
  report's stub clears it or puts another object there);
  the item heap's info with and without its valid bit; the handle object's items (none, a null
  range, one to seven, extreme values, and the 4 bytes after each item that are not copied); and
  calls to 0x022658b0 that swap or clear the list's block or swap its heap (the block held before
  the call is freed, through the heap the list holds after it). The real append and grow run, on
  a stubbed heap that hands out a block per growth. For with_items: the caller's list is what is
  handed on (its contents and changes to it do not reach the free).
- append: room left, with the item outside the list (below, above, at the end) or inside it
  (first, middle, last); full lists of one to six items, outside, inside and at the end, grown by
  the real grow or a stub (which shows where the copy is read from); a grow that leaves no end; a null end with room; and sizes from raw pointers
  that pick each growth rule: half as much again, past the limit (no growth room), and the
  negative sizes an end below the beginning gives.
"""
import json
import os
import struct
import sys

ADDRESSES = {'pass': '0x01e9cb80', 'with_items': '0x01e9cd20', 'append': '0x01e9d960'}
RETURNS = {'pass': 'i8', 'with_items': 'i8', 'append': 'void'}
GROW, PASS_ITEMS, FATAL_ERROR = '0x01e9da70', '0x022658b0', '0x024b55b0'
FRPG_NET_MAN, ITEM_HEAP, GUARD_GOT = '0x0593b120', '0x05940428', '0x057e3be0'
# Unused guest addresses: the heaps' vtable slots, stubbed.
HEAP_INFO, HEAP_FREE, OTHER_HEAP_FREE, HEAP_ALLOCATE = '0x00400090', '0x004000a0', '0x004000b0', '0x004000c0'
INFO, ALLOCATE, FREE = 0x20, 0x58, 0x70  # heap vtable slots
ITEMS_BEGIN, ITEMS_END = 0x70, 0x78  # in the handle object
BEGIN, END, CAPACITY, HEAP = 0x8, 0x10, 0x18, 0x20  # in a list
ITEM = 12
# For the cases whose report changes the handle: a stub writes only through its arguments, so the
# handle's place is an image word (unused by these functions) that the report's stub reaches from
# its first argument, the singleton check's file name in the image.
IMAGE_SLOT, REPORT_FILE = 0x05940430, 0x04d3b369
ALIGNMENT = 4


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def item_bytes(k):
    return u32(0x1000 + k) + u32(0x20 + k) + u32(0x30000 + 7 * k)


def growths(count, size=0, capacity=0):
    """The capacities the list grows to while `count` items are appended."""
    out = []
    for _ in range(count):
        if size == capacity:
            capacity = max(size + size // 2, size + 1)
            out.append(capacity)
        size += 1
    return out


def heap_memory():
    return [{'addr': 'buf:heap', 'pointer': 'heap_vt'}, {'addr': f'buf:heap_vt+{INFO}', 'guest': HEAP_INFO},
            {'addr': f'buf:heap_vt+{ALLOCATE}', 'guest': HEAP_ALLOCATE}, {'addr': f'buf:heap_vt+{FREE}', 'guest': HEAP_FREE},
            {'addr': 'buf:other_heap', 'pointer': 'other_heap_vt'},
            {'addr': f'buf:other_heap_vt+{FREE}', 'guest': OTHER_HEAP_FREE}]


def heap_buffers(caps):
    b = {'heap': {'size': 8}, 'heap_vt': {'size': 0x78}, 'other_heap': {'size': 8}, 'other_heap_vt': {'size': 0x78}}
    for k, cap in enumerate(caps):
        b[f'block{k}'] = {'size': ITEM * cap}
    return b


def grow_stubs(caps, had_block=False):
    """The real grow's calls: an allocation per growth, and a free of each block it leaves."""
    out = [{'address': HEAP_ALLOCATE, 'argc': 3, 'argi': [0, 1, 2], 'ret': f'buf:block{k}'} for k in range(len(caps))]
    frees = len(caps) - (0 if had_block else 1)
    out += [{'address': HEAP_FREE, 'argc': 2, 'argi': [0, 1]}] * max(frees, 0)
    return out


def pass_case(variant, cid, handle=True, net=True, info=0x20, count=2, null_range=False, values=None,
              swap_block=False, clear_block=False, swap_heap=False, report_writes_handle=None):
    """report_writes_handle: what the missing-FrpgNetMan report leaves in the handle's place
    (None: nothing; 0: no handle; 'object2': another object, with one item)."""
    caps = growths(count)
    c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant],
         'args': {'rdi': 'buf:slot'},
         'buffers': {'slot': {'size': 8}, 'object': {'size': 0x80}, 'entries': {'size': 16 * max(count, 1)},
                     'net': {'size': 8}, 'guard': {'size': 8, 'bytes': u64(0x5a5a5a5a12345678)},
                     'other_block': {'size': 24}, **heap_buffers(caps)},
         'memory': [{'addr': GUARD_GOT, 'pointer': 'guard'}, {'addr': ITEM_HEAP, 'pointer': 'heap'},
                    {'addr': FRPG_NET_MAN, 'pointer': 'net'} if net else {'addr': FRPG_NET_MAN, 'bytes': u64(0)},
                    {'addr': 'buf:slot', 'pointer': 'object'} if handle else {'addr': 'buf:slot', 'bytes': u64(0)},
                    {'addr': 'buf:object+8', 'bytes': u32(1)}] + heap_memory(),
         'imports': []}
    if null_range:
        c['memory'] += [{'addr': f'buf:object+{ITEMS_BEGIN}', 'bytes': u64(0)}, {'addr': f'buf:object+{ITEMS_END}', 'bytes': u64(0)}]
    else:
        c['memory'] += [{'addr': f'buf:object+{ITEMS_BEGIN}', 'pointer': 'entries'},
                        {'addr': f'buf:object+{ITEMS_END}', 'pointer': f'entries+{16 * count}'}]
    for k in range(count):
        data = item_bytes(k) if values is None else u32(values[0]) + u32(values[1]) + u32(values[2])
        c['memory'].append({'addr': f'buf:entries+{16 * k}', 'bytes': data + u32(0xeeee0000 + k)})
    if variant == 'with_items':
        c['args']['rsi'] = 'buf:passed'
        c['buffers']['passed'] = {'size': 0x28}
        c['buffers']['passed_items'] = {'size': 24}
        c['memory'] += [{'addr': 'buf:passed', 'bytes': u64(0x1111)}, {'addr': f'buf:passed+{BEGIN}', 'pointer': 'passed_items'},
                        {'addr': f'buf:passed+{END}', 'pointer': 'passed_items+12'},
                        {'addr': f'buf:passed+{CAPACITY}', 'pointer': 'passed_items+24'},
                        {'addr': f'buf:passed+{HEAP}', 'pointer': 'heap'}]
    stubs = []
    if not handle:
        c['stubs'] = stubs
        return c
    if not net:
        report = {'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3]}
        if report_writes_handle is not None:
            c['args']['rdi'] = f'guest:{IMAGE_SLOT:#010x}'
            c['memory'] = [m for m in c['memory'] if m['addr'] != 'buf:slot'] + [{'addr': f'{IMAGE_SLOT:#010x}', 'pointer': 'object'}]
        if report_writes_handle == 0:
            report['writes'] = [{'arg': 0, 'offset': IMAGE_SLOT - REPORT_FILE, 'bytes': u64(0)}]
        elif report_writes_handle:
            report['writes'] = [{'arg': 0, 'offset': IMAGE_SLOT - REPORT_FILE, 'pointer': 'object2'}]
            c['buffers']['object2'] = {'size': 0x80}
            c['buffers']['entries2'] = {'size': 16, 'bytes': item_bytes(40) + u32(0xeeeeeeee)}
            c['memory'] += [{'addr': f'buf:object2+{ITEMS_BEGIN}', 'pointer': 'entries2'},
                            {'addr': f'buf:object2+{ITEMS_END}', 'pointer': 'entries2+16'}]
            caps = growths(1)
            c['buffers'].update(heap_buffers(caps))
        if report_writes_handle == 0:
            c['stubs'] = [report]
            return c
        stubs.append(report)
    stubs.append({'address': HEAP_INFO, 'argc': 3, 'argi': [1, 2], 'writes': [{'arg': 0, 'offset': 0, 'bytes': f'{info:02x}'}]})
    if not info & 0x20:
        stubs.append({'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]})
    stubs += grow_stubs(caps)
    writes = []
    if swap_block:
        writes.append({'arg': 1, 'offset': BEGIN, 'pointer': 'other_block'})
    if clear_block:
        writes.append({'arg': 1, 'offset': BEGIN, 'bytes': u64(0)})
    if swap_heap:
        writes.append({'arg': 1, 'offset': HEAP, 'pointer': 'other_heap'})
    stubs.append({'address': PASS_ITEMS, 'argc': 2, 'argi': [0, 1] if variant == 'with_items' else [0],
                  'argmem': [{'arg': 0, 'size': 8}, {'arg': 1, 'offset': BEGIN, 'size': 32}], 'ret': 1, 'writes': writes})
    if caps:
        # The free goes through the new list's heap as it is after the call; with_items hands the
        # caller's list on, so the new one keeps the item heap.
        swapped = swap_heap and variant == 'pass'
        stubs.append({'address': OTHER_HEAP_FREE if swapped else HEAP_FREE, 'argc': 2, 'argi': [0, 1]})
    c['stubs'] = stubs
    return c


def pass_cases(variant):
    out = [pass_case(variant, 'no_handle', handle=False), pass_case(variant, 'no_handle_no_net', handle=False, net=False),
           pass_case(variant, 'no_net', net=False), pass_case(variant, 'no_net_no_items', net=False, count=0),
           # The handle is read again after the report (the stub stands for whatever changed it).
           pass_case(variant, 'no_net_report_clears_handle', net=False, report_writes_handle=0),
           pass_case(variant, 'no_net_report_swaps_handle', net=False, count=3, report_writes_handle='object2')]
    out += [pass_case(variant, 'info_without_valid_bit', info=0xdf), pass_case(variant, 'info_zero', info=0),
            pass_case(variant, 'info_only_valid_bit', info=0x20), pass_case(variant, 'info_all_bits', info=0xff)]
    out += [pass_case(variant, f'items_{n}', count=n) for n in (0, 1, 2, 3, 4, 5, 7)]
    out += [pass_case(variant, 'items_null_range', count=0, null_range=True),
            pass_case(variant, 'items_extreme_values', count=3, values=(0xffffffff, 0x80000000, 0x7fffffff)),
            pass_case(variant, 'items_zero_values', count=2, values=(0, 0, 0))]
    out += [pass_case(variant, 'pass_swaps_block', swap_block=True), pass_case(variant, 'pass_clears_block', clear_block=True),
            pass_case(variant, 'pass_swaps_heap', swap_heap=True), pass_case(variant, 'pass_swaps_block_and_heap', swap_block=True, swap_heap=True),
            pass_case(variant, 'no_items_pass_swaps_block', count=0, swap_block=True),
            pass_case(variant, 'no_items_pass_swaps_heap', count=0, swap_heap=True)]
    return out


def append_case(cid, size=2, capacity=4, item='outside', grow='real', grow_leaves_no_end=False, raw=None, null_end=False):
    """item: 'outside' (a buffer below the list's), 'after' (one above it), 'at_end' (the list's
    end), or an index inside the list. raw: (begin, end, capacity) as numbers (grow is stubbed)."""
    buffers = {'list': {'size': 0x28}, 'item': {'size': ITEM, 'bytes': item_bytes(90)},
               'z_item': {'size': ITEM, 'bytes': item_bytes(91)},
               'items': {'size': ITEM * max(capacity, 1) + ITEM, 'bytes': ''.join(item_bytes(k) for k in range(max(capacity, 1) + 1))},
               'stub_block': {'size': ITEM * 8, 'bytes': ''.join(item_bytes(50 + k) for k in range(8))}}
    c = {'schema': 1, 'address': ADDRESSES['append'], 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:list'},
         'buffers': buffers, 'memory': [{'addr': 'buf:list', 'bytes': u64(0x2222)}, {'addr': f'buf:list+{HEAP}', 'pointer': 'heap'}] + heap_memory(),
         'imports': []}
    if raw:
        c['memory'] += [{'addr': f'buf:list+{off}', 'bytes': u64(v)} for off, v in zip((BEGIN, END, CAPACITY), raw)]
    elif null_end:
        c['memory'] += [{'addr': f'buf:list+{BEGIN}', 'bytes': u64(0)}, {'addr': f'buf:list+{END}', 'bytes': u64(0)},
                        {'addr': f'buf:list+{CAPACITY}', 'pointer': 'items'}]
    elif capacity == 0:
        c['memory'] += [{'addr': f'buf:list+{off}', 'bytes': u64(0)} for off in (BEGIN, END, CAPACITY)]
    else:
        c['memory'] += [{'addr': f'buf:list+{BEGIN}', 'pointer': 'items'}, {'addr': f'buf:list+{END}', 'pointer': f'items+{ITEM * size}'},
                        {'addr': f'buf:list+{CAPACITY}', 'pointer': f'items+{ITEM * capacity}'}]
    if item == 'outside':
        c['args']['rsi'] = 'buf:item'
    elif item == 'after':
        c['args']['rsi'] = 'buf:z_item'
    elif item == 'at_end':
        c['args']['rsi'] = f'buf:items+{ITEM * size}'
    else:
        c['args']['rsi'] = f'buf:items+{ITEM * item}'
    full = raw is not None or (size == capacity and not null_end)
    stubs = []
    if full and grow == 'real':
        caps = growths(1, size, capacity)
        c['buffers'].update(heap_buffers(caps))
        stubs += grow_stubs(caps, had_block=capacity != 0)
    else:
        c['buffers'].update(heap_buffers([]))
        if full:
            writes = [{'arg': 0, 'offset': BEGIN, 'pointer': 'stub_block'},
                      {'arg': 0, 'offset': END, 'bytes': u64(0)} if grow_leaves_no_end else {'arg': 0, 'offset': END, 'pointer': f'stub_block+{ITEM * 3}'},
                      {'arg': 0, 'offset': CAPACITY, 'pointer': 'stub_block+96'}]
            stubs.append({'address': GROW, 'argc': 2, 'argi': [0, 1], 'writes': writes})
    c['stubs'] = stubs
    return c


def append_cases():
    out = [append_case('empty_list', size=0, capacity=0), append_case('room_item_below'), append_case('room_item_above', item='after'),
           append_case('room_item_at_end', item='at_end'), append_case('room_last_free_slot', size=3, capacity=4),
           append_case('room_inside_first', item=0), append_case('room_inside_middle', size=3, capacity=5, item=1),
           append_case('room_inside_last', size=3, capacity=5, item=2), append_case('null_end_with_room', null_end=True)]
    out += [append_case(f'full_{n}_outside', size=n, capacity=n) for n in range(1, 7)]
    out += [append_case('full_1_inside', size=1, capacity=1, item=0), append_case('full_3_inside_middle', size=3, capacity=3, item=1),
            append_case('full_4_inside_last', size=4, capacity=4, item=3), append_case('full_6_inside_first', size=6, capacity=6, item=0),
            append_case('full_item_above', size=2, capacity=2, item='after'),
            append_case('full_item_at_end', size=3, capacity=3, item='at_end')]
    out += [append_case('grow_leaves_no_end_outside', size=2, capacity=2, grow='stub', grow_leaves_no_end=True),
            append_case('grow_leaves_no_end_inside', size=2, capacity=2, item=1, grow='stub', grow_leaves_no_end=True),
            append_case('grow_stubbed_inside', size=3, capacity=3, item=2, grow='stub'),
            append_case('grow_stubbed_inside_first', size=3, capacity=3, item=0, grow='stub'),
            append_case('grow_stubbed_at_end', size=3, capacity=3, item='at_end', grow='stub'),
            append_case('grow_stubbed_outside', size=3, capacity=3, grow='stub')]
    high = 0x8000000000000000
    # Sizes from raw pointers (the item lies below them): half as much again, the largest size
    # that still grows by half, and ends below the beginning (negative sizes: no room, or wrap).
    out += [append_case('raw_size_1000', raw=(high, high + ITEM * 1000, high + ITEM * 1000), grow='stub'),
            append_case('raw_size_largest', raw=(high, high + ITEM * 0x0aaaaaaaaaaaaaaa, high + ITEM * 0x0aaaaaaaaaaaaaaa), grow='stub'),
            append_case('raw_end_one_below', raw=(high + ITEM, high, high), grow='stub'),
            append_case('raw_end_two_below', raw=(high + 2 * ITEM, high, high), grow='stub'),
            append_case('raw_end_far_below', raw=(high + ITEM * 0x10000, high, high), grow='stub')]
    return out


def cases(variant):
    return append_cases() if variant == 'append' else pass_cases(variant)


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
