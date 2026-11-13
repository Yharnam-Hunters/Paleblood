#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the physics instance's id vector, controller and body set constructors
(game/physics/controller_body_set.cpp), for tools/verify.py run.

usage: controller_body_set_cases.py OUT_DIR ids|controller|body_set

Objects are named buffers filled with a pattern, so every byte a function leaves or writes is
compared. The original callees are stubbed (their arguments and the memory they see recorded).
- ids: a fake heap whose allocate and free slots are scripted stubs; empty (null) and allocated
  vectors, growing, the same capacity, shrinking below the size, capacity 0, a capacity with the
  upper 32 bits set, an unaligned buffer; a failed allocation (thread's transaction flag clear)
  over an empty vector and one with a single id, which goes on with the null buffer (with two or
  more ids both the game and the replacement fault writing the second to address 4).
- controller: the setup leaving the parameter array without capacity (flag bits only, or bit 30
  alone) so the reserve runs, with a buffer (size reset, no reserve), a reserve that leaves a size,
  the setup writing the two cleared words; patterned, NaN and zero parameter blocks.
- body_set: shape sets null, not counted (top half 0), counted (the count goes up), at 0xffff (wraps
  within 16 bits), 0xffff0001; index counts 0, negative, 1, 5, 16 and 37 (wide copies); a heap that
  rounds the size up, hands back a size not a multiple of 4 or a negative one; a buffer overlapping
  the source (the copy goes forward); upper register bits of the 32-bit arguments set.
"""
import json
import os
import struct
import sys

ADDRESSES = {'ids': '0x02bc1ca0', 'controller': '0x00ae3fc0', 'body_set': '0x00ae8700'}
GUARD_GOT = '0x057e3be0'
SHAPE_HEAP = '0x058018b0'
FATAL_ERROR = '0x024b55b0'
TRANSACTION_FLAG_OFFSET = 0x057e4318
THREAD_BLOCK = 0x40
FAKE_ALLOCATE, FAKE_FREE, FAKE_BUF_ALLOC = '0x00500200', '0x00500220', '0x00500240'
ALLOCATE_SLOT, FREE_SLOT, BUF_ALLOC_SLOT = 0x58, 0x70, 0x20
CONTROLLER_BASE, CONTROLLER_SETUP, RESERVE_MORE = '0x00aea9d0', '0x00ae40e0', '0x00819100'
BODY_SET_BASE = '0x00f359c0'
NAN_A, NAN_B = '0100c07f', '0200c0ff'
PATTERN = 'cd'
MASK = 0xffffffffffffffff


def h(fmt, *v):
    return struct.pack(fmt, *v).hex()


def f32s(*v):
    return b''.join(bytes.fromhex(x) if isinstance(x, str) else struct.pack('<f', x) for x in v).hex()


class Case:
    def __init__(self, variant, cid):
        self.variant, self.cid = variant, cid
        self.args, self.buffers, self.memory, self.stubs, self.stack = {}, {}, [], [], []
        self.gs = None
        self.buf('guard', 0x10, '5a' * 0x10)
        self.memory.append({'addr': GUARD_GOT, 'pointer': 'guard'})

    def buf(self, name, size, fill=PATTERN):
        self.buffers[name] = {'size': size, 'bytes': (fill * size)[:2 * size] if len(fill) == 2 else fill}
        return name

    def put(self, name, off, data):
        self.memory.append({'addr': f'buf:{name}+{off:#x}', 'bytes': data})

    def ptr(self, name, off, target):
        self.memory.append({'addr': f'buf:{name}+{off:#x}', 'pointer': target})

    def stub(self, address, argc, **kw):
        self.stubs.append({'address': address, 'argc': argc, **kw})

    def transaction_clear(self):
        """The thread block: fs:0 points at itself; the transaction flag (offset -0x20) is zero."""
        self.buf('tls', THREAD_BLOCK + 8, '00')
        self.ptr('tls', THREAD_BLOCK, f'tls+{THREAD_BLOCK}')
        self.memory.append({'addr': f'0x{TRANSACTION_FLAG_OFFSET:08x}', 'bytes': h('<q', -0x20)})
        self.gs = f'buf:tls+{THREAD_BLOCK}'

    def json(self):
        c = {'schema': 1, 'address': ADDRESSES[self.variant], 'id': self.cid, 'returns': 'void', 'args': self.args,
             'buffers': self.buffers, 'memory': self.memory, 'imports': [], 'stubs': self.stubs}
        if self.stack:
            c['stack'] = self.stack
        if self.gs:
            c['gs'] = self.gs
        return c


def ids_case(cid, size, capacity, new_capacity, null=False, fails=False, old_off=0, fresh_ids=None):
    """size/capacity: the old vector's, in ids (null: no buffer at all); new_capacity: the argument."""
    c = Case('ids', cid)
    c.buf('vec', 0x30)
    c.buf('heap', 0x10, '3c')
    c.buf('heapvt', 0x80, 'e7')
    c.ptr('vec', 0x20, 'heap')
    c.ptr('heap', 0, 'heapvt')
    c.memory.append({'addr': f'buf:heapvt+{ALLOCATE_SLOT:#x}', 'guest': FAKE_ALLOCATE})
    c.memory.append({'addr': f'buf:heapvt+{FREE_SLOT:#x}', 'guest': FAKE_FREE})
    old = bytes(range(0x11, 0x11 + 4 * max(capacity, 1))) + b'\xab' * 8
    c.buf('old', old_off + len(old), '77')
    c.put('old', old_off, old.hex())
    if null:
        for off in (8, 0x10, 0x18):
            c.put('vec', off, h('<Q', 0))
    else:
        c.ptr('vec', 8, f'old+{old_off:#x}')
        c.ptr('vec', 0x10, f'old+{old_off + 4 * size:#x}')
        c.ptr('vec', 0x18, f'old+{old_off + 4 * capacity:#x}')
    c.buf('fresh', 4 * max(size, (new_capacity & 0xff), 1) + 0x10, '99' if fresh_ids is None else fresh_ids)
    c.args = {'rdi': 'buf:vec', 'rsi': hex(new_capacity & MASK)}
    c.stub(FAKE_ALLOCATE, 3, ret=0 if fails else 'buf:fresh')
    if fails:
        c.transaction_clear()
        c.stub(FATAL_ERROR, 3)
    if not null:
        c.stub(FAKE_FREE, 2)
    return c


def ids_cases():
    return [ids_case('empty_initial_0x20', 0, 0, 0x20, null=True),
            ids_case('grow_3_to_8', 3, 4, 8),
            ids_case('grow_full_5_to_9', 5, 5, 9),
            ids_case('same_capacity', 4, 4, 4),
            ids_case('allocated_but_empty', 0, 6, 7),
            ids_case('shrink_below_size', 6, 6, 2),
            ids_case('capacity_0', 2, 3, 0),
            ids_case('capacity_upper_bits', 2, 2, (1 << 32) | 3),
            ids_case('unaligned_old_buffer', 3, 3, 6, old_off=2),
            ids_case('fails_empty', 0, 0, 0x20, null=True, fails=True),
            ids_case('fails_with_one_id', 1, 4, 8, fails=True)]


def controller_case(cid, setup_array=None, reserve=None, setup_words=None, params=None, self_fill=PATTERN):
    """setup_array: (data buffer or None, size, capacity_and_flags) the setup leaves in the parameter
    array; reserve: (size, capacity) the reserve leaves (its buffer 'grown')."""
    c = Case('controller', cid)
    c.buf('self', 0xf0, self_fill)
    c.buf('bodies', 0x58, '11')
    c.buf('world', 0x40, '22')
    c.buf('params', 0x40, '33')
    c.put('params', 0, params or f32s(*[0.17 + i for i in range(12)]))
    c.buf('setbuf', 0x30 * 4, '44')
    c.buf('grown', 0x30 * 4, '55')
    c.args = {'rdi': 'buf:self', 'rsi': 'buf:bodies', 'rdx': 'buf:world', 'rcx': 'buf:params'}
    c.stub(CONTROLLER_BASE, 3, argi=[0, 1], argu8=[2], argmem=[{'arg': 0, 'size': 0xe0}])
    writes = []
    if setup_array:
        data, size, cap = setup_array
        writes.append({'arg': 0, 'offset': 0x40, 'pointer': data} if data else
                      {'arg': 0, 'offset': 0x40, 'bytes': h('<Q', 0)})
        writes.append({'arg': 0, 'offset': 0x48, 'bytes': h('<iI', size, cap)})
    if setup_words:
        writes.append({'arg': 0, 'offset': 0x68, 'bytes': h('<QQ', *setup_words)})
    c.stub(CONTROLLER_SETUP, 4, argi=[0, 1, 2], argu8=[3], argmem=[{'arg': 0, 'size': 0xe0}], writes=writes)
    if reserve:
        size, cap = reserve
        c.stub(RESERVE_MORE, 3, argi=[0, 1], argu32=[2], argmem=[{'arg': 1, 'size': 0x10}],
               writes=[{'arg': 1, 'offset': 0, 'pointer': 'grown'},
                       {'arg': 1, 'offset': 8, 'bytes': h('<iI', size, cap)}])
    return c


def controller_cases():
    return [controller_case('setup_leaves_empty', reserve=(0, 1)),
            controller_case('setup_flags_only', setup_array=(None, 0, 0x80000000), reserve=(0, 4)),
            controller_case('capacity_bit_30_only', setup_array=('setbuf', 3, 0x40000000), reserve=(0, 2)),
            controller_case('setup_buffer_kept', setup_array=('setbuf+0x0', 2, 3)),
            controller_case('setup_buffer_owned_bits', setup_array=('setbuf+0x30', 4, 0x80000004)),
            controller_case('reserve_leaves_size', reserve=(2, 4)),
            controller_case('setup_words', setup_array=('setbuf', 1, 1), setup_words=(0x1111, 0x2222)),
            controller_case('params_nan', params=f32s(NAN_A, -0.0, NAN_B, float('inf')) + '00' * 32,
                            reserve=(0, 1)),
            controller_case('zero_object', reserve=(0, 1), self_fill='00'),
            controller_case('ones_object', setup_array=('setbuf', 0, 1), self_fill='ff')]


def body_set_case(cid, count, header=0xffff0001, shapes=True, written=None, data_off=None, upper=0xdeadbeef00000000,
                  mode=5):
    """count: the source's index count; header: the shape set's size and references; written: the
    size the heap writes back (default count*4); data_off: hand back a buffer inside the source's
    indices at this byte offset."""
    c = Case('body_set', cid)
    c.buf('self', 0x60)
    c.buf('source', 0x90, '66')
    c.buf('part', 0x10, '77')
    c.buf('world', 0x20, '88')
    c.buf('shapes', 0x20, '99')
    c.put('shapes', 8, h('<I', header))
    n = max(count, 0)
    c.buf('indices', 4 * (n + 8), '00')
    c.put('indices', 0, b''.join(struct.pack('<i', 0x101 * (i + 1) - 50) for i in range(n + 8)).hex())
    c.buf('data', 4 * (n + 8), 'aa')
    c.buf('heapvt', 0x40, 'e7')
    c.memory.append({'addr': SHAPE_HEAP, 'pointer': 'heapvt'})
    c.memory.append({'addr': f'buf:heapvt+{BUF_ALLOC_SLOT:#x}', 'guest': FAKE_BUF_ALLOC})
    if shapes:
        c.ptr('source', 0x78, 'shapes')
    else:
        c.put('source', 0x78, h('<Q', 0))
    c.ptr('source', 0x80, 'indices')
    c.put('source', 0x88, h('<iI', count, 0x80000000 | (n + 8)))
    c.args = {'rdi': 'buf:self', 'rsi': 'buf:source', 'rdx': 'buf:world', 'rcx': 'buf:part',
              'r8': hex(upper | 0x12345678), 'r9': hex(upper | 0xfffffffe)}
    c.stack = [hex(upper | (mode & 0xffffffff))]
    c.stub(BODY_SET_BASE, 7, argi=[0, 1, 2, 3], argu32=[4, 5, 6], argmem=[{'arg': 0, 'size': 0x58}])
    if count > 0:
        back = count * 4 if written is None else written
        c.stub(FAKE_BUF_ALLOC, 2, argi=[0], argmem=[{'arg': 1, 'size': 4}],
               writes=[{'arg': 1, 'bytes': h('<i', back)}],
               ret='buf:data' if data_off is None else f'buf:indices+{data_off:#x}')
    return c


def body_set_cases():
    return [body_set_case('count_0', 0),
            body_set_case('count_negative', -3),
            body_set_case('count_1', 1),
            body_set_case('count_5', 5),
            body_set_case('count_16_wide', 16),
            body_set_case('count_37_wide_and_tail', 37),
            body_set_case('shapes_null', 2, shapes=False),
            body_set_case('shapes_not_counted', 2, header=0x0000ffff),
            body_set_case('shapes_counted_first', 2, header=0x00010000),
            body_set_case('shapes_count_wraps', 2, header=0x0003ffff),
            body_set_case('heap_rounds_up', 5, written=0x40),
            body_set_case('heap_odd_size', 3, written=0x15),
            body_set_case('heap_negative_size', 3, written=-9),
            body_set_case('buffer_overlaps_source', 6, data_off=4),
            body_set_case('buffer_overlaps_wide', 20, data_off=8),
            body_set_case('mode_negative_no_upper', 2, upper=0, mode=-1)]


CASES = {'ids': ids_cases, 'controller': controller_cases, 'body_set': body_set_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in CASES:
        print(__doc__, file=sys.stderr)
        return 2
    out = [c.json() for c in CASES[sys.argv[2]]()]
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
