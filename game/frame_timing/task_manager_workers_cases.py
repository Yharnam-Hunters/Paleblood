#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the task manager's worker and the task item clear
(game/frame_timing/task_manager_workers.cpp), for tools/verify.py run.

usage: task_manager_workers_cases.py OUT_ROOT

Writes OUT_ROOT/<function>/*.json for each function. The lane steps (0x01467500, 0x01467300,
0x01467530, 0x014675a0) are stubbed, so every call and its order is seen.
- frame_timing_task_worker_assign_0143e030: busy (any nonzero byte), a null batch, negative
  indexes, the first and last slot, one past the end, a large index, no slots, a slot vector whose
  size is not a whole number of slots, an end before the begin (an unsigned count).
- frame_timing_task_worker_start_0143de20: busy (no change), no slots, slots with and without a
  batch in mode 0, 1, 2 and -1, a mixed vector.
- frame_timing_task_worker_run_0143deb0: not busy (nothing), busy with no slots, mode 0 and
  other modes with and without a batch.
- frame_timing_task_worker_finish_0143df10: not busy (nothing), busy with no slots, idle lanes
  with and without batches in mode 0 and other modes, a running caller-thread lane that stops
  after one or two runs, a run that sets an earlier slot's lane running again (the scan restarts
  from the first slot).
- frame_timing_task_clear_items_0143ec40: no items, items with nulls among them, an array with
  a zero count, the second list with a count (emptied) and without one (left), a free that
  replaces the task's allocator (items after it use the new one; the array the first one).
"""
import json
import os
import struct
import sys

LANE = 0x60
MODE, RUNNING = 0x48, 0x4e
BEGIN, END, BUSY = 0x18, 0x20, 0x38
START_CALLER, START_THREAD, RUN, FINISH = '0x01467500', '0x01467300', '0x01467530', '0x014675a0'


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def base(function, address, cid, returns):
    return function, {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {},
                      'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def batch_value(i):
    return 0x7f3300001000 + 0x100 * i


def worker(c, slots, busy, span=None, order=None):
    """slots: list of (mode, has_batch, running). The lanes lie one after another in one buffer,
    slot i's lane at position order[i] (by default i)."""
    n = len(slots)
    c['buffers']['worker'] = {'size': 0x40, 'bytes': 'c3' * 0x40}
    c['buffers']['slots'] = {'size': 0x10 * max(n, 4), 'bytes': '00' * 0x10 * max(n, 4)}
    c['buffers']['lanes'] = {'size': LANE * max(n, 1), 'bytes': 'a5' * LANE * max(n, 1)}
    begin, end = span if span is not None else (0, 0x10 * n)
    c['memory'] += [{'addr': 'buf:worker+0x18', 'pointer': f'slots+{begin:#x}'},
                    {'addr': 'buf:worker+0x20', 'pointer': f'slots+{end:#x}'},
                    {'addr': 'buf:worker+0x38', 'bytes': f'{busy:02x}'}]
    for i, (mode, has_batch, running) in enumerate(slots):
        lane = LANE * (order[i] if order else i)
        c['memory'] += [{'addr': f'buf:slots+{0x10 * i:#x}', 'pointer': f'lanes+{lane:#x}'},
                        {'addr': f'buf:slots+{0x10 * i + 8:#x}', 'bytes': q(batch_value(i) if has_batch else 0)},
                        {'addr': f'buf:lanes+{lane + MODE:#x}', 'bytes': d(mode)},
                        {'addr': f'buf:lanes+{lane + RUNNING:#x}', 'bytes': f'{running:02x}'}]
    c['args']['rdi'] = 'buf:worker'


def assign_cases():
    out = []

    def add(name, index, count=3, busy=0, batch=0x7f3300009000, span=None):
        f, c = base('frame_timing_task_worker_assign_0143e030', '0x0143e030', name, 'i8')
        worker(c, [(0, False, 0)] * count, busy, span)
        c['args']['rsi'] = index & 0xffffffff
        c['args']['rdx'] = batch
        out.append((f, c))

    for i in (0, 1, 2, 3, 4, 100, 0x7fffffff, -1, -2, -0x80000000):
        add(f'three_index_{i}', i)
    add('busy', 0, busy=1)
    add('busy_high_bit', 1, busy=0x80)
    add('null_batch', 0, batch=0)
    add('null_batch_busy', 0, busy=1, batch=0)
    add('null_batch_negative', -1, batch=0)
    add('no_slots_index_0', 0, count=0)
    add('one_slot_index_0', 0, count=1)
    add('one_slot_index_1', 1, count=1)
    add('partial_slot_index_0', 0, count=4, span=(0, 0x18))
    add('partial_slot_index_1', 1, count=4, span=(0, 0x18))
    add('end_before_begin_index_0', 0, count=4, span=(0x20, 0x0))
    add('end_before_begin_index_1', 1, count=4, span=(0x20, 0x0))
    add('end_before_begin_negative', -1, count=4, span=(0x20, 0x0))
    # A negative index below the (unsigned, wrapped) count: the original refuses it before the compare.
    add('end_before_begin_index_below_count', -7, count=8, span=(0x80, 0x20))
    add('high_batch', 2, batch=0xffffffff00000010)
    return out


def start_cases():
    out = []

    def add(name, slots, busy=0):
        f, c = base('frame_timing_task_worker_start_0143de20', '0x0143de20', name, 'void')
        worker(c, slots, busy)
        if not busy:
            for i, (mode, has_batch, _) in enumerate(slots):
                if has_batch:
                    c['stubs'].append({'address': START_CALLER if mode == 0 else START_THREAD, 'argc': 2})
        out.append((f, c))

    add('busy', [(0, True, 0), (1, True, 0)], busy=1)
    add('busy_other_value', [(0, True, 0)], busy=0x40)
    add('no_slots', [])
    add('caller_lane', [(0, True, 0)])
    add('thread_lane', [(1, True, 0)])
    add('mode_2', [(2, True, 0)])
    add('mode_negative', [(-1, True, 0)])
    add('no_batch_caller', [(0, False, 0)])
    add('no_batch_thread', [(1, False, 0)])
    add('mixed', [(0, True, 0), (1, True, 0), (0, False, 0), (3, True, 0), (1, False, 0), (0, True, 1)])
    add('high_mode', [(0x80000000, True, 0), (0x100, True, 0)])
    return out


def run_cases():
    out = []

    def add(name, slots, busy=1):
        f, c = base('frame_timing_task_worker_run_0143deb0', '0x0143deb0', name, 'void')
        worker(c, slots, busy)
        if busy:
            for i, (mode, has_batch, _) in enumerate(slots):
                if has_batch and mode == 0:
                    c['stubs'].append({'address': RUN, 'argc': 1})
        else:
            c['stubs'].append({'address': RUN, 'argc': 1})   # scripted but must not be called
        out.append((f, c))

    add('not_busy', [(0, True, 0), (0, True, 0)], busy=0)
    add('busy_other_value', [(0, True, 0)], busy=0x80)
    add('no_slots', [])
    add('caller_lane', [(0, True, 0)])
    add('thread_lane', [(1, True, 0)])
    add('caller_no_batch', [(0, False, 0)])
    add('mixed', [(0, True, 0), (1, True, 0), (0, False, 0), (0, True, 1), (-1, True, 0), (0x100, True, 0)])
    return out


def finish_cases():
    out = []

    def add(name, slots, busy=1, runs=(), order=None):
        """runs: per expected lane_run call, the writes its stub makes (to the lane, by offset)."""
        f, c = base('frame_timing_task_worker_finish_0143df10', '0x0143df10', name, 'void')
        worker(c, slots, busy, order=order)
        for writes in runs:
            c['stubs'].append({'address': RUN, 'argc': 1, 'writes': [{'arg': 0, 'offset': o, 'bytes': b}
                                                                      for o, b in writes]})
        if busy:
            for mode, has_batch, _ in slots:
                if has_batch and mode == 0:
                    c['stubs'].append({'address': FINISH, 'argc': 1})
        else:
            c['stubs'] += [{'address': RUN, 'argc': 1}, {'address': FINISH, 'argc': 1}]
        out.append((f, c))

    stop = [(RUNNING, '00')]
    add('not_busy', [(0, True, 1), (0, True, 0)], busy=0)
    add('no_slots', [])
    add('busy_other_value', [(0, True, 0)], busy=0x20)
    add('idle_caller_lanes', [(0, True, 0), (0, False, 0), (0, True, 0)])
    add('idle_thread_lanes', [(1, True, 0), (2, False, 0), (-1, True, 0)])
    add('idle_mixed', [(0, True, 0), (1, True, 0), (0, False, 0), (5, True, 0), (0, True, 0)])
    add('running_caller_lane', [(0, True, 0), (0, True, 1)], runs=[stop])
    add('running_caller_lane_first', [(0, True, 1), (1, True, 0)], runs=[stop])
    add('running_without_batch', [(0, False, 1), (0, True, 0)], runs=[stop])
    add('runs_twice', [(0, True, 0), (0, False, 0), (0, True, 1)], runs=[[], stop])
    add('two_running', [(0, True, 1), (0, True, 1)], runs=[stop, stop])
    # Slot 0's lane lies right after slot 1's, so slot 1's run can set it running again; only a scan
    # that restarts from the first slot runs it.
    add('restart_from_first', [(0, True, 0), (0, True, 1), (1, True, 0)],
        runs=[[(RUNNING, '00'), (LANE + RUNNING, '01')], stop], order=[1, 0, 2])
    add('running_flag_other_value', [(0, True, 0x80)], runs=[stop])
    return out


# clear_items: allocator A at blob+0, allocator B at blob+8, the task at blob+0x10.
TASK = 0x10
LOCK, UNLOCK, FREE = '0x00500100', '0x00500200', '0x00500700'


def clear_case(name, items=None, count=None, second=(0, 0), switch_at=None):
    """items: list of item values (0 for a null slot) or None for no array; count defaults to its
    length. switch_at: the free call (0-based) whose stub makes allocator B the task's."""
    f, c = base('frame_timing_task_clear_items_0143ec40', '0x0143ec40', name, 'void')
    c['buffers']['blob'] = {'size': TASK + 0xa0, 'bytes': '3c' * (TASK + 0xa0)}
    c['buffers']['alloc_vt'] = {'size': 0x78, 'bytes': '00' * 0x78}
    c['buffers']['lock_vt'] = {'size': 0x30, 'bytes': '00' * 0x30}
    t = TASK
    c['memory'] += [{'addr': 'buf:blob+0x0', 'pointer': 'alloc_vt'}, {'addr': 'buf:blob+0x8', 'pointer': 'alloc_vt'},
                    {'addr': 'buf:alloc_vt+0x70', 'guest': FREE},
                    {'addr': f'buf:blob+{t + 0x80:#x}', 'pointer': 'blob+0x0'},
                    {'addr': f'buf:blob+{t + 0x90:#x}', 'pointer': 'lock_vt'},
                    {'addr': 'buf:lock_vt+0x18', 'guest': LOCK}, {'addr': 'buf:lock_vt+0x28', 'guest': UNLOCK},
                    {'addr': f'buf:blob+{t + 0x70:#x}', 'bytes': q(second[0])},
                    {'addr': f'buf:blob+{t + 0x78:#x}', 'bytes': q(second[1])}]
    if items is None:
        c['memory'].append({'addr': f'buf:blob+{t + 0x60:#x}', 'bytes': q(0)})
        n = 0 if count is None else count
    else:
        c['buffers']['items'] = {'size': 8 * max(1, len(items)), 'bytes': 'e1' * 8 * max(1, len(items))}
        c['memory'] += [{'addr': f'buf:items+{8 * i:#x}', 'bytes': q(v)} for i, v in enumerate(items)]
        c['memory'].append({'addr': f'buf:blob+{t + 0x60:#x}', 'pointer': 'items'})
        n = len(items) if count is None else count
    c['memory'].append({'addr': f'buf:blob+{t + 0x68:#x}', 'bytes': q(n)})
    c['stubs'].append({'address': LOCK, 'argc': 2})
    frees = sum(1 for v in (items or [])[:n] if v) + (1 if items is not None else 0)
    for k in range(frees):
        stub = {'address': FREE, 'argc': 2}
        if k == switch_at:
            stub['writes'] = [{'arg': 0, 'offset': TASK + 0x80, 'pointer': 'blob+0x8'}]
        c['stubs'].append(stub)
    c['stubs'].append({'address': UNLOCK, 'argc': 1})
    c['args']['rdi'] = f'buf:blob+{t:#x}'
    return f, c


def clear_cases():
    return [clear_case('no_items'), clear_case('one_item', [0x7f4400000010]),
            clear_case('three_items', [0x7f4400000010, 0x7f4400000020, 0x7f4400000030]),
            clear_case('nulls_among_items', [0, 0x7f4400000020, 0, 0x7f4400000040, 0]),
            clear_case('all_null', [0, 0, 0]),
            clear_case('array_count_zero', [0x7f4400000010], count=0),
            clear_case('count_below_array', [0x7f4400000010, 0x7f4400000020, 0x7f4400000030], count=2),
            clear_case('second_list_counted', [0x7f4400000010], second=(0x7f5500000000, 3)),
            clear_case('second_list_count_only', None, second=(0, 1)),
            clear_case('second_list_not_counted', [0x7f4400000010], second=(0x7f5500000000, 0)),
            clear_case('second_count_high_bit', None, second=(0x10, 0x8000000000000000)),
            clear_case('allocator_replaced_first', [0x7f4400000010, 0x7f4400000020, 0x7f4400000030],
                       switch_at=0),
            clear_case('allocator_replaced_last_item', [0x7f4400000010, 0x7f4400000020], switch_at=1),
            clear_case('allocator_replaced_no_items', [0, 0], switch_at=0)]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    counts = {}
    for f, c in assign_cases() + start_cases() + run_cases() + finish_cases() + clear_cases():
        out = os.path.join(sys.argv[1], f)
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as fh:
            json.dump(c, fh, indent=1)
        counts[f] = counts.get(f, 0) + 1
    for f, n in counts.items():
        print(f'{n} cases written to {os.path.join(sys.argv[1], f)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
