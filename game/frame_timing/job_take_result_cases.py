#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_job_take_result (0x024bd150); see job_fixtures.py.

usage: job_take_result_cases.py OUT_DIR

A handle without a job; each mode (poll 0, wait 1 and other values, take 2) with the job done
or not, finished or not; the single result and the first of a result list; the holder count at
its 8-bit wrap.
"""
import sys

import job_fixtures as jf

ADDRESS = '0x024bd150'
DONE = 0x01


def lock_calls(lock_writes=(), unlock_writes=()):
    lock = {'address': jf.LOCK_FN, 'argc': 1, 'argu32': [1]}
    unlock = {'address': jf.UNLOCK_FN, 'argc': 1}
    if lock_writes:
        lock['writes'] = list(lock_writes)
    if unlock_writes:
        unlock['writes'] = list(unlock_writes)
    return [lock, unlock]


def case(cid, mode, job=None, waits=0, done_check=True, take=True, list_results=False, after_check=None,
         under_take_lock=None, after_take=None, swap_job=None):
    """job: job_bytes keyword arguments (None: the handle has no job). after_check, under_take_lock
    and after_take are (offset, bytes) stores into the job made by the done check's unlock, the
    take's lock and the take's unlock: another thread changing the job around the lock, which
    shows whether each read happens while the lock is held. swap_job (job_bytes keyword
    arguments) is a second job the done check's unlock puts in the handle: the original reads the
    handle's job again for the wait and for the take."""
    buffers = {'handle': {'size': 0x10}, 'lockvt': {'size': 0x30}, 'lockobj': {'size': 0x8}}
    memory = [{'addr': jf.JOB_LOCK, 'pointer': 'lockvt'},
              {'addr': 'buf:lockvt+0x18', 'guest': jf.LOCK_FN},
              {'addr': 'buf:lockvt+0x28', 'guest': jf.UNLOCK_FN}]
    stubs = []
    imports = []
    if job is None:
        memory.append({'addr': 'buf:handle+0x8', 'bytes': '00' * 8})
    else:
        if list_results:
            buffers['entry'] = {'size': 0x18, 'bytes': ('11' * 0x10) + 'efcdab8967452301'}
            buffers['list'] = {'size': 0x10}
            memory.append({'addr': 'buf:list', 'pointer': 'entry'})
            memory.append({'addr': 'buf:job+0x80', 'pointer': 'list'})
        buffers['job'] = {'size': jf.JOB_SIZE, 'bytes': jf.job_bytes(**job).hex()}
        memory.append({'addr': 'buf:handle+0x8', 'pointer': 'job'})

        if swap_job is not None:
            buffers['job2'] = {'size': jf.JOB_SIZE, 'bytes': jf.job_bytes(**swap_job).hex()}

        def store(change, buffer='job'):
            return [jf.through_lock(buffers, buffer, *change)] if change else []
        if done_check:
            swap = ()
            if swap_job is not None:
                job2 = jf.buffer_addresses(buffers)['job2']
                swap = store((0x8, job2.to_bytes(8, 'little')), 'handle')
            stubs += lock_calls(unlock_writes=store(after_check) + list(swap))
        imports = jf.sleeps(waits, buffers, 'job2' if swap_job is not None else 'job')
        if take:
            stubs += lock_calls(store(under_take_lock), store(after_take))
    # The job lock object is the global itself: its first qword points at the fake vtable.
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:handle', 'rsi': hex(mode)}, 'buffers': buffers, 'memory': memory,
            'stubs': stubs, 'imports': imports}


SINGLE = 0x1122334455667788
CASES = [
    case('no_job_poll', 0),
    case('no_job_wait', 1),
    case('no_job_take', 2),
    case('poll_not_done', 0, dict(state=0, single_result=SINGLE), take=False),
    case('poll_not_done_but_finished_state', 0, dict(state=2, single_result=SINGLE), take=False),
    case('poll_done', 0, dict(state=0, flags=DONE, single_result=SINGLE)),
    case('poll_done_other_flag_bits', 0, dict(flags=0xff, single_result=SINGLE)),
    case('poll_only_other_flag_bits', 0, dict(flags=0xfe, single_result=SINGLE), take=False),
    case('wait_done_no_sleep', 1, dict(state=0, flags=DONE, single_result=SINGLE)),
    case('wait_not_done_already_finished', 1, dict(state=2, single_result=SINGLE)),
    case('wait_not_done_one_sleep', 1, dict(state=1, single_result=SINGLE), waits=1),
    case('wait_not_done_three_sleeps', 1, dict(state=0, single_result=SINGLE), waits=3),
    case('mode_3_waits', 3, dict(state=1, single_result=SINGLE), waits=2),
    case('mode_negative_waits', -1 & 0xffffffff, dict(state=1, single_result=SINGLE), waits=1),
    case('mode_upper_bits_ignored', 0x500000002, dict(state=0, single_result=SINGLE), done_check=False),
    case('take_skips_checks', 2, dict(state=0, single_result=SINGLE), done_check=False),
    case('take_result_list', 2, dict(result_count=1), done_check=False, list_results=True),
    case('take_result_list_many', 2, dict(result_count=0xffffffffffffffff), done_check=False, list_results=True),
    case('take_null_single_result', 2, dict(single_result=0), done_check=False),
    case('take_holder_count_wraps', 2, dict(references=0xff, single_result=SINGLE), done_check=False),
    case('wait_result_list', 1, dict(state=0, result_count=2), waits=1, list_results=True),
    case('poll_done_after_check_unlock', 0, dict(state=0, single_result=SINGLE), take=False,
         after_check=(0x31, bytes([DONE]))),
    case('poll_undone_after_check_unlock', 0, dict(state=0, flags=DONE, single_result=SINGLE),
         after_check=(0x31, bytes([0]))),
    case('result_set_under_take_lock', 2, dict(single_result=SINGLE), done_check=False,
         under_take_lock=(0x18, (0x0badc0de).to_bytes(8, 'little'))),
    case('result_changed_after_take_unlock', 2, dict(single_result=SINGLE), done_check=False,
         after_take=(0x18, (0x0badc0de).to_bytes(8, 'little'))),
    case('result_count_changed_after_take_unlock', 2, dict(single_result=SINGLE, result_count=1),
         done_check=False, list_results=True, after_take=(0x88, bytes(8))),
    case('handle_job_swapped_before_take', 0, dict(flags=DONE, single_result=SINGLE),
         swap_job=dict(flags=DONE, single_result=0x0bad0bad)),
    case('handle_job_swapped_before_wait', 1, dict(state=0, single_result=SINGLE), waits=2,
         swap_job=dict(state=1, single_result=0x0bad0bad)),
]

if __name__ == '__main__':
    sys.exit(jf.write(CASES, __doc__))
