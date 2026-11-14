#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine's fixed-size block pool (game/engine/block_pool.cpp), for
tools/verify.py run.

usage: block_pool_cases.py OUT_DIR allocate|free

Writes OUT_DIR/*.json for one function. The pool (filled with a pattern) has invented pages of
0x100 bytes of blocks (0x20 each unless a case says otherwise) followed by their 0x20-byte header;
case buffers start on a page boundary, so the pool's page mask (~0xff) takes a block back to its
page. Pages sit on the pool's partial and full rings as each case sets out. The pool's allocator
is a fake vtable whose allocate (+0x58) and free (+0x70) slots are scripted stubs, each with a
spare scripted call so an unexpected call is recorded. No game data is used.
- allocate (engine_block_pool_allocate_02a3b8b0): from the current page (blocks left, last block),
  from the front partial page, and from a new page (allocated, or the allocation failing).
- free (engine_block_pool_free_02a3b9b0): a null block; a block back on a page with others free;
  on a full page (to the back of the partial pages); the last block of a page (current, or on a
  ring), handed back to the allocator.
"""
import json
import os
import struct
import sys

ADDRESSES = {'allocate': '0x02a3b8b0', 'free': '0x02a3b9b0'}
RETURNS = {'allocate': 'i64', 'free': 'void'}
ALLOCATE, FREE = '0x00500100', '0x00500200'   # fake vtable targets (stubbed)
PAGE, HEADER, MASK = 0x100, 0x20, 0xffffffffffffff00
PARTIAL, FULL = 0x10, 0x20   # the rings' offsets in the pool


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


class Case:
    def __init__(self, mode, cid, block=0x20, per_page=8, page_count=3, fill=0x5a):
        self.mode, self.block = mode, block
        self.buffers = {'pool': {'size': 0x60, 'bytes': f'{fill:02x}' * 0x60}, 'allocator': {'size': 8},
                        'allocator_vtable': {'size': 0x78}}
        self.memory = [{'addr': 'buf:allocator', 'pointer': 'allocator_vtable'},
                       {'addr': 'buf:allocator_vtable+0x58', 'guest': ALLOCATE},
                       {'addr': 'buf:allocator_vtable+0x70', 'guest': FREE}]
        self.word('pool+0x08', page_count)
        self.word('pool+0x30', block)
        self.word('pool+0x40', PAGE)
        self.word('pool+0x48', MASK)
        self.word('pool+0x50', per_page)
        self.word('pool+0x58', 'allocator')
        self.word('pool+0x00', 0)
        self.stubs = []
        self.c = {'schema': 1, 'address': ADDRESSES[mode], 'id': cid, 'returns': RETURNS[mode],
                  'args': {'rdi': 'buf:pool'}}

    def word(self, at, value):
        if isinstance(value, str):
            self.memory.append({'addr': f'buf:{at}', 'pointer': value})
        else:
            self.memory.append({'addr': f'buf:{at}', 'bytes': q(value)})

    def page(self, name, free, fill=0xc6):
        """free: the offsets of the free blocks in chain order."""
        self.buffers[name] = {'size': PAGE + HEADER, 'bytes': f'{fill:02x}' * (PAGE + HEADER)}
        for a, b in zip(free, free[1:] + [None]):
            self.word(f'{name}+{a:#x}', f'{name}+{b:#x}' if b is not None else 0)
        self.word(f'{name}+{PAGE + 0x10:#x}', f'{name}+{free[0]:#x}' if free else 0)
        self.word(f'{name}+{PAGE + 0x18:#x}', len(free))

    def ring(self, head, pages):
        """head: the ring's offset in the pool; pages: the ring's pages in order."""
        nodes = [('pool', head)] + [(p, PAGE) for p in pages]
        for i, (b, o) in enumerate(nodes):
            nb, no = nodes[(i + 1) % len(nodes)]
            pb, po = nodes[i - 1]
            self.word(f'{b}+{o:#x}', f'{nb}+{no:#x}')
            self.word(f'{b}+{o + 8:#x}', f'{pb}+{po:#x}')

    def current(self, name):
        self.word('pool+0x00', f'{name}+{PAGE:#x}')

    def done(self):
        self.stubs += [{'address': ALLOCATE, 'argc': 3, 'ret': 0}, {'address': FREE, 'argc': 2}]
        self.c.update({'buffers': self.buffers, 'memory': self.memory, 'stubs': self.stubs, 'imports': []})
        return self.c


def allocate_cases():
    out = []

    def add(cid, partial=(), full=(), current=None, pages=None, new_page=None, **kw):
        c = Case('allocate', cid, **kw)
        for name, free in (pages or {}).items():
            c.page(name, free)
        c.ring(PARTIAL, list(partial))
        c.ring(FULL, list(full))
        if current:
            c.current(current)
        if new_page is not None:
            if new_page:
                c.buffers['new_page'] = {'size': PAGE + HEADER, 'bytes': '3c' * (PAGE + HEADER)}
            c.stubs.append({'address': ALLOCATE, 'argc': 3, 'ret': 'buf:new_page' if new_page else 0})
        out.append(c.done())

    add('current_blocks_left', current='a', pages={'a': [0x60, 0xa0, 0x20], 'b': [0x40]}, partial=['b'])
    add('current_last_block', current='a', pages={'a': [0x60], 'b': []}, full=['b'])
    add('current_last_block_full_empty', current='a', pages={'a': [0xe0]})
    add('partial_front_taken', pages={'a': [0x40, 0x80], 'b': [0x20]}, partial=['a', 'b'])
    add('partial_only_page', pages={'a': [0x80, 0x00]}, partial=['a'])
    add('partial_last_block', pages={'a': [0xc0], 'b': [], 'c': [0x20]}, partial=['a', 'c'], full=['b'])
    add('new_page', new_page=True)
    add('new_page_with_full_pages', pages={'b': []}, full=['b'], new_page=True)
    add('new_page_fails', new_page=False)
    add('new_page_large_blocks', new_page=True, block=0x40, per_page=4)
    add('new_page_count_from_pool', new_page=True, per_page=0x1_0000_0001, page_count=0xffffffffffffffff)
    add('zero_filled_pool', current='a', pages={'a': [0x60, 0x20]}, fill=0)
    add('all_bits_pool', new_page=True, fill=0xff)
    return out


def free_cases():
    out = []

    def add(cid, block, partial=(), full=(), current=None, pages=None, freed=False, **kw):
        c = Case('free', cid, **kw)
        for name, free in (pages or {}).items():
            c.page(name, free)
        c.ring(PARTIAL, list(partial))
        c.ring(FULL, list(full))
        if current:
            c.current(current)
        c.c['args']['rsi'] = f'buf:{block}' if block else '0x0'
        if freed:
            c.stubs.append({'address': FREE, 'argc': 2})
        out.append(c.done())

    add('null_block', None, pages={'a': [0x20]}, current='a')
    add('others_free', 'a+0x40', pages={'a': [0x20, 0x60]}, current='a')
    add('others_free_on_ring', 'a+0x40', pages={'a': [0x20], 'b': [0x80]}, partial=['b', 'a'])
    add('block_middle_address', 'a+0x47', pages={'a': [0x20]}, current='a')
    add('full_page', 'a+0x40', pages={'a': [], 'b': []}, full=['b', 'a'])
    add('full_page_to_back', 'a+0x40', pages={'a': [], 'b': [0x20]}, full=['a'], partial=['b'])
    add('full_page_only', 'a+0xe0', pages={'a': []}, full=['a'])
    add('full_page_one_block_pages', 'a+0x00', pages={'a': []}, full=['a'], per_page=1)
    add('last_block_current', 'a+0x00', pages={'a': [0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0]}, current='a',
        freed=True)
    add('last_block_on_ring', 'a+0x60', pages={'a': [0x20, 0x40, 0x00, 0x80, 0xa0, 0xc0, 0xe0], 'b': [0x20]},
        partial=['b', 'a'], current='b', freed=True)
    add('last_block_page_count_zero', 'a+0x00', pages={'a': [0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0]},
        current='a', freed=True, page_count=0)
    add('count_past_page', 'a+0x20', pages={'a': [0x40, 0x60]}, current='a', per_page=2)
    add('zero_filled_pool', 'a+0x40', pages={'a': [0x20]}, current='a', fill=0)
    add('all_bits_pool', 'a+0x40', pages={'a': [], 'b': []}, full=['a'], partial=['b'], fill=0xff)
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__.strip().splitlines()[3], file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    cases = allocate_cases() if sys.argv[2] == 'allocate' else free_cases()
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
