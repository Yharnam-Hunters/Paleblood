#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the kernel memory pool's free (game/kernel/memory_pool_free.cpp), for
tools/verify.py run.

usage: memory_pool_free_cases.py OUT_DIR

Constructed pools, never recorded state. The lock calls, the backing heap's size query and free,
the page release and the pool's free callback (a stand-in address) are scripted stubs. Cases cover
the lazily created lock (success, failure, a result with only high bits), header pools (each
counter, the flag mask, a callback or the other releases), page pools (rounding at and around a
page, sizes and page sizes whose signed division differs from an unsigned one, counter wrap), the
repeat on the context's pool and the pool fields re-read after the release.
"""
import json
import os
import struct
import sys

ADDRESS = '0x011ab3a0'
CONTEXT_POINTER = '0x056d6ae8'
CREATE, LOCK, UNLOCK = '0x0111abb0', '0x0111acd0', '0x0111acf0'
SIZE_QUERY, BACKING_FREE, PAGE_RELEASE = '0x01201f30', '0x01200fc0', '0x011aba30'
CALLBACK = '0x00500200'
SECONDARY = 0x400000


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


class Case:
    def __init__(self, cid, mask=0xffffffff):
        self.c = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
                  'args': {'rdi': 'buf:pool0', 'rsi': 'buf:block+0x8', 'rdx': 0x04c7d10c, 'rcx': 0x234},
                  'buffers': {'context': {'size': 0x28, 'bytes': '3c' * 0x20 + u32(mask) + '3c' * 4},
                              'block': {'size': 0x20, 'bytes': '00' * 0x20}},
                  'memory': [{'addr': CONTEXT_POINTER, 'pointer': 'context'}], 'imports': [], 'stubs': []}
        self.pools = 0

    def block(self, first=0, second=0, header_size=0, header_flags=0):
        data = u32(header_size) + u32(header_flags) + u32(first) + u32(second) + 'e1' * 0x10
        self.c['buffers']['block']['bytes'] = data
        return self

    def pool(self, headers=0, page=0, callback=False, used=0x10000, secondary=0x20000, page_size=0x1000,
             lock=True, context_pool=False):
        """Add the next pool of the chain (pool0 is the argument; context_pool links it from the context)."""
        name = f'pool{self.pools}'
        self.pools += 1
        raw = bytearray(b'\x6b' * 0x60)
        raw[0x10] = page
        raw[0x1c:0x20] = struct.pack('<I', used)
        raw[0x28:0x2c] = struct.pack('<I', secondary)
        raw[0x30:0x38] = struct.pack('<Q', headers)
        raw[0x40:0x58] = bytes(0x18)
        raw[0x58:0x5c] = struct.pack('<i', page_size)
        self.c['buffers'][name] = {'size': 0x60, 'bytes': raw.hex()}
        self.c['buffers'][name + '_lock'] = {'size': 8, 'bytes': u64(0x1000 + self.pools)}
        self.c['buffers'][name + '_heap'] = {'size': 8, 'bytes': '77' * 8}
        mem = [{'addr': f'buf:{name}+0x48', 'pointer': name + '_heap'}]
        if lock:
            mem.append({'addr': f'buf:{name}+0x50', 'pointer': name + '_lock'})
        if callback:
            mem.append({'addr': f'buf:{name}+0x40', 'guest': CALLBACK})
        if context_pool:
            mem.append({'addr': 'buf:context+0x8', 'pointer': name})
        self.c['memory'] += mem
        return self

    def stub(self, address, argc, ret=0, writes=None):
        entry = {'address': address, 'argc': argc, 'ret': hex(ret & 0xffffffffffffffff)}
        if writes:
            entry['writes'] = writes
        self.c['stubs'].append(entry)
        return self

    def create(self, name, ret=0, write=True):
        return self.stub(CREATE, 2, ret, [{'arg': 0, 'offset': 0, 'pointer': name + '_lock'}] if write else None)

    def lock(self, ret=0):
        return self.stub(LOCK, 1, ret).stub(UNLOCK, 1, ret)


def header_case(cid, flags, size=0x40, mask=0xffffffff, callback=True, page=0, headers=1):
    c = Case(cid, mask).block(first=0x30, second=7, header_size=size, header_flags=flags)
    c.pool(headers=headers, page=page, callback=callback, context_pool=not page)
    if page:
        c.pool(context_pool=True)
    c.lock()
    if callback:
        c.stub(CALLBACK, 3)
    elif page:
        c.stub(PAGE_RELEASE, 4)
    else:
        c.stub(BACKING_FREE, 2)
    if page:
        c.lock().stub(SIZE_QUERY, 1, 0x30).stub(BACKING_FREE, 2)
    return c.c


def page_case(cid, size, page_size=0x1000, used=0x10000, second=3):
    c = Case(cid).block(first=size, second=second, header_size=0x99, header_flags=SECONDARY)
    c.pool(page=1, used=used, page_size=page_size).pool(context_pool=True)
    c.lock().stub(PAGE_RELEASE, 4).lock().stub(SIZE_QUERY, 1, 0x28).stub(BACKING_FREE, 2)
    return c.c


def plain_case(cid, size=0x30, used=0x10000, callback=False, mask=0xffffffff):
    c = Case(cid, mask).block(first=0x51, second=0x52, header_size=0x53, header_flags=0xffffffff)
    c.pool(used=used, callback=callback, context_pool=True)
    c.lock().stub(SIZE_QUERY, 1, size)
    c.stub(CALLBACK, 3) if callback else c.stub(BACKING_FREE, 2)
    return c.c


def special_cases():
    out = []
    # the lock is created on first use
    c = Case('lock_created').block().pool(lock=False, context_pool=True)
    c.create('pool0').lock().stub(SIZE_QUERY, 1, 0x20).stub(BACKING_FREE, 2)
    out.append(c.c)
    c = Case('lock_creation_fails').block().pool(lock=False, context_pool=True).create('pool0', ret=1, write=False)
    out.append(c.c)
    c = Case('lock_creation_fails_negative').block().pool(lock=False, context_pool=True)
    out.append(c.create('pool0', ret=0x8002000c, write=False).c)
    c = Case('lock_creation_result_high_bits_only').block().pool(lock=False, context_pool=True)
    c.create('pool0', ret=1 << 32).lock().stub(SIZE_QUERY, 1, 0x20).stub(BACKING_FREE, 2)
    out.append(c.c)
    # lock and unlock results are ignored
    c = Case('lock_results_ignored').block().pool(context_pool=True)
    c.lock(ret=0x21).stub(SIZE_QUERY, 1, 0x20).stub(BACKING_FREE, 2)
    out.append(c.c)
    # a page pool repeats on the context's pool, which may itself be a page pool; that one's page
    # release clears its flag, ending the repeats
    c = Case('page_chain_second_page_pool').block(first=0x2001, second=9)
    c.pool(page=1).pool(page=0x80, page_size=0x800, context_pool=True)
    c.lock().lock().stub(PAGE_RELEASE, 4).stub(PAGE_RELEASE, 4, writes=[{'arg': 0, 'offset': 0x10, 'bytes': '00'}])
    out.append(c.c)
    # the page release clears the page flag: no repeat
    c = Case('page_flag_cleared_during_release').block(first=0x1800, second=4).pool(page=1)
    c.lock().stub(PAGE_RELEASE, 4, writes=[{'arg': 0, 'offset': 0x10, 'bytes': '00'}])
    out.append(c.c)
    # the page release swaps the pool's lock: the unlock takes the new one
    c = Case('lock_reread_for_unlock').block(first=0x1800, second=4).pool(page=1)
    c.c['buffers']['spare_lock'] = {'size': 8, 'bytes': u64(0x5151)}
    c.lock().stub(PAGE_RELEASE, 4, writes=[{'arg': 0, 'offset': 0x50, 'pointer': 'spare_lock'},
                                           {'arg': 0, 'offset': 0x10, 'bytes': '00'}])
    out.append(c.c)
    # a page pool with a callback still repeats on the context's pool
    c = Case('page_pool_with_callback', mask=0xff).block(first=0x77, second=2, header_flags=0xffff)
    c.pool(page=1, callback=True).pool(context_pool=True)
    c.lock().stub(CALLBACK, 3).lock().stub(SIZE_QUERY, 1, 0x10).stub(BACKING_FREE, 2)
    out.append(c.c)
    # a block at a high address is passed through unchanged
    c = Case('callback_header_address').block(header_size=0x18, header_flags=0x8001)
    c.pool(headers=0x100000000, callback=True, context_pool=True).lock().stub(CALLBACK, 3)
    out.append(c.c)
    return out


CASES = [
    header_case('header_primary_callback', 0x1234),
    header_case('header_secondary_callback', SECONDARY | 5),
    header_case('header_all_flags_callback', 0xffffffff),
    header_case('header_flag_mask_partial', 0x00c0ff0f, mask=0x0000ff00),
    header_case('header_flag_mask_zero', 0x00400fff, mask=0),
    header_case('header_flag_bit_below_secondary', 0x200000),
    header_case('header_flag_bit_above_secondary', 0x800000),
    header_case('header_primary_backing', 0x10, callback=False),
    header_case('header_secondary_backing', SECONDARY, callback=False),
    header_case('header_high_pointer_bits_only', 0x1, headers=0x100000000_00),
    header_case('header_page_pool', 0x3, callback=False, page=1),
    header_case('header_page_pool_secondary', SECONDARY, callback=False, page=1),
    header_case('header_counter_wraps', 0x0, size=0x20001),
    header_case('header_size_zero', 0x0, size=0),
    plain_case('plain_backing'),
    plain_case('plain_backing_size_zero', size=0),
    plain_case('plain_counter_wraps', size=0x10001, used=0x10),
    plain_case('plain_size_full_word', size=0xffffffff),
    plain_case('plain_callback', callback=True),
    plain_case('plain_callback_mask_zero', callback=True, mask=0),
    page_case('page_one_page', 0x1000),
    page_case('page_one_byte', 0x1),
    page_case('page_just_over', 0x1001),
    page_case('page_just_under_two', 0x1fff),
    page_case('page_size_zero', 0x0),
    page_case('page_small_pages', 0x123, page_size=0x10),
    page_case('page_size_one', 0x1234, page_size=1),
    page_case('page_signed_sum', 0x90000000),
    page_case('page_sum_wraps', 0xfffffff0),
    page_case('page_negative_page_size', 0x40, page_size=-0x10),
    page_case('page_counter_wraps', 0x3000, used=0x100),
    page_case('page_first_page_high', 0x2000, second=0xfffffffe),
] + special_cases()


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
