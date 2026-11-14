#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the worker lane's steps (game/frame_timing/worker_lanes.cpp), for tools/verify.py run.

usage: worker_lanes_cases.py OUT_DIR caller|thread|run|finish|take|signal

Writes OUT_DIR/*.json for one function. The lane (0x60 bytes) and its tasks lie in one buffer,
`mem`, filled with a pattern so that every byte a step leaves or writes is compared. The batch's
take (0x0143d760) and the sync object's signal (0x02aa4080) are stubbed, and each task's vtable
points its run (slot 2) at a stubbed fake address, so every call, its arguments and the argument
word a run is given are compared. A run or a take can also write into the lane (the stop request,
the mode, the batch pointer) through its argument with an offset, as the real ones could.
- caller (frame_timing_worker_lane_start_on_caller_01467500): a null batch, the lane running or
  taken (also by other nonzero bytes), both, a free lane in modes 0, 1 and -1, a stop request
  and an earlier batch that are replaced.
- thread (frame_timing_worker_lane_start_on_thread_01467300): the same refusals; a free lane in
  modes 1, -1, 0x80000000 and 0x100 (its sync object signalled); mode 0 with an empty batch, one
  and three tasks, a run that requests a stop (no take after it), a stop request left from
  before (cleared first), runs that change the shared argument word, a take that swaps the lane's
  batch (read again for every task) and one that changes the lane's mode (not read again).
- run (frame_timing_worker_lane_run_01467530): not taken, not running, neither; a task in modes
  0, 1, -1 and 0x7fffffff, an empty batch (running cleared), a take that changes the lane's mode
  (the argument is read after it), a run that writes the argument word.
- finish (frame_timing_worker_lane_finish_014675a0): not taken (nothing changes, running or not),
  taken by 1, 0x80 and 0xff, with a stop request and a batch.
- take (frame_timing_task_batch_take_next_0143d760): a batch (proxy, block map, map size, front,
  size) over blocks of two task pointers: empty, one task left (the front back to 0), more left;
  the front in a block's first and second slot, past the map (masked), at the 64-bit top (it
  wraps); a map of one block; a proxy naming another container (its map is read, the batch's
  front and size are used).
- signal (frame_timing_lane_sync_signal_02aa4080): the sync object's created slot (a stub)
  answering 1, 0, 0xff and 0x100 (only the low byte counts); the fatal error (0x024b55b0, with the
  file, line and message) and the event's signal (0x02a3b050) are stubs; each callee has a spare
  scripted call.
"""
import json
import os
import struct
import sys

ADDRESSES = {
    'take': '0x0143d760',
    'signal': '0x02aa4080',
    'caller': '0x01467500',
    'thread': '0x01467300',
    'run': '0x01467530',
    'finish': '0x014675a0',
}
FUNCTIONS = {
    'caller': 'frame_timing_worker_lane_start_on_caller_01467500',
    'thread': 'frame_timing_worker_lane_start_on_thread_01467300',
    'run': 'frame_timing_worker_lane_run_01467530',
    'finish': 'frame_timing_worker_lane_finish_014675a0',
}
TAKE, SIGNAL = '0x0143d760', '0x02aa4080'
IS_CREATED, FATAL_ERROR, EVENT_SIGNAL = '0x00500300', '0x024b55b0', '0x02a3b050'
SYNC_FILE, SYNC_MESSAGE, SYNC_LINE = 0x04c6ea8c, 0x04c6eaf5, 0x83
RUN = '0x00500200'          # a spare address for the tasks' run slot, stubbed

SYNC, MODE, RUNNING, STOP, TAKEN, BATCH = 0x10, 0x48, 0x4e, 0x4f, 0x50, 0x58
TASKS = 0x100               # task i at TASKS + 0x20 * i in `mem`
BATCHES = 0x300             # batch j at BATCHES + 0x40 * j in `mem` (opaque to the lane)


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def task_at(i):
    return TASKS + 0x20 * i


def batch_at(j):
    return BATCHES + 0x40 * j


class Case:
    def __init__(self, mode_name, cid, mode=0, running=0, stop=0, taken=0, old_batch=None):
        self.c = {'schema': 1, 'address': ADDRESSES[mode_name], 'id': cid, 'returns': 'void',
                  'args': {'rdi': 'buf:mem'}, 'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}
        self.c['buffers']['mem'] = {'size': 0x400, 'bytes': 'a5' * 0x400}
        self.c['buffers']['vtable'] = {'size': 0x20, 'bytes': '5a' * 0x20}
        self.c['memory'] += [
            {'addr': 'buf:vtable+0x10', 'guest': RUN},
            {'addr': f'buf:mem+{MODE:#x}', 'bytes': d(mode)},
            {'addr': f'buf:mem+{RUNNING:#x}', 'bytes': f'{running:02x}'},
            {'addr': f'buf:mem+{STOP:#x}', 'bytes': f'{stop:02x}'},
            {'addr': f'buf:mem+{TAKEN:#x}', 'bytes': f'{taken:02x}'},
        ]
        if old_batch is not None:
            self.c['memory'].append({'addr': f'buf:mem+{BATCH:#x}', 'pointer': f'mem+{batch_at(old_batch):#x}'})
        for i in range(8):
            self.c['memory'].append({'addr': f'buf:mem+{task_at(i):#x}', 'pointer': 'vtable'})

    def batch_arg(self, j):
        self.c['args']['rsi'] = f'buf:mem+{batch_at(j):#x}' if j is not None else 0

    def take(self, batch, task, writes=()):
        """The take from batch j gives task i (None: the batch is empty). writes: (lane field, bytes)."""
        s = {'address': TAKE, 'argc': 1, 'ret': f'buf:mem+{task_at(task):#x}' if task is not None else 0}
        if writes:
            s['writes'] = [{'arg': 0, 'offset': field - batch_at(batch), 'bytes': data} for field, data in writes]
        self.c['stubs'].append(s)

    def run(self, task, writes=(), argument=None):
        """Task i's run. writes: (lane field, bytes) into the lane; argument: a new argument word."""
        # The argument word lives in the caller's frame: its contents are compared, not its address.
        s = {'address': RUN, 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 4}]}
        w = [{'arg': 0, 'offset': field - task_at(task), 'bytes': data} for field, data in writes]
        if argument is not None:
            w.append({'arg': 1, 'bytes': d(argument)})
        if w:
            s['writes'] = w
        self.c['stubs'].append(s)

    def signal(self):
        self.c['stubs'].append({'address': SIGNAL, 'argc': 1})


def caller_cases():
    out = []

    def add(cid, batch=0, **kw):
        c = Case('caller', cid, **kw)
        c.batch_arg(batch)
        out.append(c)

    add('null_batch', None)
    add('null_batch_taken', None, taken=1)
    add('running', running=1)
    add('running_other_byte', running=0x80)
    add('taken', taken=1)
    add('taken_other_byte', taken=0xfe)
    add('running_and_taken', running=1, taken=1)
    add('free_mode_0', 0)
    add('free_mode_1', 1, mode=1)
    add('free_mode_negative', 2, mode=-1)
    add('stop_request_cleared', stop=1)
    add('stop_request_other_byte_cleared', stop=0xff)
    add('earlier_batch_replaced', 1, old_batch=3)
    return out


def thread_cases():
    out = []

    def add(cid, batch=0, mode=0, **kw):
        c = Case('thread', cid, mode=mode, **kw)
        c.batch_arg(batch)
        out.append(c)
        return c

    add('null_batch', None, mode=1)
    add('null_batch_mode_0', None)
    add('running', mode=1, running=1)
    add('taken', mode=1, taken=1)
    add('taken_mode_0', taken=0x40)
    add('running_and_taken', mode=1, running=1, taken=1)
    for mode in (1, -1, 0x80000000, 0x100, 2):
        add(f'signalled_mode_{mode & 0xffffffff:x}', 1, mode=mode).signal()
    add('signalled_stop_cleared', 2, mode=1, stop=1).signal()
    add('signalled_earlier_batch_replaced', 2, mode=3, old_batch=0).signal()
    c = add('mode_0_empty', 1)
    c.take(1, None)
    c = add('mode_0_one_task', 1)
    c.take(1, 0); c.run(0); c.take(1, None)
    c = add('mode_0_three_tasks', 2)
    for i in (2, 0, 5):
        c.take(2, i); c.run(i)
    c.take(2, None)
    c = add('mode_0_stop_after_first', 1)
    c.take(1, 0); c.run(0, writes=[(STOP, '01')])
    c = add('mode_0_stop_other_byte_after_second', 1)
    c.take(1, 0); c.run(0); c.take(1, 1); c.run(1, writes=[(STOP, '80')])
    c = add('mode_0_stop_before_cleared', 1, stop=1)
    c.take(1, 3); c.run(3); c.take(1, None)
    c = add('mode_0_argument_changed', 1)
    c.take(1, 0); c.run(0, argument=7); c.take(1, 1); c.run(1, argument=-3); c.take(1, 2); c.run(2); c.take(1, None)
    c = add('mode_0_batch_swapped_by_take', 1)
    c.take(1, 0, writes=[(BATCH, None)]); c.run(0); c.take(2, 4); c.run(4); c.take(2, None)
    c = add('mode_0_batch_swapped_by_run', 1)
    c.take(1, 0); c.run(0, writes=[(BATCH, None)]); c.take(3, None)
    c = add('mode_0_mode_changed_by_run', 1)
    c.take(1, 0); c.run(0, writes=[(MODE, d(5))]); c.take(1, 1); c.run(1); c.take(1, None)
    c = add('mode_0_running_cleared_by_run', 1)
    c.take(1, 0); c.run(0, writes=[(RUNNING, '00')]); c.take(1, None)
    c = add('mode_0_taken_set_by_run', 1)
    c.take(1, 0); c.run(0, writes=[(TAKEN, '01')]); c.take(1, None)
    return out


def run_cases():
    out = []

    def add(cid, **kw):
        c = Case('run', cid, old_batch=1, **kw)
        out.append(c)
        return c

    add('not_taken', running=1)
    add('not_running', taken=1)
    add('neither')
    add('not_taken_other_bytes', running=0x80, taken=0)
    for mode in (0, 1, -1, 0x7fffffff):
        c = add(f'task_mode_{mode & 0xffffffff:x}', mode=mode, running=1, taken=1)
        c.take(1, 2); c.run(2)
    c = add('task_other_flag_bytes', mode=3, running=0x10, taken=0x20)
    c.take(1, 4); c.run(4)
    c = add('empty_batch', running=1, taken=1)
    c.take(1, None)
    c = add('empty_batch_stop_requested', running=1, taken=1, stop=1)
    c.take(1, None)
    c = add('mode_changed_by_take', mode=1, running=1, taken=1)
    c.take(1, 0, writes=[(MODE, d(9))]); c.run(0)
    c = add('argument_written_by_run', mode=2, running=1, taken=1)
    c.take(1, 1); c.run(1, argument=0x55)
    c = add('running_cleared_by_take', mode=0, running=1, taken=1)
    c.take(1, 0, writes=[(RUNNING, '00')]); c.run(0)
    return out


def finish_cases():
    out = []

    def add(cid, **kw):
        out.append(Case('finish', cid, old_batch=2, **kw))

    add('not_taken')
    add('not_taken_running', running=1, stop=1)
    for t in (1, 0x80, 0xff):
        add(f'taken_{t:x}', taken=t)
    add('taken_running_stop', taken=1, running=1, stop=1)
    add('taken_mode_1', taken=1, mode=1, running=1)
    return out


class Plain:
    """A case built as a dict, for resolve()."""
    def __init__(self, c):
        self.c = c


def plain(address, cid, returns):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {}, 'buffers': {},
            'memory': [], 'imports': [], 'stubs': []}


def take_case(cid, size=3, front=0, block_count=4, other=False):
    """Blocks of two task pointers; task k sits at tasks+0x10*k, block b holds tasks 2b and 2b+1."""
    c = plain(ADDRESSES['take'], cid, 'i64')
    c['buffers'] = {'batch': {'size': 0x28, 'bytes': 'c6' * 0x28}, 'proxy': {'size': 0x10, 'bytes': '3c' * 0x10},
                    'tasks': {'size': 0x200, 'bytes': '5a' * 0x200}}
    container = 'other' if other else 'batch'
    if other:
        c['buffers']['other'] = {'size': 0x28, 'bytes': '9d' * 0x28}
    c['memory'] += [{'addr': 'buf:batch', 'pointer': 'proxy'}, {'addr': 'buf:proxy', 'pointer': container},
                    {'addr': 'buf:batch+0x18', 'bytes': q(front) + q(size)}]
    map_name = f'{container}_map'
    c['buffers'][map_name] = {'size': 8 * block_count, 'bytes': '77' * 8 * block_count}
    c['memory'] += [{'addr': f'buf:{container}+0x8', 'pointer': map_name},
                    {'addr': f'buf:{container}+0x10', 'bytes': q(block_count)}]
    for b in range(block_count):
        block = f'block{b}'
        c['buffers'][block] = {'size': 0x10, 'bytes': '88' * 0x10}
        c['memory'].append({'addr': f'buf:{map_name}+{8 * b:#x}', 'pointer': block})
        for s in range(2):
            c['memory'].append({'addr': f'buf:{block}+{8 * s:#x}', 'pointer': f'tasks+{0x10 * (2 * b + s) + 0x40 * other:#x}'})
    c['args'] = {'rdi': 'buf:batch'}
    return Plain(c)


def take_cases():
    return [take_case('empty', size=0), take_case('empty_front_kept', size=0, front=5),
            take_case('one_left', size=1, front=2), take_case('one_left_odd_front', size=1, front=3),
            take_case('first_slot', front=4), take_case('second_slot', front=5),
            take_case('front_past_map', front=11), take_case('front_far_past_map', front=0x1000000000000001),
            take_case('front_at_top', front=0xffffffffffffffff, size=2),
            take_case('one_block_map', front=3, block_count=1), take_case('eight_block_map', front=13, block_count=8),
            take_case('size_at_top', size=0xffffffffffffffff, front=6),
            take_case('proxy_names_other', front=2, other=True),
            take_case('proxy_names_other_one_left', front=7, size=1, other=True)]


def signal_case(cid, created=1):
    c = plain(ADDRESSES['signal'], cid, 'void')
    c['buffers'] = {'sync': {'size': 0x38, 'bytes': 'b4' * 0x38}, 'vtable': {'size': 0x20, 'bytes': '6c' * 0x20}}
    c['memory'] = [{'addr': 'buf:sync', 'pointer': 'vtable'}, {'addr': 'buf:vtable+0x10', 'guest': IS_CREATED}]
    c['args'] = {'rdi': 'buf:sync'}
    c['stubs'] = [{'address': IS_CREATED, 'argc': 1, 'ret': created}]
    if created & 0xff == 0:
        c['stubs'].append({'address': FATAL_ERROR, 'argc': 3, 'argu32': [1]})
    c['stubs'] += [{'address': EVENT_SIGNAL, 'argc': 1}, {'address': IS_CREATED, 'argc': 1},
                   {'address': FATAL_ERROR, 'argc': 3}, {'address': EVENT_SIGNAL, 'argc': 1}]
    return Plain(c)


def signal_cases():
    return [signal_case('created'), signal_case('not_created', created=0), signal_case('created_ff', created=0xff),
            signal_case('only_upper_byte', created=0x100), signal_case('created_upper_bits', created=0xffffff01)]


GENERATORS = {'caller': caller_cases, 'thread': thread_cases, 'run': run_cases, 'finish': finish_cases,
              'take': take_cases, 'signal': signal_cases}


def resolve(c):
    """A write of the lane's batch pointer (bytes None) points it at the next batch."""
    for s in c.c['stubs']:
        for w in s.get('writes', []):
            if w.get('bytes') is None:
                del w['bytes']
                w['pointer'] = f'mem+{batch_at(2) if s["address"] == TAKE else batch_at(3):#x}'
    return c.c


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in GENERATORS:
        sys.exit(__doc__)
    out_dir, mode = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    cases = GENERATORS[mode]()
    for c in cases:
        case = resolve(c)
        with open(os.path.join(out_dir, case['id'] + '.json'), 'w') as f:
            json.dump(case, f, indent=1)
            f.write('\n')
    print(f'{len(cases)} cases written to {out_dir}')


if __name__ == '__main__':
    main()
