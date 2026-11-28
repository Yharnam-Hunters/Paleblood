#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the load object (game/engine/load.cpp), for tools/verify.py run.

usage: load_cases.py OUT_DIR construct|frame|run_job|destroy|delete

The load is an invented 0x58-byte buffer. Its job, the job's vtable, the callback's vtable (at
+0x28), the load's own vtable, a callback handle and an allocator are invented buffers too; the
vtable slots point at invented addresses that the cases stub. Every function the load calls is a
scripted stub, compared in order with its arguments (floats by their bits), so the order of the
calls and of the stores around them shows:

- construct (0x01d55cb0): kinds 0, 1, 0x4a, 0x4b, 0x4c, 0x80, 0x80000000 and 0xffffffff (only the
  low 32 bits of the register count: one case has garbage above them), registration answering a
  handle and null, a null job, and values with garbage above 32 bits. The base constructor stub
  overwrites the whole load and the registration stub overwrites the callback's fields, so a store
  moved across either call shows.
- frame (0x01d55c40): no job and a job, at states 0, 1, 2, 3, 4, 0x103, -1 and INT_MAX; in state 3
  counts 2 (kept), 1 (freed: owning allocator, the load's destroy slot, free) and 0 (wraps); the
  value at frame+0x8 as 1.0, -0.0, infinity, a NaN with a payload and a denormal; the update
  moving the state into and out of 3 (the state is read again after it); the unregister slot
  setting the count to 1 (the count is dropped after it).
- run_job (0x01d55380): no job and a job at the same states, the same values in xmm0.
- destroy (0x01d55de0) and delete (0x01d55d40): the handle set and null, the unregister slot
  clearing the handle and leaving it, no job and a job with counts 2, 1 and 0; the unregister slot
  moving the job pointer (read after it), and the handle release overwriting the vtables (stored
  around it). The unregister stub records the load's vtable pointer, which the destructors set
  before calling it. delete ends with operator delete and leaves the job pointer; destroy clears it.

Every case also scripts, after what the original calls, two more entries for every stubbed
address: the original never reaches them, so a replacement that calls too much fails on its call
sequence instead of running the real functions on invented memory.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'construct': '0x01d55cb0', 'frame': '0x01d55c40', 'run_job': '0x01d55380',
             'destroy': '0x01d55de0', 'delete': '0x01d55d40'}
BASE_CONSTRUCT = '0x013e5c10'
REGISTER = '0x02450490'
RELEASE_HANDLE = '0x02450560'
HEAP_OWNING = '0x0247b720'
OPERATOR_DELETE = '0x02fbe588'
# Invented vtable targets (any address in the image: the cases stub them).
JOB_DESTROY, JOB_UPDATE = '0x00500100', '0x00500180'
LOAD_DESTROY, HEAP_FREE, UNREGISTER = '0x00500200', '0x00500280', '0x00500300'

STATE, REFERENCES, JOB, CALLBACK = 0x0c, 0x10, 0x20, 0x28
HANDLE = CALLBACK + 0x10
LOAD_SIZE = 0x58
STATES = (0, 1, 2, 3, 4, 0x103, 0xffffffff, 0x7fffffff)
VALUES = {'one': '0000803f', 'minus_zero': '00000080', 'infinity': '0000807f', 'nan_payload': '4523c17f',
          'denormal': '01000000'}


def stub(address, argc, **extra):
    return {'address': address, 'argc': argc, **extra}


class Case:
    def __init__(self, variant, cid, state=1, references=2):
        load = bytearray.fromhex('3c' * LOAD_SIZE)
        struct.pack_into('<II', load, STATE, state, references)
        self.variant = variant
        self.c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void',
                  'args': {'rdi': 'buf:load'}, 'imports': [], 'stubs': [],
                  'buffers': {'load': {'size': LOAD_SIZE, 'bytes': load.hex()},
                              'load_vtable': {'size': 0x20, 'bytes': 'c1' * 0x20},
                              'callback_vtable': {'size': 0x38, 'bytes': 'c2' * 0x38},
                              'job_vtable': {'size': 0x20, 'bytes': 'c3' * 0x20},
                              'heap': {'size': 8}, 'heap_vtable': {'size': 0x78}},
                  'memory': [{'addr': 'buf:load+0', 'pointer': 'load_vtable'},
                             {'addr': f'buf:load+{CALLBACK:#x}', 'pointer': 'callback_vtable'},
                             {'addr': 'buf:load_vtable+0x8', 'guest': LOAD_DESTROY},
                             {'addr': 'buf:callback_vtable+0x30', 'guest': UNREGISTER},
                             {'addr': 'buf:job_vtable+0x8', 'guest': JOB_DESTROY},
                             {'addr': 'buf:job_vtable+0x18', 'guest': JOB_UPDATE},
                             {'addr': 'buf:heap+0', 'pointer': 'heap_vtable'},
                             {'addr': 'buf:heap_vtable+0x70', 'guest': HEAP_FREE}]}

    @classmethod
    def of(cls, c):
        case = cls.__new__(cls)
        case.c = c
        return case

    def buffer(self, name, size, fill, references=None):
        data = bytearray.fromhex(fill * size)
        if references is not None:
            struct.pack_into('<I', data, 8, references)
        self.c['buffers'][name] = {'size': size, 'bytes': data.hex()}

    def job(self, references=2, name='job', at=f'buf:load+{JOB:#x}'):
        """A job (vtable, count at +0x8) stored at `at` (the load's job pointer by default)."""
        if name not in self.c['buffers']:
            self.buffer(name, 0x10, '4a' if name == 'job' else '4b', references)
            self.c['memory'].append({'addr': f'buf:{name}+0', 'pointer': 'job_vtable'})
        if at:
            self.c['memory'].append({'addr': at, 'pointer': name})

    def no_job(self):
        self.c['memory'].append({'addr': f'buf:load+{JOB:#x}', 'bytes': '00' * 8})

    def handle(self, present=True):
        if present:
            self.buffer('handle', 0x10, '68')
            self.c['memory'].append({'addr': f'buf:load+{HANDLE:#x}', 'pointer': 'handle'})
        else:
            self.c['memory'].append({'addr': f'buf:load+{HANDLE:#x}', 'bytes': '00' * 8})

    def frame(self, value):
        data = bytearray.fromhex('f0' * 0x10)
        data[8:12] = bytes.fromhex(value)
        self.c['buffers']['frame'] = {'size': 0x10, 'bytes': data.hex()}
        self.c['args']['rsi'] = 'buf:frame'

    def add(self, *entries):
        self.c['stubs'] += list(entries)

    def update(self, writes=None):
        extra = {'writes': writes} if writes else {}
        self.add(stub(JOB_UPDATE, 2, argi=[0, 1], argf32=[0], **extra))

    def unregister(self, writes=None, sees_vtable=False):
        extra = {'writes': writes} if writes else {}
        if sees_vtable:
            # What the load's vtable pointer holds when the slot runs (the destructors store their
            # own class's vtable before calling it).
            extra['argmem'] = [{'arg': 0, 'offset': -CALLBACK, 'size': 8}]
        self.add(stub(UNREGISTER, 1, argi=[0], **extra))

    def frees(self, destroy):
        """One release that reaches 0: owning allocator, the object's destroy slot, free."""
        self.add(stub(HEAP_OWNING, 1, argi=[0], ret='buf:heap'), stub(destroy, 1, argi=[0]),
                 stub(HEAP_FREE, 2, argi=[0, 1]))

    def observe(self):
        """Two more entries per stubbed address, after what the original consumes (in order per
        address, so the original never reaches them)."""
        for _ in range(2):
            self.add(stub(BASE_CONSTRUCT, 1, argi=[0]), stub(REGISTER, 2, argi=[0], argu32=[1]),
                     stub(RELEASE_HANDLE, 1, argi=[0]), stub(OPERATOR_DELETE, 1, argi=[0]),
                     stub(UNREGISTER, 1, argi=[0]), stub(JOB_UPDATE, 2, argi=[0, 1], argf32=[0]))
            self.frees(JOB_DESTROY)
            self.frees(LOAD_DESTROY)


def construct_cases():
    out = []

    def case(cid, kind='0x10', job=True, value='0x2a', handle='buf:handle', base_writes=True, register_writes=True):
        c = Case('construct', cid)
        c.buffer('handle', 0x10, '68')
        c.c['args'].update({'rsi': kind, 'rcx': value})
        if job:
            c.job()
            c.c['memory'].pop()   # the job is an argument, not stored in the load yet
            c.c['args']['rdx'] = 'buf:job'
        else:
            c.c['args']['rdx'] = '0x0'
        # The base constructor stub overwrites the whole load: fields stored before the call keep
        # the stub's bytes on both sides, fields stored after it do not.
        writes = [{'addr': 'buf:load+0', 'bytes': 'a5' * LOAD_SIZE}] if base_writes else []
        c.add(stub(BASE_CONSTRUCT, 1, argi=[0], writes=writes) if writes else stub(BASE_CONSTRUCT, 1, argi=[0]))
        k = int(kind, 0) & 0xffffffff
        if k <= 0x4b:
            # The registration stub overwrites the handle, target, function and adjustment: the last
            # three are stored before it, the handle after it (its answer).
            extra = {'writes': [{'addr': f'buf:load+{HANDLE:#x}', 'bytes': 'b6' * 0x20}]} if register_writes else {}
            c.add(stub(REGISTER, 2, argi=[0], argu32=[1], ret=handle, **extra))
        out.append(c.c)

    for kind in ('0x0', '0x1', '0x4a', '0x4b', '0x4c', '0x80', '0x80000000', '0xffffffff'):
        case(f'kind_{int(kind, 0):x}', kind=kind)
    case('kind_4b_high_bits', kind='0xffffffff0000004b')
    case('kind_4c_high_bits', kind='0x000000010000004c')
    case('registration_answers_null', handle='0x0')
    case('no_job', job=False)
    case('no_job_unregistered', job=False, kind='0x4c')
    case('value_high_bits', value='0xdeadbeef00000009')
    case('value_all_ones', value='0xffffffffffffffff')
    case('plain_stubs', base_writes=False, register_writes=False)
    return out


def frame_cases(variant):
    out = []
    frame = variant == 'frame'

    def case(cid, state, references=2, job=True, value='0000803f', update_writes=None, unregister_writes=None):
        c = Case(variant, cid, state=state, references=references)
        if frame:
            c.frame(value)
        else:
            c.c['args']['xmm0'] = value
        c.job() if job else c.no_job()
        runs = state in (1, 2)
        if job and runs:
            c.update(update_writes)
        final = state
        for w in update_writes or []:
            if w['addr'] == f'buf:load+{STATE:#x}':
                final = struct.unpack('<I', bytes.fromhex(w['bytes']))[0]
        if frame and final == 3:
            count = references
            for w in unregister_writes or []:
                if w['addr'] == f'buf:load+{REFERENCES:#x}':
                    count = struct.unpack('<I', bytes.fromhex(w['bytes']))[0]
            c.unregister(unregister_writes)
            if count == 1:
                c.frees(LOAD_DESTROY)
        out.append(c.c)

    for state in STATES:
        case(f'state_{state:x}', state)
        case(f'no_job_state_{state:x}', state, job=False)
    if frame:
        for references in (1, 0):
            case(f'awaited_count_{references}', 3, references=references)
            case(f'no_job_awaited_count_{references}', 3, references=references, job=False)
    for name, value in VALUES.items():
        case(f'value_{name}_state_1', 1, value=value)
        case(f'value_{name}_state_2', 2, value=value)
    for start in (1, 2):
        case(f'update_moves_{start}_to_3', start, update_writes=[{'addr': f'buf:load+{STATE:#x}', 'bytes': '03000000'}])
        case(f'update_moves_{start}_to_3_count_1', start, references=1,
             update_writes=[{'addr': f'buf:load+{STATE:#x}', 'bytes': '03000000'}])
    case('update_moves_2_to_0', 2, update_writes=[{'addr': f'buf:load+{STATE:#x}', 'bytes': '00000000'}])
    if frame:
        case('unregister_sets_count_1', 3, references=2,
             unregister_writes=[{'addr': f'buf:load+{REFERENCES:#x}', 'bytes': '01000000'}])
        case('unregister_sets_count_2', 3, references=1,
             unregister_writes=[{'addr': f'buf:load+{REFERENCES:#x}', 'bytes': '02000000'}])
    return out


def destructor_cases(variant):
    out = []

    def case(cid, handle=True, job_references=2, job=True, unregister_writes=None, release_writes=None):
        c = Case(variant, cid)
        c.handle(handle)
        c.job(job_references) if job else c.no_job()
        c.unregister(unregister_writes, sees_vtable=True)
        still = handle and not any(w['addr'] == f'buf:load+{HANDLE:#x}' for w in unregister_writes or [])
        if any(w['addr'] == f'buf:load+{HANDLE:#x}' and 'pointer' in w for w in unregister_writes or []):
            still = True
        if still:
            extra = {'writes': release_writes} if release_writes else {}
            c.add(stub(RELEASE_HANDLE, 1, argi=[0], **extra))
        job_now = job
        count = job_references
        for w in unregister_writes or []:
            if w['addr'] == f'buf:load+{JOB:#x}':
                job_now = 'pointer' in w
                count = 1
        if job_now and count == 1:
            c.frees(JOB_DESTROY)
        if variant == 'delete':
            c.add(stub(OPERATOR_DELETE, 1, argi=[0]))
        out.append(c.c)

    for handle in (True, False):
        for job_references, job in ((2, True), (1, True), (0, True), (None, False)):
            name = f'job_count_{job_references}' if job else 'no_job'
            # A None check, not `or`: a count of 0 must reach the job (it wraps, nothing is freed).
            references = 2 if job_references is None else job_references
            case(f'{"handle" if handle else "no_handle"}_{name}', handle, references, job)
    case('unregister_clears_handle', unregister_writes=[{'addr': f'buf:load+{HANDLE:#x}', 'bytes': '00' * 8}])
    case('unregister_clears_handle_job_count_1', job_references=1,
         unregister_writes=[{'addr': f'buf:load+{HANDLE:#x}', 'bytes': '00' * 8}])
    case('unregister_sets_handle', handle=False,
         unregister_writes=[{'addr': f'buf:load+{HANDLE:#x}', 'pointer': 'load_vtable'}])
    # The unregister slot moves the job pointer to a second job with count 1: the job is read after it.
    c_job = {'addr': f'buf:load+{JOB:#x}', 'pointer': 'job2'}
    case('unregister_moves_job', unregister_writes=[c_job])
    out[-1]['buffers']['job2'] = {'size': 0x10, 'bytes': ('4b' * 8) + '01000000' + '4b' * 4}
    out[-1]['memory'].append({'addr': 'buf:job2+0', 'pointer': 'job_vtable'})
    case('unregister_clears_job', job_references=1,
         unregister_writes=[{'addr': f'buf:load+{JOB:#x}', 'bytes': '00' * 8}])
    # The handle release overwrites both vtable pointers: the callback's base vtable is stored
    # before it, the load's base vtable after it.
    case('release_overwrites_vtables', release_writes=[{'addr': 'buf:load+0', 'bytes': 'd7' * 8},
                                                       {'addr': f'buf:load+{CALLBACK:#x}', 'bytes': 'd8' * 8}])
    return out


GENERATORS = {'construct': construct_cases, 'frame': lambda: frame_cases('frame'),
              'run_job': lambda: frame_cases('run_job'), 'destroy': lambda: destructor_cases('destroy'),
              'delete': lambda: destructor_cases('delete')}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in GENERATORS:
        print(__doc__, file=sys.stderr)
        return 2
    out = GENERATORS[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        Case.of(c).observe()
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
