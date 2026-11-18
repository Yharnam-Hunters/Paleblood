#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic FMOD Thread entry protocols. Usage: SCRIPT OUT_DIR.

Original semaphore and millisecond-delay wrappers execute. Their kernel imports
are scripted, so no OS semaphore, sleep or thread runs. Scripted callbacks and
kernel boundaries change owned Thread storage between the observed loop gates.
These fixtures are synthetic inputs, not recordings or concurrency evidence.
"""
import json
from pathlib import Path
import struct
import sys


ADDRESS = '0x01118300'
VIRTUAL_A = '0x011183a0'
VIRTUAL_B = '0x0111b920'
ROUTINE_A = '0x0111b140'
ROUTINE_B = '0x0111b860'
THREAD_BYTES = 0x158
FIELDS = {'vtable': 0, 'native_handle': 0x110, 'running': 0x118,
          'argument': 0x120, 'wakeup': 0x130, 'startup': 0x138,
          'entry': 0x140, 'context': 0x148, 'delay': 0x150}
ROLES = {'thread': 0, 'startup-a': 0x200, 'startup-b': 0x240,
         'wakeup-a': 0x280, 'wakeup-b': 0x2c0,
         'argument-a': 0x380, 'argument-b': 0x3c0, 'context': 0x440}
FALLBACK_CALLS = 8


def number(field, value, width=1):
    """Describe an exact-width owned field write."""
    assert field in FIELDS and width in (1, 4, 8)
    return {'field': field, 'bytes': (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little').hex()}


def pointer(field, role):
    """Describe a buffer pointer, or null, written by a callback."""
    assert field in FIELDS
    return {'field': field, 'role': role}


def entry(field, address):
    """Describe a code-pointer field write at a scripted function boundary."""
    assert field == 'entry'
    return {'field': field, 'guest': address}


def case(cid, *, routine=True, wakeup='wakeup-a', startup='startup-a',
         argument='argument-a', vtable='vtable-a', delay=0, running=0x80,
         owner_offset=0x80, owner_bytes=0x800, fill=0xa5,
         initial_signal=(), final_signal=(), waits=None, sleeps=None,
         routine_a=None, routine_b=None, virtual_a=None, virtual_b=None,
         signal_results=(0, 0), wait_result=0, sleep_result=0,
         callback_result=0xffffffffffffffff):
    """Build one finite protocol with observing scripts for every boundary."""
    assert 0 <= owner_offset and owner_offset + THREAD_BYTES <= owner_bytes
    assert 0 <= fill <= 255 and 0 <= running <= 255 and 0 <= delay <= 0xffffffff

    def role(name):
        if name.startswith('vtable-'):
            assert name in ('vtable-a', 'vtable-b')
            return name + '+0'
        assert name in ROLES
        offset = owner_offset + ROLES[name]
        assert 0 <= offset < owner_bytes
        return f'owner+{offset}'

    def address(field):
        return f'buf:owner+{owner_offset + FIELDS[field]}'

    def payload(action):
        assert set(action) in ({'field', 'bytes'}, {'field', 'role'}, {'field', 'guest'})
        if 'bytes' in action:
            return {'bytes': action['bytes']}
        if 'guest' in action:
            return {'guest': action['guest']}
        return {'bytes': '00' * 8} if action['role'] is None else {'pointer': role(action['role'])}

    memory = [{'addr': address('vtable'), **({'bytes': '00' * 8} if vtable is None else {'pointer': role(vtable)})},
              {'addr': address('native_handle'), 'bytes': struct.pack('<Q', 0xfedcba9876543210).hex()},
              {'addr': address('running'), 'bytes': f'{running:02x}'},
              {'addr': address('argument'), **({'bytes': '00' * 8} if argument is None else {'pointer': role(argument)})},
              {'addr': address('wakeup'), **({'bytes': '00' * 8} if wakeup is None else {'pointer': role(wakeup)})},
              {'addr': address('startup'), **({'bytes': '00' * 8} if startup is None else {'pointer': role(startup)})},
              {'addr': address('entry'), **({'guest': ROUTINE_A} if routine else {'bytes': '00' * 8})},
              {'addr': address('context'), 'bytes': struct.pack('<Q', 0x8877665544332211).hex()},
              {'addr': address('delay'), 'bytes': struct.pack('<I', delay).hex()}]
    for table, slots in [('vtable-a', (VIRTUAL_A, VIRTUAL_B, VIRTUAL_B)),
                         ('vtable-b', (VIRTUAL_B, VIRTUAL_A, VIRTUAL_A))]:
        memory.extend({'addr': f'buf:{table}+{index * 8}', 'guest': target}
                      for index, target in enumerate(slots))

    def import_writes(actions):
        writes = []
        for action in actions:
            data = payload(action)
            # Fixed-location import writes are byte payloads. Pointer updates use
            # function callbacks; null field writes need no host-address constant.
            assert set(data) == {'bytes'}
            writes.append({'addr': address(action['field']), **data})
        return writes

    def kernel(name, actions, result, snapshot=False):
        count = 1 if name == 'sceKernelUsleep' else 3 if name == 'sceKernelWaitSema' else 2
        item = {'name': name, 'argc': count, 'ret': hex(result & 0xffffffffffffffff),
                'writes': import_writes(actions)}
        item.update({'argi': [], 'argu32': [0]} if name == 'sceKernelUsleep' else
                    {'argi': [0, 2], 'argu32': [1]} if name == 'sceKernelWaitSema' else
                    {'argi': [0], 'argu32': [1]})
        if snapshot and startup is not None:
            item['argmem'] = [{'arg': 0, 'offset': -ROLES[startup], 'size': THREAD_BYTES}]
        return item

    stop = [number('running', 0)]
    imports = [kernel('sceKernelSignalSema', initial_signal, signal_results[0], True),
               kernel('sceKernelSignalSema', final_signal, signal_results[1])]
    imports.extend(kernel('sceKernelSignalSema', stop, 0) for _ in range(FALLBACK_CALLS))
    for name, plans, default, result in [('sceKernelWaitSema', waits, [[]], wait_result),
                                        ('sceKernelUsleep', sleeps, [stop], sleep_result)]:
        imports.extend(kernel(name, actions, result) for actions in (default if plans is None else plans))
        imports.extend(kernel(name, stop, 0) for _ in range(FALLBACK_CALLS))

    def callback(target, plans, origin):
        default = [(origin, stop)] if origin is not None else [(0, [])]
        items = []
        for at, actions in (default if plans is None else plans):
            assert at in ROLES.values()
            writes = [{'arg': 0, 'offset': FIELDS[action['field']] - at, **payload(action)}
                      for action in actions]
            items.append({'address': target, 'argc': 1, 'argi': [0],
                          'ret': hex(callback_result & 0xffffffffffffffff), 'writes': writes})
        items.extend({'address': target, 'argc': 1, 'argi': [0], 'ret': '0x0'}
                     for _ in range(FALLBACK_CALLS))
        return items

    origin = None if argument is None else ROLES[argument]
    stubs = [*callback(ROUTINE_A, routine_a, origin), *callback(ROUTINE_B, routine_b, origin),
             *callback(VIRTUAL_A, virtual_a, 0), *callback(VIRTUAL_B, virtual_b, 0)]
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:' + role('thread')},
            'buffers': {'owner': {'size': owner_bytes, 'bytes': bytes([fill] * owner_bytes).hex()},
                        'vtable-a': {'size': 24, 'bytes': 'b6' * 24},
                        'vtable-b': {'size': 24, 'bytes': 'c7' * 24}},
            'memory': memory, 'imports': imports, 'stubs': stubs}


def cases():
    """Vary dispatch, helper results, field widths and changes between loop gates."""
    for routine in (False, True):
        kind = 'routine' if routine else 'virtual'
        for wakeup in (None, 'wakeup-a'):
            for delay in (0, 1, 16, 0x80000000, 0xffffffff):
                yield case(f'{kind}-wake-{int(wakeup is not None)}-delay-{delay:08x}',
                           routine=routine, wakeup=wakeup, delay=delay)
        for offset in (0, 8, 16, 32, 64, 128):
            for fill in (0, 0x5a, 0xa5, 0xff):
                yield case(f'{kind}-offset-{offset}-fill-{fill:02x}',
                           routine=routine, owner_offset=offset, fill=fill)
        for value in (0, 1, 2, 0x7f, 0x80, 0xff):
            yield case(f'{kind}-initial-running-{value:02x}', routine=routine, running=value)
        for startup in (None, 'startup-a'):
            for wakeup in (None, 'wakeup-a'):
                yield case(f'{kind}-initial-signal-stops-s{int(startup is not None)}-w{int(wakeup is not None)}',
                           routine=routine, startup=startup, wakeup=wakeup,
                           initial_signal=[number('running', 0)])
        for value in (0, 1, 0x80020001, 0x100000000, 0xffffffffffffffff):
            yield case(f'{kind}-wait-result-{value:016x}', routine=routine, wait_result=value)
            yield case(f'{kind}-wait-stops-result-{value:016x}', routine=routine,
                       wait_result=value, waits=[[number('running', 0)]])
            yield case(f'{kind}-initial-signal-result-{value:016x}', routine=routine,
                       signal_results=(value, 0))
            yield case(f'{kind}-final-signal-result-{value:016x}', routine=routine,
                       signal_results=(0, value))
            yield case(f'{kind}-sleep-result-{value:016x}', routine=routine,
                       delay=1, sleep_result=value)
        for value in (1, 2, 0x7f, 0x80, 0xff):
            plans = [(ROLES['argument-a'] if routine else 0, [number('running', value)]),
                     (ROLES['argument-a'] if routine else 0, [number('running', 0)])]
            yield case(f'{kind}-running-recheck-{value:02x}', routine=routine, waits=[[], []],
                       **({'routine_a': plans} if routine else {'virtual_a': plans}))
        for before, after in ((0, 1), (1, 0), (0, 0x80000000), (0xffffffff, 16)):
            plans = [(ROLES['argument-a'] if routine else 0,
                      [number('running', 0), number('delay', after, 4)])]
            yield case(f'{kind}-callback-delay-{before:08x}-to-{after:08x}', routine=routine,
                       delay=before, **({'routine_a': plans} if routine else {'virtual_a': plans}))
        for value in (0, 1, 0x8000000000000000, 0xffffffffffffffff):
            yield case(f'{kind}-ignored-callback-result-{value:016x}', routine=routine,
                       callback_result=value, delay=1)

    a = ROLES['argument-a']
    yield case('routine-null-argument', argument=None, routine_a=[(0, [])])
    yield case('routine-aliases-thread', argument='thread')
    yield case('routine-null-unused-vtable', vtable=None)
    yield case('virtual-current-vtable-b', routine=False, vtable='vtable-b')
    yield case('exact-thread-owned-object', owner_offset=0, owner_bytes=THREAD_BYTES,
               argument='thread', startup=None, wakeup=None)
    yield case('initial-signal-clears-entry', initial_signal=[pointer('entry', None)])
    yield case('initial-signal-clears-wakeup', initial_signal=[pointer('wakeup', None)])
    yield case('initial-signal-clears-argument', initial_signal=[pointer('argument', None)],
               routine_a=[(0, [])])
    yield case('wait-clears-entry', waits=[[pointer('entry', None)]])
    yield case('wait-clears-argument', waits=[[pointer('argument', None)]], routine_a=[(0, [])])
    yield case('wait-clears-startup', waits=[[pointer('startup', None)]])
    yield case('callback-changes-routine', waits=[[], []],
               routine_a=[(a, [entry('entry', ROUTINE_B)])])
    yield case('callback-changes-to-virtual', waits=[[], []],
               routine_a=[(a, [pointer('entry', None)])])
    yield case('virtual-changes-to-routine', routine=False, waits=[[], []],
               virtual_a=[(0, [entry('entry', ROUTINE_A)])])
    yield case('virtual-changes-vtable', routine=False, waits=[[], []],
               virtual_a=[(0, [pointer('vtable', 'vtable-b')])])
    yield case('callback-changes-argument', waits=[[], []],
               routine_a=[(a, [pointer('argument', 'argument-b')]), (ROLES['argument-b'], [])])
    yield case('callback-clears-wakeup',
               routine_a=[(a, [pointer('wakeup', None)]), (a, [number('running', 0)])])
    yield case('callback-changes-wakeup', routine_a=[(a, [pointer('wakeup', 'wakeup-b')])])
    yield case('callback-changes-startup',
               routine_a=[(a, [pointer('startup', 'startup-b'), number('running', 0)])])
    yield case('callback-clears-startup',
               routine_a=[(a, [pointer('startup', None), number('running', 0)])])
    yield case('sleep-revives-running', delay=1,
               sleeps=[[number('running', 2), number('delay', 0, 4)]])
    yield case('sleep-clears-startup', delay=1, sleeps=[[pointer('startup', None)]])
    yield case('routine-no-writes-wait-terminates', routine_a=[(a, [])])
    yield case('virtual-no-writes-wait-terminates', routine=False, virtual_a=[(0, [])])
    # A wrong wide loop must pass the later byte gate and reach its terminating callback.
    yield case('initial-signal-stops-with-wide-neighbor-control',
               initial_signal=[number('running', 0)], waits=[[number('running', 1)]],
               routine_a=[(a, [number('running', 0, 4)])])


def main():
    """Write deterministic dictionaries, checking complete names on regeneration."""
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    names = {row['id'] + '.json' for row in generated}
    assert len(generated) == len(names)
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (output / (row['id'] + '.json')).write_text(json.dumps(row, indent=1) + '\n')
    assert {p.name for p in output.glob('*.json')} == names
    print(f'{len(generated)} synthetic cases written')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
