#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the language object's load creation steps (game/menu/localize_create_steps.cpp), for tools/verify.py run.

usage: localize_create_steps_cases.py OUT_DIR create_load|create_held_load|next_load

The language object is an invented 0x70-byte buffer. The resource heap (the global at 0x05940408)
and the container heap (0x05940418) are invented allocators whose vtable slots are scripted
stubs: allocate (slot 0x58) returns an invented buffer or null, info (slot 0x20) fills in the
flags. The load constructor (0x01d55cb0), the big job's constructor (0x01d59fb0), the fatal error
report (0x024b55b0) and, for the release in next_load, the owning allocator lookup (0x0247b720),
the load's destroy (vtable slot 0x8) and the heap's free (slot 0x70) are scripted stubs too,
compared in order with their arguments, so the sizes, alignments, kinds, jobs and order show.
The constructor stub writes state 1 and count 1 into the load, as the real one does, so the count
the step adds to it after the call shows. The byte at 0x0593e89d starts nonzero, so clearing it
shows. Every case also scripts, after what the original calls, one more allocation (of a spare
buffer), constructor, job constructor, info, fatal report and freeing release: the original never
reaches them, so a replacement that calls too much fails on its call sequence instead of faulting.

- create_load (0x01d52a20): both allocations working with the object's state at 0, 1, 4, -1 and
  INT_MAX (wraps); the load allocation failing (null stored, no job allocated); the job allocation
  failing (the load built with a null job); the load allocation changing the heap global (it is
  read again for the job); the constructor writing the object's +0x60 and state
  (stored and added to after it).
- create_held_load (0x01d52f00): the same states; the job allocation failing; the info flags
  0x20 and 0xff (valid) and 0x00, 0xdf and 0x1f (invalid: reported, then built anyway); the info
  call writing the job's fields on both sides of it (+0x28..+0x38 are stored before it, the
  container after it) and changing the container heap global (the heap read before the call is the
  one stored); the constructor writing counts 1, 0 and 0xffffffff (the add wraps) and the object's
  +0x50 and state.
- next_load (0x01d53050): the load at +0x50 at state 3 with counts 1 (freed), 2, 0 and 0x80000000
  (kept); at states 0, 2, 4, 0x103, 0x80000003, -1 and INT_MAX (nothing happens); SprjScaleform
  present and missing (reported, the state still added to); the job allocation failing; the
  object's state at several values; the load's destroy writing the object's +0x50, +0x48 and state.
A failed load allocation in create_held_load and next_load adds to a count through the null
pointer and faults on both sides; verify.py cannot compare a fault, so those are not cases.
Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'create_load': '0x01d52a20', 'create_held_load': '0x01d52f00', 'next_load': '0x01d53050'}
RESOURCE_HEAP, CONTAINER_HEAP = '0x05940408', '0x05940418'
PLAIN_JOB_FLAG = '0x0593e89d'
SCALEFORM = '0x05940368'
GUARD_GOT = '0x057e3be0'
LOAD_CONSTRUCT, JOB_CONSTRUCT = '0x01d55cb0', '0x01d59fb0'
FATAL = '0x024b55b0'
HEAP_OWNING = '0x0247b720'
# Fake vtable targets (any address: the case stubs them).
ALLOCATE, INFO, DESTROY, HEAP_FREE = '0x00500100', '0x00500180', '0x00500200', '0x00500280'
STATE, REFERENCES = 0x0c, 0x10
LANGUAGE_SIZE, LOAD_SIZE = 0x70, 0x58
JOBS = {'create_load': 0x38, 'create_held_load': 0x68, 'next_load': 0x1d0}
SLOTS = {'create_load': 0x60, 'create_held_load': 0x50, 'next_load': 0x48}
KINDS = {'create_load': 10, 'create_held_load': 10, 'next_load': 11}
AWAITED = 3
STATES = (0, 1, 4, 0xffffffff, 0x7fffffff)
OTHER_STATES = (0, 2, 4, 0x103, 0x80000003, 0xffffffff, 0x7fffffff)


def u32s(*v):
    return b''.join(struct.pack('<I', x & 0xffffffff) for x in v).hex()


class Case:
    def __init__(self, variant, cid, language_state=1):
        self.variant = variant
        language = bytearray.fromhex('3c' * LANGUAGE_SIZE)
        struct.pack_into('<I', language, STATE, language_state)
        self.c = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void',
                  'args': {'rdi': 'buf:language'}, 'imports': [], 'stubs': [],
                  'buffers': {'language': {'size': LANGUAGE_SIZE, 'bytes': language.hex()},
                              'heap': {'size': 8}, 'heap2': {'size': 8}, 'heap_vtable': {'size': 0x78},
                              'new_load': {'size': LOAD_SIZE, 'bytes': '4e' * LOAD_SIZE},
                              'job': {'size': JOBS[variant], 'bytes': '6a' * JOBS[variant]},
                              'spare': {'size': 0x200, 'bytes': '5e' * 0x200},
                              'guard': {'size': 0x10, 'bytes': '5a' * 0x10}},
                  'memory': [{'addr': RESOURCE_HEAP, 'pointer': 'heap+0x0'},
                             {'addr': 'buf:heap+0', 'pointer': 'heap_vtable+0x0'},
                             {'addr': 'buf:heap2+0', 'pointer': 'heap_vtable+0x0'},
                             {'addr': 'buf:heap_vtable+0x58', 'guest': ALLOCATE},
                             {'addr': 'buf:heap_vtable+0x70', 'guest': HEAP_FREE},
                             {'addr': PLAIN_JOB_FLAG, 'bytes': 'a5'},
                             {'addr': GUARD_GOT, 'pointer': 'guard+0x0'}]}
        if variant == 'create_held_load':
            self.c['buffers'].update({'container_heap': {'size': 8}, 'container_heap_vtable': {'size': 0x28},
                                      'other_heap': {'size': 8}})
            self.c['memory'] += [{'addr': CONTAINER_HEAP, 'pointer': 'container_heap+0x0'},
                                 {'addr': 'buf:container_heap+0', 'pointer': 'container_heap_vtable+0x0'},
                                 {'addr': 'buf:other_heap+0', 'pointer': 'container_heap_vtable+0x0'},
                                 {'addr': 'buf:container_heap_vtable+0x20', 'guest': INFO}]
        if variant == 'next_load':
            self.c['buffers'].update({'held': {'size': LOAD_SIZE, 'bytes': '48' * LOAD_SIZE},
                                      'load_vtable': {'size': 0x10, 'bytes': 'c1' * 0x10},
                                      'scaleform': {'size': 0x30, 'bytes': '53' * 0x30}})
            self.c['memory'] += [{'addr': 'buf:language+0x50', 'pointer': 'held+0x0'},
                                 {'addr': 'buf:held+0', 'pointer': 'load_vtable+0x0'},
                                 {'addr': 'buf:load_vtable+0x8', 'guest': DESTROY},
                                 {'addr': SCALEFORM, 'pointer': 'scaleform+0x0'}]
        self.held(AWAITED, 2)

    @classmethod
    def of(cls, c):
        """Wraps a finished case dict again (the generators return the dicts)."""
        case = cls.__new__(cls)
        case.c = c
        case.variant = next(v for v, a in ADDRESSES.items() if a == c['address'])
        return case

    def held(self, state, references):
        if self.variant != 'next_load':
            return
        data = bytearray.fromhex(self.c['buffers']['held']['bytes'])
        struct.pack_into('<II', data, STATE, state, references)
        self.c['buffers']['held']['bytes'] = data.hex()

    def stub(self, address, argc, argi=None, **kw):
        self.c['stubs'].append({'address': address, 'argc': argc, 'argi': list(range(argc)) if argi is None else argi,
                                **kw})

    def allocate(self, size, buffer, writes=()):
        extra = {'writes': list(writes)} if writes else {}
        self.stub(ALLOCATE, 3, ret=f'buf:{buffer}' if buffer else 0, **extra)

    def construct(self, job=True, references=1, writes=()):
        """The load constructor: writes state 1 and the count, as the real one does."""
        self.stub(LOAD_CONSTRUCT, 4, writes=[{'arg': 0, 'offset': STATE, 'bytes': u32s(1, references)}, *writes])

    def info(self, flags=0x20, writes=()):
        # The first argument is the caller's stack buffer, which differs between the two sides. Only
        # the flags byte is written: the original keeps its stack guard 8 bytes past that buffer.
        self.stub(INFO, 3, argi=[1, 2], writes=[{'arg': 0, 'bytes': f'{flags:02x}'}, *writes])

    def fatal(self, argc):
        self.stub(FATAL, argc)

    def frees(self, writes=()):
        """One release that reaches 0: owning allocator, destroy, free."""
        destroy = {'writes': list(writes)} if writes else {}
        self.stub(HEAP_OWNING, 1, ret='buf:heap')
        self.stub(DESTROY, 1, **destroy)
        self.stub(HEAP_FREE, 2)

    def create(self, job_ok=True, flags=0x20, info_writes=(), references=1, construct_writes=(), load_writes=()):
        """What a creation calls when the load allocation works."""
        self.allocate(LOAD_SIZE, 'new_load', load_writes)
        self.allocate(JOBS[self.variant], 'job' if job_ok else None)
        if job_ok and self.variant == 'create_held_load':
            self.info(flags, info_writes)
            if not flags & 0x20:
                self.fatal(3)
        if job_ok and self.variant == 'next_load':
            self.stub(JOB_CONSTRUCT, 1)
        self.construct(references=references, writes=construct_writes)

    def missing_scaleform(self):
        self.c['memory'] = [m for m in self.c['memory'] if m['addr'] != SCALEFORM]
        self.c['memory'].append({'addr': SCALEFORM, 'bytes': '00' * 8})

    def observe(self):
        """Scripts what a wrong replacement could call on top of what the original calls. Stubs are
        consumed in order per address and the extra entries come last, so the original never
        reaches them."""
        self.allocate(0x200, 'spare')
        self.allocate(0x200, 'spare')
        self.construct()
        self.stub(JOB_CONSTRUCT, 1)
        self.fatal(4)
        if self.variant == 'create_held_load':
            self.info()
        if self.variant == 'next_load':
            self.frees()


def common_cases(variant):
    out = []
    for state in STATES:
        c = Case(variant, f'language_state_{state:x}', language_state=state)
        c.create()
        out.append(c)
    c = Case(variant, 'load_allocation_changes_heap')
    c.create(load_writes=[{'addr': RESOURCE_HEAP, 'pointer': 'heap2+0x0'}])
    out.append(c)
    c = Case(variant, 'job_allocation_fails')
    c.create(job_ok=False)
    out.append(c)
    slot = SLOTS[variant]
    c = Case(variant, 'construct_writes_slot_and_state')
    c.create(construct_writes=[{'addr': f'buf:language+{slot:#x}', 'pointer': 'spare+0x0'},
                               {'addr': f'buf:language+{STATE:#x}', 'bytes': u32s(7)}])
    out.append(c)
    return out


def create_load_cases():
    out = common_cases('create_load')
    c = Case('create_load', 'load_allocation_fails')
    c.allocate(LOAD_SIZE, None)
    out.append(c)
    return [c.c for c in out]


def create_held_load_cases():
    out = common_cases('create_held_load')
    for flags in (0xff, 0x00, 0xdf, 0x1f):
        c = Case('create_held_load', f'info_flags_{flags:02x}')
        c.create(flags=flags)
        out.append(c)
    c = Case('create_held_load', 'info_writes_job_fields')
    c.create(info_writes=[{'addr': 'buf:job+0x28', 'bytes': '99' * 0x18},
                          {'addr': 'buf:job+0x48', 'bytes': '99' * 0x20}])
    out.append(c)
    c = Case('create_held_load', 'info_writes_job_fields_invalid')
    c.create(flags=0, info_writes=[{'addr': 'buf:job+0x0', 'bytes': '99' * 0x68}])
    out.append(c)
    c = Case('create_held_load', 'info_changes_container_heap')
    c.create(info_writes=[{'addr': CONTAINER_HEAP, 'pointer': 'other_heap+0x0'}])
    out.append(c)
    for references in (0, 0xffffffff):
        c = Case('create_held_load', f'construct_count_{references:x}')
        c.create(references=references)
        out.append(c)
    return [c.c for c in out]


def next_load_cases():
    out = common_cases('next_load')
    for references in (1, 0, 0x80000000):
        c = Case('next_load', f'held_count_{references:x}')
        c.held(AWAITED, references)
        if references == 1:
            c.frees()
        c.create()
        out.append(c)
    for state in OTHER_STATES:
        c = Case('next_load', f'held_state_{state:x}')
        c.held(state, 1)
        out.append(c)
    c = Case('next_load', 'scaleform_missing')
    c.missing_scaleform()
    c.create()
    c.fatal(4)
    out.append(c)
    c = Case('next_load', 'scaleform_missing_job_fails')
    c.missing_scaleform()
    c.create(job_ok=False)
    c.fatal(4)
    out.append(c)
    c = Case('next_load', 'destroy_writes_language')
    c.held(AWAITED, 1)
    c.frees(writes=[{'addr': 'buf:language+0x50', 'pointer': 'spare+0x0'},
                    {'addr': 'buf:language+0x48', 'pointer': 'spare+0x0'},
                    {'addr': f'buf:language+{STATE:#x}', 'bytes': u32s(0x40)}])
    c.create()
    out.append(c)
    c = Case('next_load', 'held_freed_construct_count_ffffffff')
    c.held(AWAITED, 1)
    c.frees()
    c.create(references=0xffffffff)
    out.append(c)
    return [c.c for c in out]


GENERATORS = {'create_load': create_load_cases, 'create_held_load': create_held_load_cases,
              'next_load': next_load_cases}


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
