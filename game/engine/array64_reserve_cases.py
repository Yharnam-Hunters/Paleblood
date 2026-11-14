#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the engine array reallocation copies for 8-byte elements
(game/engine/array64_reserve.cpp), for tools/verify.py run.

usage: array64_reserve_cases.py OUT_DIR ADDRESS   (one of the copies in COPIES)

Each case lays out an engine array (`array`: begin, end, capacity end, allocator) over distinct
64-bit values (`old`) and a fake allocator whose allocate and free slots are scripted stubs (with a
spare call each). The new buffer (`fresh`) holds other values. The cases are those of
physics/controller_body_set_cases.py's id vector reserve, for 8-byte elements:
- an empty array with no buffer; growing, full, the same capacity, allocated but empty;
- a capacity below the count, capacity 0, and capacities with upper bits set (the byte size wraps);
- an old buffer that is only 4-aligned, one element, eight elements;
- a new buffer one element past the old one's start, so the copy runs over its own source
  (forward);
- a failed allocation (the thread's transaction flag clear) over an empty array and one with a
  single element, which goes on with the null buffer (with two or more elements both the game and
  the replacement fault writing the second to address 8).
Generated fixtures, not game data.
"""
import json
import os
import struct
import sys

COPIES = {
    '0x016a5130',
    '0x016b1290',
    '0x016d1fc0',
    '0x016d9f70',
    '0x016e1490',
    '0x0179c3b0',
    '0x0179fd90',
    '0x0187df30',
    '0x0189a350',
    '0x0189e790',
    '0x018bf2c0',
    '0x01904bb0',
    '0x0193ed60',
    '0x01c42780',
    '0x01c62b70',
    '0x01ceb6a0',
    '0x0215f2f0',
    '0x02255b50',
    '0x02266eb0',
    '0x02266fb0',
    '0x02273af0',
    '0x022b03e0',
    '0x022b0720',
    '0x02463540',
    '0x02471310',
    '0x02471410',
    '0x024fa650',
    '0x024fa750',
    '0x02559480',
    '0x025c4630',
    '0x0260fdf0',
    '0x0265c6a0',
    '0x0265e210',
    '0x02682520',
    '0x02686cd0',
    '0x02686dd0',
    '0x026c07c0',
    '0x026c08c0',
    '0x026f4c50',
    '0x02706ac0',
    '0x02707280',
    '0x027077a0',
    '0x02a46410',
    '0x02a6e960',
    '0x02a6f3d0',
    '0x02a76a30',
    '0x02a78620',
    '0x02a8e840',
    '0x02a930b0',
    '0x02aa3430',
    '0x02b15c20',
    '0x02b17760',
    '0x02b18a60',
    '0x02b1c130',
    '0x02b220f0',
    '0x02b26b10',
    '0x02b2a280',
    '0x02b2e010',
    '0x02b2e1f0',
    '0x02b2e590',
    '0x02b30c50',
    '0x02b32400',
    '0x02b325f0',
    '0x02b33fc0',
    '0x02b34ac0',
    '0x02b3ad50',
    '0x02b3ae50',
    '0x02b3ba60',
    '0x02b5ae80',
    '0x02b5c400',
    '0x02ba3260',
    '0x02ba85a0',
    '0x02bbb730',
    '0x02bc2280',
    '0x02bc2480',
    '0x02bc2580',
    '0x02bc6170',
    '0x02bc6850',
    '0x02bc72c0',
    '0x02bc73d0',
    '0x02bcaa60',
    '0x02bcad00',
    '0x02bcb1d0',
    '0x02bda710',
    '0x02bdee60',
    '0x02bdfa20',
    '0x02be19f0',
    '0x02be1af0',
    '0x02c01ea0',
    '0x02c026b0',
    '0x02c03680',
    '0x02c03ca0',
    '0x02c04cb0',
    '0x02c0a100',
    '0x02c12de0',
    '0x02c17760',
    '0x02c19a90',
    '0x02c2b3f0',
    '0x02c54a90',
    '0x02c69d80',
    '0x02cf9ac0',
    '0x02d064b0',
    '0x02d1ad80',
    '0x02d27cd0',
    '0x02d2a2f0',
    '0x02d2dcb0',
    '0x02d413b0',
    '0x02d43fb0',
    '0x02d44190',
    '0x02d44d00',
    '0x02d48ea0',
    '0x02d49460',
    '0x02d4acf0',
    '0x02d4c0b0',
    '0x02d56420',
    '0x02d688b0',
    '0x02d689b0',
    '0x02d690f0',
    '0x02d691f0',
    '0x02d6abe0',
    '0x02d6ace0',
    '0x02d6ade0',
    '0x02d94450',
    '0x02dab5f0',
    '0x02dae610',
    '0x02daf010',
    '0x02db1cc0',
    '0x02db5140',
    '0x02db8230',
    '0x02dba990',
    '0x02dc2010',
    '0x02dce2f0',
    '0x02dd07f0',
    '0x02dd2070',
    '0x02dd2170',
    '0x02dd2270',
    '0x02dd2370',
    '0x02dd2590',
    '0x02dd6e20',
    '0x02dd8270',
    '0x02dd9a30',
    '0x02ddef60',
    '0x02ddf4c0',
    '0x02ddf5c0',
    '0x02ddf990',
    '0x02de1510',
    '0x02de54c0',
    '0x02de55c0',
    '0x02e60d80',
    '0x02e73730',
    '0x02e73830',
    '0x02e73ca0',
    '0x02ec98d0',
    '0x02ecc6a0',
    '0x02ed5560',
    '0x02edc7e0',
    '0x02ee6f00',
    '0x02f6e1c0',
    '0x02f90730',
}
FATAL_ERROR = '0x024b55b0'
TRANSACTION_FLAG_OFFSET = 0x057e4318
THREAD_BLOCK = 0x40
FAKE_ALLOCATE, FAKE_FREE = '0x00500200', '0x00500220'
ALLOCATE_SLOT, FREE_SLOT = 0x58, 0x70
MASK = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & MASK).hex()


def words(seed, count):
    """Distinct 64-bit values, every byte different from its neighbours."""
    return ''.join(u64(0x0102030405060708 * (seed + i) ^ 0xa5a5a5a5a5a5a5a5) for i in range(count))


def case(address, cid, size, capacity, new_capacity, null=False, fails=False, old_off=0, overlap=False):
    """size/capacity: the old array's, in elements (null: no buffer at all); new_capacity: the argument."""
    buffers, memory, stubs = {}, [], []

    def buf(name, n, fill):
        buffers[name] = {'size': n, 'bytes': fill * n if len(fill) == 2 else fill}

    def put(name, off, data):
        memory.append({'addr': f'buf:{name}+{off:#x}', 'bytes': data})

    def ptr(name, off, target):
        memory.append({'addr': f'buf:{name}+{off:#x}', 'pointer': target})

    buf('array', 0x30, 'cd')
    buf('heap', 0x10, '3c')
    buf('heapvt', 0x80, 'e7')
    ptr('array', 0x20, 'heap')
    ptr('heap', 0, 'heapvt')
    memory.append({'addr': f'buf:heapvt+{ALLOCATE_SLOT:#x}', 'guest': FAKE_ALLOCATE})
    memory.append({'addr': f'buf:heapvt+{FREE_SLOT:#x}', 'guest': FAKE_FREE})
    old_size = old_off + 8 * max(capacity, 1) + 0x18
    buf('old', old_size, '77')
    put('old', old_off, words(0x11, max(capacity, 1)))
    if null:
        for off in (8, 0x10, 0x18):
            put('array', off, u64(0))
    else:
        ptr('array', 8, f'old+{old_off:#x}')
        ptr('array', 0x10, f'old+{old_off + 8 * size:#x}')
        ptr('array', 0x18, f'old+{old_off + 8 * capacity:#x}')
    if fails:
        fresh = 0
    elif overlap:
        fresh = f'buf:old+{old_off + 8:#x}'
    else:
        buf('fresh', 8 * max(size, new_capacity & 0xff, 1) + 0x10, words(0x51, max(size, new_capacity & 0xff, 1) + 2))
        fresh = 'buf:fresh'
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': fresh})
    stubs.append({'address': FAKE_ALLOCATE, 'argc': 3, 'ret': 0})
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
         'args': {'rdi': 'buf:array', 'rsi': hex(new_capacity & MASK)},
         'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}
    if fails:
        # The thread block: fs:0 points at itself; the transaction flag (offset -0x20) is zero.
        buf('tls', THREAD_BLOCK + 8, '00')
        ptr('tls', THREAD_BLOCK, f'tls+{THREAD_BLOCK}')
        memory.append({'addr': f'0x{TRANSACTION_FLAG_OFFSET:08x}', 'bytes': struct.pack('<q', -0x20).hex()})
        c['gs'] = f'buf:tls+{THREAD_BLOCK}'
        stubs.append({'address': FATAL_ERROR, 'argc': 3})
    if not null:
        stubs.append({'address': FAKE_FREE, 'argc': 2})
        stubs.append({'address': FAKE_FREE, 'argc': 2})
    return c


def reserve_cases(address):
    return [case(address, 'empty_initial_0x20', 0, 0, 0x20, null=True),
            case(address, 'grow_3_to_8', 3, 4, 8),
            case(address, 'grow_full_5_to_9', 5, 5, 9),
            case(address, 'same_capacity', 4, 4, 4),
            case(address, 'allocated_but_empty', 0, 6, 7),
            case(address, 'one_element', 1, 1, 2),
            case(address, 'eight_elements', 8, 8, 12),
            case(address, 'shrink_below_size', 6, 6, 2),
            case(address, 'capacity_0', 2, 3, 0),
            case(address, 'capacity_upper_bits', 2, 2, (1 << 32) | 3),
            case(address, 'capacity_byte_size_wraps', 2, 2, (1 << 61) | 3),
            case(address, 'old_buffer_4_aligned', 3, 3, 6, old_off=4),
            case(address, 'new_buffer_over_the_old', 4, 6, 6, overlap=True),
            case(address, 'fails_empty', 0, 0, 0x20, null=True, fails=True),
            case(address, 'fails_with_one_element', 1, 4, 8, fails=True)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in COPIES:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = reserve_cases(sys.argv[2])
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
