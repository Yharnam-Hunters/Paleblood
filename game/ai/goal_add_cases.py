#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the goal-adding script bindings in goal_add.cpp (invented data, not
game recordings): AddTopGoal (0 to 7 parameters), AddSubGoal (0 to 13) and AddSubGoal_Front.

usage: goal_add_cases.py OUT_DIR

The bindings call the game's goal-adding core (0x0164cc60, or 0x0164d4f0 for the front), and
the cases let it run for real against a fake think: a goal pool, the owner goal's sub-goal
queue, the AI heap allocator (0x05940420) behind a fake vtable. What the core calls further is
stubbed and compared: goal init (0x0164b8d0), the float vector's insert (0x0164f600) and
assignment (0x02a8e940), the queue's growth (0x01650190), the allocator's query, allocation and
free, and the fatal report (0x024b55b0). So every parameter a binding passes, in xmm0..xmm7 or
on the stack, ends up in compared memory: the pool slot (life at +0x28, parameters 1..5 at
+0x60) and the vector buffer (parameters 6..15, as long as the last nonzero one).

Situations: the first slot free, a later one, none free (all 32 in use), a negative id; the
queue empty, holding a goal (the front variant clears its active flag and passes on its 0x4
flag), a missing block (allocated through the queue's allocator), a full map (growth stubbed);
the allocator query not reporting ready (the fatal report runs, then the core goes on);
parameters with NaN payloads, infinities, -0 (counts as zero for the vector), denormals,
zeros between nonzero ones, garbage in the upper lanes and in the upper half of stack slots and
of the id register; and seeded random parameter sets for every binding.
"""
import json
from pathlib import Path
import random
import struct
import sys

# The bindings: (registry suffix, address, owner kind, parameter count, core).
CORE, CORE_FRONT = '0x0164cc60', '0x0164d4f0'
TOP = [(f'ai_func_add_top_goal_{n}', a, 'top', n, CORE) for n, a in enumerate(
    ('0x01656fc0', '0x01657040', '0x016570c0', '0x01657130', '0x016571a0', '0x01657210',
     '0x01657280', '0x016572e0'))]
SUB = [(f'ai_goal_func_add_sub_goal_{n}', a, 'sub', n, CORE) for n, a in enumerate(
    ('0x0167ec30', '0x0167ecb0', '0x0167ed20', '0x0167ed90', '0x0167ee00', '0x0167ee70',
     '0x0167eed0', '0x0167ef30', '0x0167ef90', '0x0167eff0', '0x0167f050', '0x0167f0b0',
     '0x0167f120', '0x0167f190'))]
FRONT = [('ai_goal_func_add_sub_goal_front', '0x0167f200', 'sub', 3, CORE_FRONT)]
BINDINGS = TOP + SUB + FRONT

GOAL_INIT = '0x0164b8d0'
VECTOR_INSERT = '0x0164f600'
VECTOR_ASSIGN = '0x02a8e940'
QUEUE_GROW = '0x01650190'
FATAL_ERROR = '0x024b55b0'
HEAP_GLOBAL = '0x05940420'
# Game addresses the fake allocator vtable points at; the cases stub them, so they never run.
HEAP_QUERY, HEAP_ALLOCATE, HEAP_FREE = '0x016dd820', '0x016e4500', '0x016dda50'
HEAP_READY = 0x20               # the query's ready bit

POOL_SLOTS, SLOT_SIZE = 32, 0x3e8
SLOT_ID, SLOT_FLAGS = 0xa0, 0x3e0
THINK_SIZE, THINK_POOL, THINK_ROOT, THINK_TAG = 0xa38, 0x50, 0x60, 0xa34
OWNER_SIZE = 0x60               # through the sub-goal queue at +0x30
QUEUE = 0x30                    # proxy, map, map size, offset, count, allocator
MAP_SIZE = 4
PARAMS = 15

MASK64 = (1 << 64) - 1


def bits(word):
    return struct.pack('<I', word & 0xffffffff).hex()


def f32(value):
    return struct.pack('<f', value).hex()


def is_zero(hex_bits):
    """vucomiss against 0.0: +0 and -0 are zero, NaN is not."""
    return int.from_bytes(bytes.fromhex(hex_bits[:8]), 'little') & 0x7fffffff == 0


def pattern(size, byte):
    return bytes([(byte + i * 7) & 0xff for i in range(size)])


def pool_bytes(free_slots, byte=0x21):
    pool = bytearray(pattern(POOL_SLOTS * SLOT_SIZE, byte))
    for slot in range(POOL_SLOTS):
        goal_id = -1 if slot in free_slots else 3000 + slot
        pool[slot * SLOT_SIZE + SLOT_ID:slot * SLOT_SIZE + SLOT_ID + 4] = struct.pack('<i', goal_id)
    return pool


def qword(value):
    return struct.pack('<Q', value & MASK64).hex()


def make_case(binding, cid, goal_id, life, params, *, free_slots=(0,), queue='empty', heap_ready=True,
              upper_lanes=False, id_upper=0, stack_upper=0):
    """params: up to the binding's count of 8-hex-digit float bit strings (the rest pass as zero)."""
    name, address, kind, count, core = binding
    params = list(params)[:count] + [f32(0.0)] * (count - min(count, len(params)))
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'i64', 'args': {}, 'buffers': {},
         'memory': [], 'imports': [], 'stubs': []}
    owner = bytearray(pattern(OWNER_SIZE, 0x41))
    buffers = {
        'func': bytes(16),
        'think': pattern(THINK_SIZE, 0x61),
        'owner': bytes(owner),
        'pool': bytes(pool_bytes(set(free_slots))),
        'map': bytes(8 * MAP_SIZE),
        'blocks': pattern(16 * MAP_SIZE, 0x81),
        'proxy': bytes(8),
        'heap': bytes(16),
        'heap_vtable': bytes(0x80),
        'queue_heap': bytes(16),
        'queue_heap_vtable': bytes(0x80),
        'new_block': pattern(16, 0x91),
        'vector': bytes(4 * 10),
        'front_goal': pattern(SLOT_SIZE, 0xa1),
    }
    c['buffers'] = {k: {'size': len(v), 'bytes': v.hex()} for k, v in buffers.items()}
    mem = c['memory']
    mem.append({'addr': 'buf:func+8', 'pointer': 'think' if kind == 'top' else 'owner'})
    mem.append({'addr': f'buf:think+{THINK_ROOT:#x}', 'pointer': 'owner'})
    mem.append({'addr': f'buf:think+{THINK_POOL:#x}', 'pointer': 'pool'})
    mem.append({'addr': 'buf:owner+0', 'pointer': 'think'})
    # The queue: proxy -> the queue itself, map of MAP_SIZE blocks of two goal pointers.
    offset, size = {'empty': (0, 0), 'one_goal': (0, 1), 'wrapped': (5, 2), 'no_block': (0, 0),
                    'full_map': (1, 2 * MAP_SIZE - 1)}[queue]
    mem.append({'addr': f'buf:owner+{QUEUE:#x}', 'pointer': 'proxy'})
    mem.append({'addr': 'buf:proxy+0', 'pointer': f'owner+{QUEUE:#x}'})
    mem.append({'addr': f'buf:owner+{QUEUE + 0x08:#x}', 'pointer': 'map'})
    mem.append({'addr': f'buf:owner+{QUEUE + 0x10:#x}', 'bytes': qword(MAP_SIZE)})
    mem.append({'addr': f'buf:owner+{QUEUE + 0x18:#x}', 'bytes': qword(offset)})
    mem.append({'addr': f'buf:owner+{QUEUE + 0x20:#x}', 'bytes': qword(size)})
    mem.append({'addr': f'buf:owner+{QUEUE + 0x28:#x}', 'pointer': 'queue_heap'})
    for block in range(MAP_SIZE):
        if queue == 'no_block':
            continue
        mem.append({'addr': f'buf:map+{8 * block:#x}', 'pointer': f'blocks+{16 * block:#x}'})
    if size:
        # The goal at the queue's front (the front variant reads its flags).
        front = offset
        mem.append({'addr': f'buf:blocks+{16 * (front // 2 % MAP_SIZE) + 8 * (front % 2):#x}',
                    'pointer': 'front_goal'})
    mem.append({'addr': HEAP_GLOBAL, 'pointer': 'heap'})
    mem.append({'addr': 'buf:heap+0', 'pointer': 'heap_vtable'})
    mem.append({'addr': 'buf:heap_vtable+0x20', 'guest': HEAP_QUERY})
    mem.append({'addr': 'buf:heap_vtable+0x70', 'guest': HEAP_FREE})
    mem.append({'addr': 'buf:queue_heap+0', 'pointer': 'queue_heap_vtable'})
    mem.append({'addr': 'buf:queue_heap_vtable+0x58', 'guest': HEAP_ALLOCATE})

    # Arguments: the binding object, the id, the life and the parameters.
    args = {'rdi': 'buf:func', 'rsi': (goal_id & 0xffffffff) | (id_upper << 32)}
    lanes = 'a1b2c3d4e5f60718293a4b5c' if upper_lanes else ''
    args['xmm0'] = life + lanes
    for i, p in enumerate(params[:7]):
        args[f'xmm{i + 1}'] = p + lanes
    c['args'] = args
    if count > 7:
        c['stack'] = [int.from_bytes(bytes.fromhex(p), 'little') | (stack_upper << 32) for p in params[7:]]

    # What the core calls, in order.
    stubs = c['stubs']
    all_params = params + [f32(0.0)] * (PARAMS - len(params))
    if goal_id < 0:
        return c
    if not free_slots:
        return c
    stubs.append({'address': GOAL_INIT, 'argc': 2, 'argi': [0], 'argu32': [1]})
    stubs.append({'address': HEAP_QUERY, 'argc': 3, 'argi': [0, 1, 2],
                  'writes': [{'arg': 0, 'bytes': f'{HEAP_READY if heap_ready else 0:02x}'}]})
    if not heap_ready:
        stubs.append({'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]})
    length = 0
    for index, p in enumerate(all_params[5:], start=1):
        if is_zero(p):
            continue
        if length < index:
            stubs.append({'address': VECTOR_INSERT, 'argc': 4, 'argi': [0, 1, 2, 3],
                          'writes': [{'arg': 0, 'offset': 0x38, 'pointer': 'vector'},
                                     {'arg': 0, 'offset': 0x40, 'pointer': f'vector+{4 * index:#x}'}]})
            length = index
    # The list it copies from: begin, end, capacity and allocator (the word before is never set).
    stubs.append({'address': VECTOR_ASSIGN, 'argc': 2, 'argi': [0, 1],
                  'argmem': [{'arg': 1, 'offset': 8, 'size': 0x20}]})
    # The queue grows its map when the new goal starts a block and the map is half full.
    starts_block = (offset + size) % 2 == 0 if core == CORE else offset % 2 == 0
    grows = starts_block and MAP_SIZE <= (size + 2) // 2
    if grows:
        stubs.append({'address': QUEUE_GROW, 'argc': 1, 'argi': [0]})
    if queue == 'no_block':
        stubs.append({'address': HEAP_ALLOCATE, 'argc': 3, 'argi': [0, 1, 2], 'ret': 'buf:new_block'})
    if length:
        stubs.append({'address': HEAP_FREE, 'argc': 2, 'argi': [0, 1]})
    return c


NAN_A, NAN_B = 0x7fc12345, 0xffc54321
SPECIAL = [bits(NAN_A), bits(NAN_B), bits(0x7f800000), bits(0xff800000), bits(0x80000000),
           bits(0x00000001), bits(0x80000001), bits(0x7f800001)]


def binding_cases(binding):
    name, address, kind, count, core = binding
    ordinal = [f32(1.0 + i) for i in range(count)]
    out = [make_case(binding, 'plain', 2000, f32(5.0), ordinal),
           make_case(binding, 'endless_life', 2001, f32(-1.0), ordinal),
           make_case(binding, 'all_zero', 2002, f32(0.0), [f32(0.0)] * count),
           make_case(binding, 'later_slot', 2003, f32(2.5), ordinal, free_slots=(7, 19)),
           make_case(binding, 'last_slot', 2004, f32(2.5), ordinal, free_slots=(31,)),
           make_case(binding, 'pool_full', 2005, f32(2.5), ordinal, free_slots=()),
           make_case(binding, 'negative_id', -1, f32(2.5), ordinal),
           make_case(binding, 'id_min', -0x80000000, f32(2.5), ordinal),
           make_case(binding, 'id_zero', 0, f32(2.5), ordinal),
           make_case(binding, 'id_garbage_above', 2006, f32(1.0), ordinal, id_upper=0xdeadbeef),
           make_case(binding, 'queue_one_goal', 2007, f32(1.0), ordinal, queue='one_goal'),
           make_case(binding, 'queue_wrapped', 2008, f32(1.0), ordinal, queue='wrapped'),
           make_case(binding, 'queue_no_block', 2009, f32(1.0), ordinal, queue='no_block'),
           make_case(binding, 'queue_full_map', 2010, f32(1.0), ordinal, queue='full_map'),
           make_case(binding, 'heap_not_ready', 2011, f32(1.0), ordinal, heap_ready=False),
           make_case(binding, 'upper_lanes', 2012, f32(3.0), ordinal, upper_lanes=True,
                     stack_upper=0x5a5a5a5a)]
    for life_name, life in (('nan', bits(NAN_A)), ('negative_nan', bits(NAN_B)), ('inf', bits(0x7f800000)),
                            ('minus_zero', bits(0x80000000)), ('denormal', bits(1))):
        out.append(make_case(binding, f'life_{life_name}', 2013, life, ordinal))
    if count:
        for i, special in enumerate(SPECIAL):
            params = [SPECIAL[(i + k) % len(SPECIAL)] for k in range(count)]
            out.append(make_case(binding, f'special_{i}', 2014, f32(1.0), params))
        for k in range(count):
            # One nonzero parameter at a time: the vector is as long as the last nonzero one.
            params = [f32(0.0)] * count
            params[k] = bits(NAN_A + k)
            out.append(make_case(binding, f'only_{k + 1}', 2015, f32(1.0), params))
            params = [f32(0.5 * (j + 1)) for j in range(count)]
            params[k] = bits(0x80000000)
            out.append(make_case(binding, f'minus_zero_{k + 1}', 2016, f32(1.0), params))
    rng = random.Random(f'1094-{name}')
    for i in range(12):
        params = [rng.choice((f32(0.0), f32(rng.uniform(-1e3, 1e3)), rng.choice(SPECIAL),
                              bits(rng.getrandbits(32)))) for _ in range(count)]
        out.append(make_case(binding, f'random_{i:02d}', rng.randrange(0, 1 << 31),
                             bits(rng.getrandbits(32)), params,
                             free_slots=tuple(sorted(rng.sample(range(POOL_SLOTS), rng.randrange(1, 4)))),
                             queue=rng.choice(('empty', 'one_goal', 'wrapped')),
                             upper_lanes=rng.random() < 0.5, stack_upper=rng.getrandbits(32)))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    total = 0
    for binding in BINDINGS:
        directory = Path(sys.argv[1]) / binding[0]
        directory.mkdir(parents=True, exist_ok=True)
        for c in binding_cases(binding):
            (directory / f"{c['id']}.json").write_text(json.dumps(c, indent=1))
            total += 1
    print(f'{total} cases for {len(BINDINGS)} bindings written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
