#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/heap_buffer_owner_families.cpp's families, which family_cases.py
runs on every member at its own address. Generated fixtures, not game data.

destroys_heap_buffer_owner_directly, ..._through_base_at_0x38 and ..._through_base_at_0x48: an
owner (`owner`) entered at its start or 0x38 / 0x48 bytes in, the heap the global at 0x057e7320
points at, whose free slot (0x68) is a scripted stub (with a spare call), and a counted object
(`counted`) whose release (0x027ee1e0) is a scripted stub comparing the state dword it is given.
The handle at +0x8 is null, tagged (odd: 1, a large one, another) or the counted object, whose
state dword has counts 0, 1, 2, the largest, and counts beside set upper bits (and upper bits
over a zero count). The buffer at +0x20 is a block, null, or the owner itself: then the free stub
compares the owner's first 0x28 bytes (the vtable is stored before the free) and some of these
cases write a new handle through it (null, tagged or the counted object, over each kind), which
shows the handle is read after the free.

frees_to_global_heap: the argument (a block, null, an inner pointer, an odd and an all-ones
value) is passed to that heap's free slot.
"""
from __future__ import annotations

import struct

GLOBAL_HEAP = '0x057e7320'
COUNTED_RELEASED = '0x027ee1e0'
FAKE_FREE = '0x00500400'
HEAP_FREE_SLOT = 0x68
OWNER_HANDLE, OWNER_BUFFER = 0x8, 0x20
COUNTED_STATE = 0x20
MASK = 0xffffffffffffffff


def _u64(value: int) -> str:
    return struct.pack('<Q', value & MASK).hex()


def _u32(value: int) -> str:
    return struct.pack('<I', value & 0xffffffff).hex()


def _heap(buffers: dict, memory: list) -> None:
    buffers['heap'] = {'size': 0x10, 'bytes': '3c' * 0x10}
    buffers['heapvt'] = {'size': 0x80, 'bytes': 'e7' * 0x80}
    memory.append({'addr': GLOBAL_HEAP, 'pointer': 'heap'})
    memory.append({'addr': 'buf:heap+0x0', 'pointer': 'heapvt'})
    memory.append({'addr': f'buf:heapvt+{HEAP_FREE_SLOT:#x}', 'guest': FAKE_FREE})


def owner_case(address: str, base: int, cid: str, handle, state: int = 1, buffer: str = 'block',
               free_writes=None) -> dict:
    """handle: None (null), an int (tagged value) or 'counted'; state: the counted object's state
    dword; buffer: 'block', 'null' or 'owner'; free_writes: what the free stub stores as the
    handle (buffer 'owner' only): None, an int or 'counted'."""
    buffers = {'owner': {'size': 0x60, 'bytes': 'cd' * 0x60},
               'block': {'size': 0x20, 'bytes': '81' * 0x20},
               'counted': {'size': 0x30, 'bytes': '5a' * 0x30}}
    memory = []
    _heap(buffers, memory)
    memory.append({'addr': f'buf:counted+{COUNTED_STATE:#x}', 'bytes': _u32(state)})
    if handle == 'counted':
        memory.append({'addr': f'buf:owner+{OWNER_HANDLE:#x}', 'pointer': 'counted'})
    else:
        memory.append({'addr': f'buf:owner+{OWNER_HANDLE:#x}', 'bytes': _u64(handle or 0)})
    if buffer == 'null':
        memory.append({'addr': f'buf:owner+{OWNER_BUFFER:#x}', 'bytes': _u64(0)})
    else:
        memory.append({'addr': f'buf:owner+{OWNER_BUFFER:#x}', 'pointer': buffer})
    free = {'address': FAKE_FREE, 'argc': 2}
    if buffer == 'owner':
        free['argmem'] = [{'arg': 1, 'offset': 0, 'size': OWNER_BUFFER + 8}]
        if free_writes == 'counted':
            free['writes'] = [{'arg': 1, 'offset': OWNER_HANDLE, 'pointer': 'counted'}]
        elif free_writes is not None:
            free['writes'] = [{'arg': 1, 'offset': OWNER_HANDLE, 'bytes': _u64(free_writes)}]
    release = {'address': COUNTED_RELEASED, 'argc': 1,
               'argmem': [{'arg': 0, 'offset': COUNTED_STATE, 'size': 4}]}
    stubs = [free, {'address': FAKE_FREE, 'argc': 2}, release, dict(release)]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': f'buf:owner+{base:#x}', 'rsi': '0x5151515151515151'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


TAGGED = [('tagged_1', 1), ('tagged_large', 0x8000000000000001), ('tagged_other', 0x123457)]
STATES = [('count_0', 0), ('count_1', 1), ('count_2', 2), ('count_max', 0x3fffff),
          ('count_1_upper_bits', 0x80400001), ('count_0_upper_bits', 0xffc00000),
          ('count_5_flag_bit_22', 0x400005)]


def _owner_cases(base: int):
    def cases(address: str, value: int | None) -> list[dict]:
        out = []
        for buffer in ('block', 'null', 'owner'):
            out.append(owner_case(address, base, f'null_handle_{buffer}', None, buffer=buffer))
            for name, tagged in TAGGED:
                out.append(owner_case(address, base, f'{name}_{buffer}', tagged, buffer=buffer))
            for name, state in STATES:
                out.append(owner_case(address, base, f'counted_{name}_{buffer}', 'counted', state, buffer))
        # The free stub stores a new handle: the destructor must act on the one it reads after.
        for before_name, before in (('null', None), ('tagged', 0x123457), ('counted', 'counted')):
            for after_name, after in (('null', 0), ('tagged', 0x8001), ('counted', 'counted')):
                if before_name != after_name:
                    out.append(owner_case(address, base, f'free_sets_{after_name}_over_{before_name}', before,
                                          state=0x10, buffer='owner', free_writes=after))
        return out
    return cases


directly_cases = _owner_cases(0)
through_base_at_0x38_cases = _owner_cases(0x38)
through_base_at_0x48_cases = _owner_cases(0x48)


def free_case(address: str, cid: str, data) -> dict:
    buffers = {'block': {'size': 0x40, 'bytes': '81' * 0x40}}
    memory = []
    _heap(buffers, memory)
    stubs = [{'address': FAKE_FREE, 'argc': 2}, {'address': FAKE_FREE, 'argc': 2}]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void',
            'args': {'rdi': data, 'rsi': '0x5151515151515151'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def free_cases(address: str, value: int | None) -> list[dict]:
    return [free_case(address, 'block', 'buf:block'),
            free_case(address, 'inner_pointer', 'buf:block+0x18'),
            free_case(address, 'null', 0),
            free_case(address, 'odd_value', hex(0x123456789abd)),
            free_case(address, 'all_ones', hex(MASK))]
