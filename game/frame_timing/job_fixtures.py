# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared synthetic edge cases for the job waits in job_wait.cpp, written by job_wait_cases.py
(0x024bdd40) and job_take_result_cases.py (0x024bd150).

The kernel sleep is scripted, so nothing sleeps. A job that is not finished becomes finished
during a chosen sleep: that sleep's scripted call stores the finished state into the job, the
way the worker thread would while the caller sleeps. A scripted write is addressed through one
of the call's arguments, so it goes through the sleep's only argument, the constant 1000, plus
the distance from 1000 to the job's state in the case sandbox (tools/harness.py lays buffers out
from SANDBOX in name order, each followed by a guard page). The shared job lock is a fake object
whose lock and unlock slots point at two stand-in game addresses that the cases stub, so the
order of lock, read and unlock calls is compared. Generated fixtures, not inputs recorded in a
game run.
"""
import json
import os
import struct
import sys

SANDBOX = 0x900000000          # tools/harness.py: where case buffers start
PAGE = 4096
SLEEP_ARGUMENT = 1000          # the 1 ms poll interval
STATE_OFFSET = 0x10
FINISHED = 2
JOB_LOCK = '0x05aa4578'
LOCK_FN = '0x024bdd70'         # stand-ins for the lock's slots; stubbed in every case
UNLOCK_FN = '0x024bdd80'
JOB_SIZE = 0x90
IMAGE, EBOOT_BASE = 0x800000000, 0x400000   # tools/harness.py: host address of the image


def through_lock(buffers, buffer, offset, data):
    """A lock or unlock stub's write into a buffer, addressed from its argument (the lock)."""
    lock_host = IMAGE + int(JOB_LOCK, 16) - EBOOT_BASE
    target = buffer_addresses(buffers)[buffer] + offset
    return {'arg': 0, 'offset': target - lock_host, 'bytes': data.hex()}


def buffer_addresses(buffers):
    """Each buffer's sandbox address, as tools/harness.py lays them out."""
    out, offset = {}, 0
    for name in sorted(buffers):
        out[name] = SANDBOX + offset
        pages = max(1, (buffers[name]['size'] + PAGE - 1) // PAGE)
        offset += (pages + 1) * PAGE
    return out


def sleeps(count, buffers, job_buffer):
    """count scripted 1 ms sleeps; the last one finishes the job in job_buffer (none for 0)."""
    calls = [{'name': 'sceKernelUsleep', 'argc': 1, 'ret': 0} for _ in range(count)]
    if calls:
        target = buffer_addresses(buffers)[job_buffer] + STATE_OFFSET
        calls[-1]['writes'] = [{'arg': 0, 'offset': target - SLEEP_ARGUMENT,
                                'bytes': struct.pack('<i', FINISHED).hex()}]
    return calls


def job_bytes(state=0, flags=0, references=0, single_result=0, results=0, result_count=0):
    job = bytearray(b'\x6b' * JOB_SIZE)
    struct.pack_into('<i', job, STATE_OFFSET, state)
    struct.pack_into('<Q', job, 0x18, single_result)
    job[0x30] = references
    job[0x31] = flags
    struct.pack_into('<QQ', job, 0x80, results, result_count)
    return job


def write(items, usage):
    """Write the cases to the directory named on the command line."""
    if len(sys.argv) != 2:
        print(usage, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in items:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(items)} synthetic cases written to {sys.argv[1]}')
    return 0
