#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for frame_timing_job_wait (0x024bdd40); see job_fixtures.py.

usage: job_wait_cases.py OUT_DIR

An already finished job (no sleep), and jobs that finish after one, two and five sleeps from
each unfinished state (0, 1, 3, negative); the bytes around the state stay untouched.
"""
import sys

import job_fixtures as jf

ADDRESS = '0x024bdd40'


def case(cid, state, sleep_count):
    buffers = {'job': {'size': jf.JOB_SIZE, 'bytes': jf.job_bytes(state=state).hex()}}
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:job+0x0'},
            'buffers': buffers, 'memory': [], 'imports': jf.sleeps(sleep_count, buffers, 'job')}


CASES = [case('already_finished', 2, 0)]
for state in (0, 1, 3, -1, 0x7fffffff):
    for count in (1, 2, 5):
        CASES.append(case(f'state_{state & 0xffffffff:x}_finishes_after_{count}', state, count))

if __name__ == '__main__':
    sys.exit(jf.write(CASES, __doc__))
