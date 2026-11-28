#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the localize job's constructor (game/menu/localize_job.cpp), for tools/verify.py run.

usage: localize_job_cases.py OUT_DIR

The job is an invented 0x1d0-byte buffer filled with a pattern, so every byte the constructor does
not write shows. The resource heap (the global at 0x05940408) is an invented allocator whose
allocate slot (0x58) is a scripted stub. The member constructor (0x02256c30), the idle check
(0x01ee9b40), the resource constructor (0x01d27cf0), the load constructor (0x01d55cb0), the
singleton call (0x023b54d0) and the fatal error report (0x024b55b0) are scripted stubs too,
compared in order with their arguments, so the sizes, alignments, kind and order show. The load
constructor stub writes state 1 and count 1 into the load, as the real one does. The engine object
(the global at 0x05956678) is an invented buffer whose byte at +0x1591 starts at 0x55, and the
singleton (0x059403d0) an invented buffer. Every case also scripts, after what the original calls,
one more of each call: the original never reaches them, so a replacement that calls too much fails
on its call sequence instead of faulting.

- idle check false (0, 0x100, 0xff00, which are 0 in the low byte): only the state at +0xc goes to 0;
  nothing else is allocated or called. Also with the check writing the job's load pointer and state.
- idle check true (1, 0xff, 0x101, 0x80): both allocations working; the load allocation failing (null
  stored, no resource allocated); the resource allocation failing (the load built with a null job);
  the load allocation changing the heap global (it is read again for the resource); the load pointer
  already set by the member constructor or by the idle check (no allocation); the member constructor
  writing fields the constructor writes after it (+0x1c0, +0x1c1, +0x1c8) and ones it writes before
  (+0x0, +0x88); the load constructor writing the job's state and load pointer (the state is set to
  0 after it, the pointer stored after it); the singleton missing (reported with its name, then the
  global is read again: valid after the report, still null after it is not a case: it faults); the
  singleton call and the report writing the job's state and the engine flag.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESS = '0x01d59fb0'
RESOURCE_HEAP = '0x05940408'
OWNER, SINGLETON = '0x05956678', '0x059403d0'
MEMBER, GATE, RESOURCE_CONSTRUCT = '0x02256c30', '0x01ee9b40', '0x01d27cf0'
LOAD_CONSTRUCT, APPLY, FATAL = '0x01d55cb0', '0x023b54d0', '0x024b55b0'
ALLOCATE = '0x00500100'   # fake vtable target (any address: the case stubs it)
JOB_SIZE, LOAD_SIZE, RESOURCE_SIZE = 0x1d0, 0x58, 0x13bc0
FLAG = 0x1591
STATE, LOAD = 0x0c, 0xa8


def u32s(*v):
    return b''.join(struct.pack('<I', x & 0xffffffff) for x in v).hex()


class Case:
    def __init__(self, cid, singleton=True):
        self.c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:job'},
                  'imports': [], 'stubs': [],
                  'buffers': {'job': {'size': JOB_SIZE, 'bytes': '6a' * JOB_SIZE},
                              'heap': {'size': 8}, 'heap2': {'size': 8}, 'heap_vtable': {'size': 0x78},
                              'new_load': {'size': LOAD_SIZE, 'bytes': '4e' * LOAD_SIZE},
                              'resource': {'size': 0x40, 'bytes': '7e' * 0x40},
                              'spare': {'size': 0x200, 'bytes': '5e' * 0x200},
                              'owner': {'size': 0x1600, 'bytes': '55' * 0x1600},
                              'singleton': {'size': 0x30, 'bytes': '53' * 0x30},
                              'singleton2': {'size': 0x30, 'bytes': '54' * 0x30}},
                  'memory': [{'addr': RESOURCE_HEAP, 'pointer': 'heap+0x0'},
                             {'addr': 'buf:heap+0', 'pointer': 'heap_vtable+0x0'},
                             {'addr': 'buf:heap2+0', 'pointer': 'heap_vtable+0x0'},
                             {'addr': 'buf:heap_vtable+0x58', 'guest': ALLOCATE},
                             {'addr': OWNER, 'pointer': 'owner+0x0'},
                             {'addr': SINGLETON, 'pointer': 'singleton+0x0'} if singleton
                             else {'addr': SINGLETON, 'bytes': '00' * 8}]}

    def stub(self, address, argc, argi=None, **kw):
        self.c['stubs'].append({'address': address, 'argc': argc, 'argi': list(range(argc)) if argi is None else argi,
                                **kw})

    def member(self, writes=()):
        self.stub(MEMBER, 1, **({'writes': list(writes)} if writes else {}))

    def gate(self, ret, writes=()):
        self.stub(GATE, 0, ret=ret, **({'writes': list(writes)} if writes else {}))

    def allocate(self, buffer, writes=()):
        self.stub(ALLOCATE, 3, ret=f'buf:{buffer}' if buffer else 0, **({'writes': list(writes)} if writes else {}))

    def resource(self):
        self.stub(RESOURCE_CONSTRUCT, 1)

    def construct(self, writes=()):
        """The load constructor: writes state 1 and the count, as the real one does."""
        self.stub(LOAD_CONSTRUCT, 4, writes=[{'arg': 0, 'offset': STATE, 'bytes': u32s(1, 1)}, *writes])

    def apply(self, writes=()):
        self.stub(APPLY, 1, **({'writes': list(writes)} if writes else {}))

    def fatal(self, writes=()):
        self.stub(FATAL, 4, **({'writes': list(writes)} if writes else {}))

    def create(self, load_ok=True, resource_ok=True, load_writes=(), construct_writes=()):
        """What the load creation calls: both allocations, the resource constructor, the load constructor."""
        self.allocate('new_load' if load_ok else None, load_writes)
        if load_ok:
            self.allocate('resource' if resource_ok else None)
            if resource_ok:
                self.resource()
            self.construct(construct_writes)

    def observe(self):
        """One more of every call after what the original makes (never reached by it)."""
        self.member()
        self.gate(1)
        self.allocate('spare')
        self.allocate('spare')
        self.resource()
        self.construct()
        self.apply()
        self.fatal()


def job_writes(offset, data):
    return {'addr': f'buf:job+{offset:#x}', 'bytes': data}


def cases():
    out = []
    for ret in (0, 0x100, 0xff00):
        c = Case(f'idle_false_{ret:x}')
        c.member()
        c.gate(ret)
        out.append(c)
    c = Case('idle_false_gate_writes_load_and_state')
    c.member()
    c.gate(0, writes=[{'addr': f'buf:job+{LOAD:#x}', 'pointer': 'spare+0x0'}, job_writes(STATE, u32s(7))])
    out.append(c)
    c = Case('idle_false_member_writes_load')
    c.member(writes=[{'addr': f'buf:job+{LOAD:#x}', 'pointer': 'spare+0x0'}])
    c.gate(0)
    out.append(c)
    for ret in (1, 0xff, 0x101, 0x80):
        c = Case(f'idle_true_{ret:x}')
        c.member()
        c.gate(ret)
        c.create()
        c.apply()
        out.append(c)
    c = Case('load_allocation_fails')
    c.member()
    c.gate(1)
    c.create(load_ok=False)
    c.apply()
    out.append(c)
    c = Case('resource_allocation_fails')
    c.member()
    c.gate(1)
    c.create(resource_ok=False)
    c.apply()
    out.append(c)
    c = Case('load_allocation_changes_heap')
    c.member()
    c.gate(1)
    c.create(load_writes=[{'addr': RESOURCE_HEAP, 'pointer': 'heap2+0x0'}])
    c.apply()
    out.append(c)
    for who in ('member', 'gate'):
        c = Case(f'{who}_sets_load_no_allocation')
        w = [{'addr': f'buf:job+{LOAD:#x}', 'pointer': 'spare+0x0'}]
        if who == 'member':
            c.member(writes=w)
            c.gate(1)
        else:
            c.member()
            c.gate(1, writes=w)
        c.apply()
        out.append(c)
    c = Case('member_writes_fields_around_the_constructor')
    c.member(writes=[job_writes(0x0, '99' * 8), job_writes(0x88, '99' * 8), job_writes(0x1c0, '99' * 16),
                     job_writes(0xb0, '98' * 0x10)])
    c.gate(1)
    c.create()
    c.apply()
    out.append(c)
    c = Case('gate_writes_state')
    c.member()
    c.gate(1, writes=[job_writes(STATE, u32s(7))])
    c.create()
    c.apply()
    out.append(c)
    c = Case('construct_writes_state_and_load')
    c.member()
    c.gate(1)
    c.create(construct_writes=[job_writes(STATE, u32s(7)), {'addr': f'buf:job+{LOAD:#x}', 'pointer': 'spare+0x0'}])
    c.apply()
    out.append(c)
    c = Case('apply_writes_state_and_flag')
    c.member()
    c.gate(1)
    c.create()
    c.apply(writes=[job_writes(STATE, u32s(5)), {'addr': f'buf:owner+{FLAG:#x}', 'bytes': '09'}])
    out.append(c)
    c = Case('singleton_missing_reread_valid', singleton=False)
    c.member()
    c.gate(1)
    c.create()
    c.fatal(writes=[{'addr': SINGLETON, 'pointer': 'singleton2+0x0'}])
    c.apply()
    out.append(c)
    c = Case('singleton_missing_report_writes_state_and_flag', singleton=False)
    c.member()
    c.gate(1)
    c.create()
    c.fatal(writes=[{'addr': SINGLETON, 'pointer': 'singleton2+0x0'}, job_writes(STATE, u32s(5)),
                    {'addr': f'buf:owner+{FLAG:#x}', 'bytes': '09'}])
    c.apply()
    out.append(c)
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        c.observe()
        with open(os.path.join(sys.argv[1], f"{c.c['id']}.json"), 'w') as f:
            json.dump(c.c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
