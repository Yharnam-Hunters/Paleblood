#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD native-thread wrapper cases; no real threads or kernel calls.

Original string comparisons and affinity lookup execute. Imported API calls are scripted;
successful output APIs write complete opaque handles. Unknown names leave affinity
unspecified, so those cases omit only that undefined argument from observations.
"""
from pathlib import Path
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from harness import SANDBOX
from fmod_thread_lookup_cases import MASKS, NAMES, SLOTS, TABLE

ADDRESS = '0x0111a9f0'
APIS = [
    'scePthreadAttrInit', 'scePthreadAttrSetstacksize', 'scePthreadAttrSetdetachstate',
    'scePthreadAttrSetschedpolicy', 'scePthreadAttrSetschedparam', 'scePthreadAttrSetaffinity',
    'scePthreadCreate', 'scePthreadAttrDestroy', 'scePthreadSetprio',
]
ATTR_HANDLE = SANDBOX  # a_attributes is the first canonical owned buffer.
THREAD_HANDLE = 0x123456789abcdef0
NAME_BYTES = 96
NONBLOCKING_PREFIX_BYTES = 23


def word(value, width=8):
    """Encode fixed low-width callback data, including complete opaque handles."""
    return (value & ((1 << (8 * width)) - 1)).to_bytes(width, 'little').hex()


def affinity_slot(name):
    """Use the original exact matches and its nonblocking prefix contract."""
    visible = name.split(b'\0', 1)[0]
    for index, known in enumerate(NAMES):
        if SLOTS[index] == 'nonblocking':
            matches = visible[:NONBLOCKING_PREFIX_BYTES] == known
        else:
            matches = visible == known
        if matches:
            return index
    return None


def case(cid, name=NAMES[1], priority=0, stack_bytes=0x10000, *, failure=None, result=1,
         attr_handle=ATTR_HANDLE, thread_handle=THREAD_HANDLE, masks=None,
         output='buf:out+0x8', entry='guest:0x01118300', argument='buf:argument+0x8',
         unused=0xfedcba9876543210, writes_on_error=True, priority_write=None,
         attr_rewrite=None, name_rewrite=None, api_output_write=None):
    """Write independent API scripts; the wrapper must gate each later operation."""
    raw = name + b'\0'
    assert len(raw) <= NAME_BYTES
    slot = affinity_slot(name)
    operations = []
    for api in APIS:
        argc = 1 if api in [APIS[0], APIS[7]] else 5 if api == APIS[6] else 2
        operation = {'name': api, 'argc': argc, 'ret': hex(result & ((1 << 64) - 1)) if failure == api else '0x0'}
        if api in [APIS[2], APIS[3], APIS[8]]:
            operation.update(argi=[0], argu32=[1])
        elif api == APIS[5] and slot is None:
            operation['argi'] = [0]
        else:
            operation['argi'] = list(range(argc))
        snapshots = []
        if api not in [APIS[0], APIS[8]]:
            snapshots.append({'arg': 1 if api == APIS[6] else 0, 'size': 8})
        if api == APIS[4]:
            snapshots.append({'arg': 1, 'size': 4})
        if api == APIS[6]:
            snapshots.append({'arg': 4, 'size': 32})
        if snapshots:
            operation['argmem'] = snapshots
        writes = []
        if api in [APIS[0], APIS[6]] and (failure != api or writes_on_error or result & 0xffffffff == 0):
            writes.append({'arg': 0, 'bytes': word(attr_handle if api == APIS[0] else thread_handle)})
        if api == APIS[4] and priority_write is not None:
            writes.append({'arg': 1, 'bytes': word(priority_write, 4)})
        if attr_rewrite and api == attr_rewrite[0]:
            assert api not in [APIS[0], APIS[6], APIS[8]]
            writes.append({'arg': 0, 'bytes': word(attr_rewrite[1])})
        if name_rewrite and api == name_rewrite[0]:
            payload = name_rewrite[1] + b'\0'
            assert len(payload) <= NAME_BYTES
            writes.append({'addr': 'buf:name+0', 'bytes': payload.hex()})
        if api_output_write and api == api_output_write[0]:
            writes.append({'addr': 'buf:out+8', 'bytes': word(api_output_write[1])})
        if writes:
            operation['writes'] = writes
        operations.append(operation)
    return {
        'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i32',
        'args': {'rdi': 'buf:name', 'rsi': entry, 'rdx': argument,
                 'rcx': hex(priority & ((1 << 64) - 1)), 'r8': hex(unused & ((1 << 64) - 1)),
                 'r9': hex(stack_bytes & ((1 << 64) - 1))},
        'stack': [output],
        'buffers': {'a_attributes': {'size': 32, 'bytes': 'a5' * 32},
                    'argument': {'size': 32, 'bytes': '6b' * 32},
                    'name': {'size': NAME_BYTES, 'bytes': (raw + b'\xa5' * (NAME_BYTES - len(raw))).hex()},
                    'out': {'size': 32, 'bytes': '5a' * 32}},
        'memory': [{'addr': hex(TABLE), 'bytes': struct.pack('<8Q', *(MASKS if masks is None else masks)).hex()}],
        'imports': operations,
    }


def cases():
    """Cover each priority target, API gate, stack boundary and publication path."""
    for priority in range(-2, 4):
        for label, name in [('ordinary', NAMES[1]), ('mixer', NAMES[0])]:
            yield case(f'priority-{priority}-{label}', name=name, priority=priority)
    for value in [-0x80000000, -3, 4, 0x7fffffff, 0x100000000, 0x100000003,
                  0xffffffff00000000, 0xfffffffffffffffe]:
        for label, name in [('ordinary', NAMES[1]), ('mixer', NAMES[0])]:
            yield case(f'priority-bits-{value & ((1 << 64) - 1):016x}-{label}', name=name, priority=value)
    for value in [-0x80000000, -1, 0, 1, 0x3ffe, 0x3fff, 0x4000, 0x4001, 0x4002,
                  0x10000, 0x7fffffff, 0x80000000, 0xffffffff, 0x100004000,
                  0x100004001, 0xffffffff00004001]:
        yield case(f'stack-bits-{value & ((1 << 64) - 1):016x}', stack_bytes=value)
    for slot, name in enumerate(NAMES):
        for mask in [0, 1, 0xffffffff, 0x100000000, 0x8000000000000000, 0xffffffffffffffff]:
            values = list(MASKS); values[slot] = mask
            yield case(f'affinity-slot-{slot}-mask-{mask:016x}', name=name, masks=values)
    for index, name in enumerate([b'', b'FMOD', b'unknown thread', b'fmod mixer thread',
                                  NAMES[0] + b' extra', NAMES[3] + b' suffix',
                                  NAMES[3][:-1], NAMES[3][:22] + b'X',
                                  NAMES[0] + b'\0suffix', b'\xff thread']):
        yield case(f'name-boundary-{index}', name=name, priority=2)
    for api in APIS:
        for value in [1, 2, 0x80020016, 0x80000000, 0xffffffff, 0x100000000,
                      0xffffffff00000000, 0x1234567800000001]:
            yield case(f'api-{api}-result-{value:016x}', failure=api, result=value)
        yield case(f'api-{api}-writes-public-output-before-error', failure=api, result=1,
                   api_output_write=(api, 0xfedcba9876543210))
        for output in ['0x0', '0xffffffff00000000']:
            yield case(f'api-{api}-invalid-output-{output}', failure=api, result=1, output=output)
    for api in [APIS[0], APIS[6]]:
        yield case(f'api-{api}-error-without-local-write', failure=api, writes_on_error=False)
    for value in [0, 1, 0x89abcdef, 0x100000000, 0x8000000000000000, 0xffffffffffffffff]:
        yield case(f'attribute-handle-{value:016x}', attr_handle=value)
        yield case(f'thread-handle-{value:016x}', thread_handle=value)
    for api in [APIS[1], APIS[2], APIS[3], APIS[4], APIS[5], APIS[7]]:
        yield case(f'attribute-rewrite-{api}', attr_rewrite=(api, 0xfedcba9876543210))
    for value in [0, 1, 257, 767, 0x80000000, 0xffffffff]:
        for label, name in [('ordinary', NAMES[1]), ('mixer', NAMES[0])]:
            yield case(f'scheduling-parameter-write-{value:08x}-{label}', name=name, priority_write=value)
    for api in [APIS[0], APIS[4], APIS[6]]:
        yield case(f'name-changed-{api}-from-mixer', name=NAMES[0], name_rewrite=(api, NAMES[1]))
        yield case(f'name-changed-{api}-to-mixer', name=NAMES[1], name_rewrite=(api, NAMES[0]))
    for offset in [0, 1, 7, 8, 9, 16, 24]:
        yield case(f'output-offset-{offset}', output=f'buf:out+{offset}')
    for offset in [0, 8, 64, 80, 88]:
        yield case(f'output-aliases-name-{offset}', output=f'buf:name+{offset}')
    for value in [0, 1, 0x100000000, 0x8000000000000000, 0xffffffffffffffff]:
        yield case(f'unused-argument-{value:016x}', unused=value)
    for entry in ['0x0', '0x1', '0xffffffff00000000', 'guest:0x0111b140']:
        yield case('entry-carrier-' + entry.replace(':', '-'), entry=entry)
    for argument in ['0x0', 'buf:out+0x8', 'buf:name', '0x8000000000000001']:
        yield case('argument-carrier-' + argument.replace(':', '-').replace('+', '-'), argument=argument)


def main():
    """Generate deterministic dictionaries and reject stale/duplicate case IDs."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    names = {row['id'] + '.json' for row in generated}
    assert len(names) == len(generated)
    output = Path(sys.argv[1]); output.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (output / (row['id'] + '.json')).write_text(json.dumps(row, indent=1) + '\n')
    assert {p.name for p in output.glob('*.json')} == names
    print(f'{len(generated)} synthetic cases written')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
