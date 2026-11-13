#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the load results' helpers (game/loading/result_helpers.cpp), for tools/verify.py run.

usage: result_helpers_cases.py OUT_DIR MODE

- construct (0x024bcfa0): an entry filled with a pattern (also at an odd address), four request
  words, a code (zero, negative, the extremes) and a word.
- copy (0x024bcfe0): a source and an entry with different patterns; the entry copied onto itself
  (its words end up cleared) and a source that starts 8 bytes into the entry.
- entry_assign (0x02aa4b90): the result base's assignment (0x024bd080) runs as it is or is a
  stub; the handle's assignment (0x024bd310) is a stub; an entry assigned to itself.
- handle_assign (0x024bd310): handles with the same info, null on either side or both, and two
  different infos; the info's release (0x024bd930) and its add reference (0x02a3a300) are stubs.
  In one case the source handle sits inside the old info and the release changes it, so the
  source's info must be read after the release; a handle assigned to itself.
- owned_release (0x02479710): an owned object with no parts, either part or both. Each part's
  vtable slots (destroy at +0, +0xb0), the allocator lookup (0x0247b720, a different allocator
  for each part) and the allocators' free (slot +0x70) are stubs. In two cases the owned object
  sits inside its part and the +0xb0 call clears its slot or replaces the part with another one.
"""
import json
import os
import struct
import sys

ADDRESSES = {'construct': '0x024bcfa0', 'copy': '0x024bcfe0', 'entry_assign': '0x02aa4b90',
             'handle_assign': '0x024bd310', 'owned_release': '0x02479710'}
RESULT_ASSIGN, HANDLE_ASSIGN = '0x024bd080', '0x024bd310'
INFO_RELEASE, INFO_ADD_REFERENCE, ALLOCATOR_OF = '0x024bd930', '0x02a3a300', '0x0247b720'
DESTROY, BEFORE_DESTROY, FREE = '0x00500200', '0x00500240', '0x00500280'
ENTRY_SIZE, HANDLE_OFFSET = 0x48, 0x38
MASK = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


class Case:
    def __init__(self, mode, cid, returns='void'):
        self.mode, self.cid, self.returns = mode, cid, returns
        self.raw, self.memory, self.stubs, self.args = {}, [], [], {}

    def stub(self, address, argc, **fields):
        self.stubs.append({'address': address, 'argc': argc, **fields})

    def json(self):
        return {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': self.returns,
                'args': self.args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
                'memory': self.memory, 'imports': [], 'stubs': self.stubs}


def pattern(seed, size):
    return bytearray((seed + i * 7) & 0xff for i in range(size))


def words(seed):
    return bytearray(b''.join(struct.pack('<Q', (seed * 0x0101010101010101 + i) & MASK) for i in range(4)))


def construct(cid, code=1, word=0x2020, seed=0x11, skew=0):
    c = Case('construct', cid)
    c.raw.update({'entry': pattern(0xa5, ENTRY_SIZE + 8), 'words': words(seed)})
    c.args.update({'rdi': f'buf:entry+{skew}', 'rsi': 'buf:words', 'rdx': hex(code & 0xffffffff),
                   'rcx': hex(word & MASK)})
    return c


def copy(cid, source_seed=0x5a, code=-0xf8, word=0x3030, onto='source'):
    c = Case('copy', cid)
    source = pattern(source_seed, ENTRY_SIZE)
    source[0x28:0x2c] = struct.pack('<i', code)
    source[0x30:0x38] = struct.pack('<Q', word & MASK)
    c.raw.update({'entry': pattern(0xa5, ENTRY_SIZE + 0x10), 'source': source})
    c.args.update({'rdi': 'buf:entry', 'rsi': {'source': 'buf:source', 'self': 'buf:entry',
                                               'inside': 'buf:entry+8'}[onto]})
    return c


def entry_assign(cid, stub_base=True, onto='source'):
    c = Case('entry_assign', cid, returns='i64')
    c.raw.update({'entry': pattern(0xa5, ENTRY_SIZE), 'source': pattern(0x5a, ENTRY_SIZE)})
    c.args.update({'rdi': 'buf:entry', 'rsi': 'buf:entry' if onto == 'self' else 'buf:source'})
    if stub_base:
        c.stub(RESULT_ASSIGN, 2, ret='buf:entry')
    c.stub(HANDLE_ASSIGN, 2, ret=f'buf:entry+{HANDLE_OFFSET}')
    return c


def handle_assign(cid, old=None, new=None, reread=False, onto='source'):
    """old/new: None, 'a' or 'b' (the info each handle points to)."""
    c = Case('handle_assign', cid, returns='i64')
    c.raw.update({'handle': pattern(0x31, 0x10), 'source': pattern(0x41, 0x10), 'a': bytearray(0x40),
                  'b': bytearray(0x40), 'c': bytearray(0x40)})
    source = 'buf:source'
    if reread:
        # The source handle sits at +0x20 in the old info, whose release points it to info c.
        source = 'buf:a+32'
        c.memory.append({'addr': 'buf:a+40', 'pointer': f'{new}+0'})
    c.memory.append({'addr': 'buf:handle+8', 'pointer': f'{old}+0'} if old else {'addr': 'buf:handle+8',
                                                                               'bytes': u64(0)})
    if not reread:
        c.memory.append({'addr': 'buf:source+8', 'pointer': f'{new}+0'} if new else {'addr': 'buf:source+8',
                                                                                    'bytes': u64(0)})
    c.args.update({'rdi': 'buf:handle', 'rsi': 'buf:handle' if onto == 'self' else source})
    if old and old != new and onto != 'self':
        writes = [{'arg': 0, 'offset': 40, 'pointer': 'c+0'}] if reread else []
        c.stub(INFO_RELEASE, 1, writes=writes)
    final = 'c' if reread else new
    if final and old != new and onto != 'self':
        c.stub(INFO_ADD_REFERENCE, 1)
    return c


class Owned:
    def __init__(self, cid, inside=None):
        self.c = Case('owned_release', cid)
        self.c.raw.update({'owner': pattern(0x77, 0x18), 'vtable': bytearray(0xb8), 'alloc_a': bytearray(0x10),
                           'alloc_b': bytearray(0x10), 'alloc_vtable': bytearray(0x78)})
        self.c.memory += [{'addr': 'buf:vtable+0', 'guest': DESTROY}, {'addr': 'buf:vtable+176', 'guest': BEFORE_DESTROY},
                          {'addr': 'buf:alloc_a+0', 'pointer': 'alloc_vtable+0'},
                          {'addr': 'buf:alloc_b+0', 'pointer': 'alloc_vtable+0'},
                          {'addr': 'buf:alloc_vtable+112', 'guest': FREE}]
        self.owner = 'buf:owner'
        self.allocators = ['alloc_a', 'alloc_b']
        if inside:
            # The owned object sits at +0x40 in its part, so the part's +0xb0 call can rewrite it.
            self.part(inside, 0x60)
            self.owner = f'buf:{inside}+64'
            self.c.memory.append({'addr': f'buf:{inside}+64', 'bytes': pattern(0x77, 0x18).hex()})
        self.c.args['rdi'] = self.owner

    def part(self, name, size=0x10):
        if name not in self.c.raw:
            self.c.raw[name] = pattern(0x90, size)
            self.c.memory.append({'addr': f'buf:{name}+0', 'pointer': 'vtable+0'})

    def slot(self, offset, name):
        """Puts a part (or null) in the owned object's slot at offset."""
        if name:
            self.part(name)
        address = f'{self.owner}+{offset}' if self.owner == 'buf:owner' else \
            f"buf:{self.owner.split(':')[1].split('+')[0]}+{0x40 + offset}"
        self.c.memory.append({'addr': address, 'pointer': f'{name}+0'} if name else {'addr': address, 'bytes': u64(0)})

    def release(self, before_writes=None, freed=True):
        self.c.stub(BEFORE_DESTROY, 1, writes=before_writes or [])
        if freed:
            self.c.stub(ALLOCATOR_OF, 1, ret=f'buf:{self.allocators.pop(0)}')
            self.c.stub(DESTROY, 1)
            self.c.stub(FREE, 2)


def owned(cid, part_0x08=None, part_0x10=None):
    o = Owned(cid)
    o.slot(8, part_0x08)
    o.slot(16, part_0x10)
    for name in (part_0x10, part_0x08):
        if name:
            o.release()
    return o.c


def owned_rewritten(cid, replace):
    o = Owned(cid, inside='p')
    o.slot(8, None)
    if replace:
        o.part('q')
        o.slot(16, 'p')
        o.release(before_writes=[{'arg': 0, 'offset': 0x50, 'pointer': 'q+0'}])
    else:
        o.slot(16, 'p')
        o.release(before_writes=[{'arg': 0, 'offset': 0x50, 'bytes': u64(0)}], freed=False)
    return o.c


def cases(mode):
    if mode == 'construct':
        return [construct('basic'), construct('negative_code', code=-0xf8), construct('code_zero_word_max', code=0,
                                                                                      word=MASK),
                construct('code_max', code=0x7fffffff, word=0, seed=0x7f), construct('code_min', code=-0x80000000),
                construct('odd_address', skew=3, seed=0x22)]
    if mode == 'copy':
        return [copy('basic'), copy('code_zero', code=0, word=0), copy('code_min_word_max', code=-0x80000000, word=MASK),
                copy('onto_itself', onto='self'), copy('source_inside', onto='inside')]
    if mode == 'entry_assign':
        return [entry_assign('stubbed'), entry_assign('base_runs', stub_base=False),
                entry_assign('onto_itself', stub_base=False, onto='self')]
    if mode == 'handle_assign':
        return [handle_assign('same_info', 'a', 'a'), handle_assign('both_null'), handle_assign('null_to_info', None, 'a'),
                handle_assign('info_to_null', 'a', None), handle_assign('info_to_other', 'a', 'b'),
                handle_assign('source_read_after_release', 'a', 'b', reread=True),
                handle_assign('onto_itself', 'a', 'a', onto='self'),
                handle_assign('onto_itself_null', onto='self')]
    return [owned('empty'), owned('only_0x10', part_0x10='p'), owned('only_0x08', part_0x08='p'),
            owned('both', part_0x08='p', part_0x10='q'), owned_rewritten('cleared_by_before_destroy', False),
            owned_rewritten('replaced_by_before_destroy', True)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = cases(sys.argv[2])
    for c in generated:
        with open(os.path.join(out, f'{c.cid}.json'), 'w') as f:
            json.dump(c.json(), f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
