#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the task manager's helpers (game/frame_timing/task_manager_helpers.cpp), for
tools/verify.py run.

usage: task_manager_helpers_cases.py OUT_ROOT

Writes OUT_ROOT/<function>/*.json for each function:
- frame_timing_task_entry_count_01389df0: empty, one, many entries; sizes that are not a whole
  number of entries; the end before the begin; counts past 32 bits.
- frame_timing_task_entry_at_01389e10: negative indexes, the first and last entry, one past the
  end, the largest index, an empty vector and an end before the begin (an unsigned count).
- frame_timing_task_batch_add_0143d7c0: a null job (nothing pushed); jobs pushed by the game's own
  deque push_back at the start of a block, inside one, at a wrapped offset.
- frame_timing_task_lookup_0143f720: every key of a map, ids between, below and above them, id 0
  (never looked up, even when the map holds key 0, and with no table), an empty map, keys with the
  top bit set (unsigned order).
- frame_timing_task_record_open_0143c360: every state that is not idle (no change); idle with no
  items, with an index out of range (count 0 or negative), and with the first item of each kind.
- frame_timing_task_record_close_0143c450: the closing state and every other one.
"""
import json
import os
import struct
import sys

GUARD_GOT = '0x057e3be0'
GUARD = {'guard': {'size': 0x10, 'bytes': '5a' * 0x10}}
ENTRY_SIZE = 0x88
ENTRIES = 0x7f1200000000   # a fake vector begin: these two only compute addresses


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def base(function, address, cid, returns):
    return function, {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {},
                      'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def guarded(c):
    c['buffers'].update(GUARD)
    c['memory'].append({'addr': GUARD_GOT, 'pointer': 'guard'})
    return c


# entry_count / entry_at
def dispatcher(c, begin, end):
    c['buffers']['disp'] = {'size': 0x30, 'bytes': 'c3' * 0x30}
    c['memory'] += [{'addr': 'buf:disp+0x18', 'bytes': q(begin)}, {'addr': 'buf:disp+0x20', 'bytes': q(end)}]
    c['args']['rdi'] = 'buf:disp'


def entry_count_cases():
    spans = {'empty': (ENTRIES, ENTRIES), 'null_vector': (0, 0), 'one': (ENTRIES, ENTRIES + ENTRY_SIZE),
             'two': (ENTRIES, ENTRIES + 2 * ENTRY_SIZE), 'seventeen': (ENTRIES, ENTRIES + 17 * ENTRY_SIZE),
             'many': (ENTRIES, ENTRIES + 1000 * ENTRY_SIZE), 'odd_bytes_1': (ENTRIES, ENTRIES + 1),
             'odd_bytes_8': (ENTRIES, ENTRIES + 8), 'odd_bytes_0x87': (ENTRIES, ENTRIES + 0x87),
             'odd_bytes_0x90': (ENTRIES, ENTRIES + 0x90), 'end_before_begin': (ENTRIES + ENTRY_SIZE, ENTRIES),
             'end_far_before_begin': (ENTRIES + 5 * ENTRY_SIZE, ENTRIES),
             'past_32_bits': (0, ENTRY_SIZE * 0x100000003), 'top_bit': (0, 0x8000000000000000),
             'all_ones': (0, 0xffffffffffffffff), 'low_31_bits': (0, ENTRY_SIZE * 0x7fffffff),
             'bit_31': (0, ENTRY_SIZE * 0x80000000)}
    out = []
    for name, (b, e) in spans.items():
        f, c = base('frame_timing_task_entry_count_01389df0', '0x01389df0', name, 'i32')
        dispatcher(c, b, e)
        out.append((f, c))
    return out


def entry_at_cases():
    out = []

    def add(name, count_bytes, index, begin=ENTRIES):
        f, c = base('frame_timing_task_entry_at_01389e10', '0x01389e10', name, 'i64')
        dispatcher(c, begin, begin + count_bytes)
        c['args']['rsi'] = index & 0xffffffff
        out.append((f, c))

    five = 5 * ENTRY_SIZE
    for i in (0, 1, 3, 4, 5, 6, 100, 0x7fffffff, -1, -2, -5, -0x80000000):
        add(f'five_index_{i}', five, i)
    add('empty_index_0', 0, 0)
    add('empty_index_minus_1', 0, -1)
    add('one_index_0', ENTRY_SIZE, 0)
    add('one_index_1', ENTRY_SIZE, 1)
    add('end_before_begin_index_0', -ENTRY_SIZE, 0)
    add('end_before_begin_index_large', -ENTRY_SIZE, 0x7fffffff)
    add('end_before_begin_index_negative', -ENTRY_SIZE, -1)
    add('far_end_before_begin_index_min', -5 * ENTRY_SIZE, -0x80000000)
    add('partial_entry_index_0', 0x40, 0)
    add('one_and_a_half_index_1', ENTRY_SIZE + 0x40, 1)
    add('null_begin_index_2', five, 2, begin=0)
    add('high_begin_index_4', five, 4, begin=0xffffffffffff0000)
    return out


# batch_add: the batch is a std::deque<TaskJob *> (map at +8, map size +0x10, offset +0x18, size
# +0x20, allocator +0x28), two jobs per block. Every block exists, so push_back allocates nothing.
MAP_SIZE = 8


def batch_case(name, job, offset=0, size=0):
    f, c = base('frame_timing_task_batch_add_0143d7c0', '0x0143d7c0', name, 'void')
    c['buffers']['batch'] = {'size': 0x30, 'bytes': '00' * 0x30}
    c['buffers']['map'] = {'size': 8 * MAP_SIZE, 'bytes': '00' * 8 * MAP_SIZE}
    c['buffers']['blocks'] = {'size': 0x10 * MAP_SIZE, 'bytes': 'e7' * 0x10 * MAP_SIZE}
    c['memory'] += [{'addr': 'buf:batch+0x8', 'pointer': 'map'}, {'addr': 'buf:batch+0x10', 'bytes': q(MAP_SIZE)},
                    {'addr': 'buf:batch+0x18', 'bytes': q(offset)}, {'addr': 'buf:batch+0x20', 'bytes': q(size)},
                    {'addr': 'buf:batch+0x28', 'bytes': q(0x7f5500000000)}]
    c['memory'] += [{'addr': f'buf:map+{8 * i:#x}', 'pointer': f'blocks+{0x10 * i:#x}'} for i in range(MAP_SIZE)]
    c['args'] = {'rdi': 'buf:batch', 'rsi': job}
    return f, guarded(c)


def batch_cases():
    return [batch_case('null_job', 0),
            batch_case('null_job_nonempty', 0, offset=3, size=4),
            batch_case('first_job', 0x7f3300001000),
            batch_case('second_slot_of_block', 0x7f3300002000, size=1),
            batch_case('next_block', 0x7f3300003000, size=2),
            batch_case('offset_inside', 0x7f3300004000, offset=5, size=4),
            batch_case('offset_wraps', 0x7f3300005000, offset=2 * MAP_SIZE + 3, size=1),
            batch_case('last_slot', 0x7f3300006000, offset=2 * MAP_SIZE - 1, size=0),
            batch_case('job_all_ones', 0xffffffffffffffff, size=3),
            batch_case('job_one', 1, offset=1, size=6)]


# lookup: std::map<uint32_t, Task *> with a head node; nodes 0x30 bytes (left, parent, right, color,
# is_nil, key at +0x20, value at +0x28).
NODE = 0x30


def tree_case(name, keys, task_id, table=True):
    f, c = base('frame_timing_task_lookup_0143f720', '0x0143f720', name, 'i64')
    keys = sorted(keys)
    n = len(keys) + 1
    c['buffers']['tree'] = {'size': NODE * n, 'bytes': 'ee' * NODE * n}   # the head's value is not null
    c['buffers']['table'] = {'size': 0x20, 'bytes': '9b' * 0x20}
    mem = c['memory']

    def ptr(node):
        return f'tree+{NODE * node:#x}'

    def at(node, field):
        return f'buf:tree+{NODE * node + field:#x}'

    def link(node, field, target):
        mem.append({'addr': at(node, field), 'pointer': ptr(target)})

    def build(lo, hi, parent):   # nodes 1..n-1 hold keys[0..]; returns the subtree's root or 0 (head)
        if lo > hi:
            return 0
        mid = (lo + hi) // 2
        node = mid + 1
        link(node, 0x8, parent)
        link(node, 0x0, build(lo, mid - 1, node))
        link(node, 0x10, build(mid + 1, hi, node))
        mem.append({'addr': at(node, 0x18), 'bytes': '0000'})
        mem.append({'addr': at(node, 0x20), 'bytes': d(keys[mid]) + 'cdcdcdcd'})
        mem.append({'addr': at(node, 0x28), 'bytes': q(0x7f4400000000 + 0x100 * mid + 0x10)})
        return node

    root = build(0, len(keys) - 1, 0)
    link(0, 0x8, root)
    link(0, 0x0, 1 if keys else 0)
    link(0, 0x10, len(keys) if keys else 0)
    mem.append({'addr': 'buf:tree+0x18', 'bytes': '0101'})
    mem.append({'addr': 'buf:table+0x18', 'pointer': 'tree'})
    c['args'] = {'rdi': 'buf:table' if table else 0, 'rsi': task_id}
    return f, c


def lookup_cases():
    keys = [3, 10, 11, 25, 40, 41, 77, 0x1000]
    out = [tree_case(f'key_{k:#x}', keys, k) for k in keys]
    for i in (1, 2, 4, 12, 26, 39, 42, 78, 0xfff, 0x1001, 0x7fffffff, 0x80000000, 0xffffffff):
        out.append(tree_case(f'missing_{i:#x}', keys, i))
    out.append(tree_case('id_0_with_key_0', [0, 5, 9], 0))
    out.append(tree_case('key_5_with_key_0', [0, 5, 9], 5))
    out.append(tree_case('id_0_no_table', [], 0, table=False))
    out.append(tree_case('empty_map', [], 7))
    out.append(tree_case('single_key_found', [9], 9))
    out.append(tree_case('single_key_below', [9], 8))
    out.append(tree_case('single_key_above', [9], 10))
    top = [5, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff]
    out += [tree_case(f'top_bit_key_{k:#x}', top, k) for k in top]
    out.append(tree_case('top_bit_missing_0x90000000', top, 0x90000000))
    out.append(tree_case('top_bit_missing_6', top, 6))
    return out


# record_open / record_close
def record_case(function, address, name, state, index=9, items=None, count=0, flags=(0xffffffff, 0xffffffff),
                kind_first=0):
    f, c = base(function, address, name, 'void')
    c['buffers']['record'] = {'size': 0x70, 'bytes': 'a5' * 0x70}
    mem = c['memory']
    mem += [{'addr': 'buf:record+0x8', 'bytes': d(count)}, {'addr': 'buf:record+0x18', 'bytes': d(index)},
            {'addr': 'buf:record+0x1c', 'bytes': d(state)}, {'addr': 'buf:record+0x20', 'bytes': d(flags[0])},
            {'addr': 'buf:record+0x24', 'bytes': d(flags[1])}]
    if items is None:
        mem.append({'addr': 'buf:record+0x10', 'bytes': q(0)})
    else:
        c['buffers']['items'] = {'size': 8 * len(items), 'bytes': ''.join(d(kind_first) + d(k) for k in items)}
        mem.append({'addr': 'buf:record+0x10', 'pointer': 'items'})
    c['args'] = {'rdi': 'buf:record'}
    return f, guarded(c)


def record_open_cases():
    def rec(name, state, **kw):
        return record_case('frame_timing_task_record_open_0143c360', '0x0143c360', name, state, **kw)

    kinds = [4, 2, 2, 4]
    out = [rec(f'state_{s & 0xffffffff:#x}', s, items=kinds, count=4) for s in (1, 2, 3, 4, 5, -1, 0x100)]
    out.append(rec('idle_no_items', 0, count=4))
    out.append(rec('idle_no_items_flags_clear', 0, flags=(0, 0)))
    for k in (0, 1, 2, 3, 4, 5, 6, 0x102, 0x104, 0xfffffffe, 0xfffffffc):
        out.append(rec(f'idle_kind_{k:#x}', 0, items=[k, 7, 2, 4], count=4))
    out.append(rec('idle_kind_2_flags_clear', 0, items=[2], count=1, flags=(0, 0)))
    out.append(rec('idle_kind_4_flags_clear', 0, items=[4], count=1, flags=(0, 0)))
    out.append(rec('idle_kind_4_first_word_2', 0, items=[4], count=1, kind_first=2))
    out.append(rec('idle_kind_0_first_word_4', 0, items=[0], count=1, kind_first=4))
    out.append(rec('idle_old_index_points_to_kind_4', 0, index=1, items=[0, 4], count=2))
    out.append(rec('idle_negative_old_index', 0, index=-1, items=[2, 4], count=2))
    for n in (0, -1, -0x80000000):
        out.append(rec(f'idle_count_{n & 0xffffffff:#x}', 0, items=[4, 2], count=n))
    out.append(rec('idle_count_large', 0, items=[2], count=0x7fffffff))
    return out


def record_close_cases():
    def rec(name, state, **kw):
        return record_case('frame_timing_task_record_close_0143c450', '0x0143c450', name, state, **kw)

    out = [rec(f'state_{s & 0xffffffff:#x}', s, items=[4, 2], count=2) for s in (0, 1, 2, 3, 5, -1, 0x104, -4)]
    out.append(rec('closing', 4, items=[4, 2], count=2))
    out.append(rec('closing_no_items', 4))
    out.append(rec('closing_index_0_flags_clear', 4, index=0, flags=(0, 0)))
    out.append(rec('closing_index_minus_1', 4, index=-1, flags=(1, 0)))
    return out


def all_cases():
    return entry_count_cases() + entry_at_cases() + batch_cases() + lookup_cases() + record_open_cases() + \
        record_close_cases()


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    counts = {}
    for function, c in all_cases():
        d_ = os.path.join(sys.argv[1], function)
        os.makedirs(d_, exist_ok=True)
        with open(os.path.join(d_, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
        counts[function] = counts.get(function, 0) + 1
    for function, n in counts.items():
        print(f'{function}: {n} cases')
    return 0


if __name__ == '__main__':
    sys.exit(main())
