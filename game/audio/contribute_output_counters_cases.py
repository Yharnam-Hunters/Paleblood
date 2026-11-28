#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate FMOD output-counter fixtures with scripted and real contributions."""
import itertools
import json
from pathlib import Path
import struct
import sys


OWNER = 32
CALLBACK = OWNER + 0x38
REGISTERED = OWNER + 0x248
MIX_COUNT = OWNER + 0x6c4
MIX = OWNER + 0x6c8
SENTINEL = OWNER + 0x90
SEPARATE_COUNTERS = 2608
NODE = 3024
CHILD = 3200
VTABLE = 3376
ARENA_BYTES = 3456
MASK = 0xffffffff
ADD = '0x011abd10'
NESTED = '0x011b0d40'
# The fixture's virtual call is intercepted at an existing mapped function.
VIRTUAL = '0x011186e0'


def pointer(offset):
    return f'buf:arena+{offset}'


def assign(offset, value, width):
    return {'addr': pointer(offset), 'bytes': value.to_bytes(width, 'little').hex()}


def link(offset, target):
    return {'addr': pointer(offset), 'pointer': f'arena+{target}'}


def case(name, *, counter=SEPARATE_COUNTERS, flag=0, mix=1, count=0x10203040,
         result=0, original=False, node=False, child=False, child_count=0,
         add_effects=(), second_effects=(), nested_effects=(), fill=0xa5):
    """Keep the complete owner, both potential counter writes and list objects mapped."""
    data = bytearray([fill] * ARENA_BYTES)
    if counter is not None:
        assert 0 <= counter <= ARENA_BYTES - 0xc4
        struct.pack_into('<I', data, counter + 0x10, 0xfffffff0)
        struct.pack_into('<I', data, counter + 0xc0, 0x80000019)
    data[REGISTERED] = flag
    struct.pack_into('<I', data, MIX_COUNT, count)
    struct.pack_into('<Q', data, MIX, mix)
    struct.pack_into('<Q', data, OWNER + 0x50, 0)
    memory = [link(SENTINEL, NODE if node else SENTINEL)]
    stubs = []
    if original:
        if node:
            memory.append(link(NODE, SENTINEL))
            if child:
                memory.extend([link(NODE + 0x48, CHILD), link(CHILD, VTABLE),
                               {'addr': pointer(VTABLE + 8), 'guest': VIRTUAL}])
                stubs.append({'address': VIRTUAL, 'argc': 2, 'ret': result,
                              'writes': list(nested_effects)})
            else:
                struct.pack_into('<Q', data, NODE + 0x48, 0)
            struct.pack_into('<Q', data, NODE + 0x60, int(child_count != 0))
            struct.pack_into('<I', data, CHILD + 0x60, child_count)
    else:
        for effects in (add_effects, second_effects):
            stubs.append({'address': ADD, 'argc': 4, 'argi': [0], 'argu8': [1],
                          'argu32': [2, 3], 'writes': list(effects)})
        stubs.append({'address': NESTED, 'argc': 2, 'ret': result,
                      'argmem': [{'arg': 0, 'offset': 0x248, 'size': 1}],
                      'writes': list(nested_effects)})
    return {'schema': 1, 'address': '0x0111b250', 'id': name, 'returns': 'i32',
            'args': {'rdi': pointer(CALLBACK),
                     'rsi': '0x0' if counter is None else pointer(counter)},
            'buffers': {'arena': {'size': len(data), 'bytes': data.hex()}},
            'memory': memory, 'imports': [], 'stubs': stubs}


def cases():
    """Cover registration, removal, callback writes and aliased real counter storage."""
    rows = []
    for remove, flag, mix in itertools.product((False, True), (0, 1, 2, 0x80, 0xff),
                                               (0, 1, 1 << 32, 1 << 63)):
        context = None if remove else SEPARATE_COUNTERS
        for original in (False, True):
            rows.append(case(f'gate-{int(remove)}-{flag:02x}-{mix:x}-{int(original)}',
                             counter=context, flag=flag, mix=mix, original=original))
    for remove, value in itertools.product((False, True),
            (0, 1, 0xff, 0x100, 0x7fffffff, 0x80000000, 0xffffffff, 1 << 32)):
        context = None if remove else SEPARATE_COUNTERS
        rows.append(case(f'return-{int(remove)}-{value:x}', counter=context, result=value,
                         nested_effects=[assign(REGISTERED, 0xcd, 1)]))
        rows.append(case(f'list-return-{int(remove)}-{value:x}', original=True,
                         counter=context, node=True, child=True, child_count=33, result=value,
                         nested_effects=[assign(REGISTERED, 0xde, 1)]))
    for remove in (False, True):
        context = None if remove else SEPARATE_COUNTERS
        for old, new in ((0, 1 << 32), (1, 0), (0xffffffffffffffff, 0)):
            rows.append(case(f'change-buffer-{int(remove)}-{old:x}-{new:x}', counter=context,
                             mix=old, add_effects=[assign(MIX, new, 8)]))
        for value in (0, 1, 0x100, 0xffff, 0x10000, 0x80000000, MASK):
            rows.append(case(f'change-count-{int(remove)}-{value:x}', counter=context,
                             add_effects=[assign(MIX_COUNT, value, 4)]))
        rows.append(case(f'change-registration-{int(remove)}', counter=context,
                         add_effects=[assign(REGISTERED, 0xf6, 1)],
                         second_effects=[assign(REGISTERED, 0x6d, 1)]))
        rows.append(case(f'empty-node-{int(remove)}', counter=context, original=True, node=True))
    for delta, field in itertools.product((0x10, 0xc0), (MIX, MIX_COUNT)):
        for seed in (0, (-0x988) & MASK, MASK, 1, 0x80000000):
            kwargs = {'mix': seed} if field == MIX else {'count': seed}
            rows.append(case(f'alias-{field:x}-{delta:x}-{seed:x}', original=True,
                             counter=field-delta, **kwargs))
    for delta in (0x10, 0xc0):
        rows.append(case(f'alias-registration-{delta:x}', original=True, counter=REGISTERED-delta))
        rows.append(case(f'alias-high-mix-{delta:x}', original=True, counter=MIX+4-delta,
                         mix=((-0x988) & MASK) << 32))
    for fill, offset in itertools.product((0, 0x5a, 0xff), (0, 1, ARENA_BYTES-0xc4)):
        rows.append(case(f'placement-{fill:02x}-{offset:x}', original=True,
                         fill=fill, counter=offset))
    return rows


def main():
    """Write one deterministic JSON input for each case."""
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rows = cases()
    assert len({row['id'] for row in rows}) == len(rows)
    for row in rows:
        (directory / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(rows)} synthetic output-counter cases')


if __name__ == '__main__':
    main()
