#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for what a transfer task's initialization calls
(game/loading/transfer_task_callees.cpp), for tools/verify.py run.

usage: transfer_task_callees_cases.py OUT_DIR instance|locate|assign|release

instance (0x0246be70): the singleton's statics are laid out in the image: the instance pointer,
the "destroyed" byte, the storage's static guard, and the light mutex beside them (its storage,
"constructed" byte and guard). An already built light mutex gets a fake vtable whose lock and
unlock slots are scripted stubs; when the light mutex is built, the system mutex creation
(0x02483fe0, a stub) compares the mutex as it is then (its real vtable) and gives it that fake
vtable. The C++ static guard's acquire and release are scripted imports, the device constructor
(0x0246bff0, compared with the aligned storage) and the quit query's creation (0x024e2550) are
stubs, and the quit query is a buffer whose vtable's second slot is a scripted stub. Cases cover
the instance already there, built with each guard state, with the destroyed byte 1 or another
value, with the quit query missing, and an instance that appears while the mutex is taken.

locate (0x0246e3d0): a device buffer (0x100 bytes of a pattern) whose lock at +0xe0 has a fake
vtable with scripted lock and unlock slots, and a path text buffer. The path copy heap
(0x05940668) is set, or null and found through a scripted 0x0247bbf0; its info slot writes the
flags byte (an invalid heap is reported through a stubbed 0x024b55b0). wcslen is a scripted import
and the counted string assign (0x02a2a720) a stub that may give the copy a heap buffer (then
freed through the heap's slot 0x70). The alias and root resolutions (0x0246d180, 0x0246cf50) are
scripted as sequences of results, the named device lookup (0x0246e630) is a stub, and fake file
devices answer through a scripted vtable slot 0x20; unnamed devices come from the device list
(+0x08) and are skipped when registered under a name (+0xb0).

assign (0x024be630): a target and a source request (0x148 bytes of two patterns, three synthetic
wide strings each with its own allocator, a fake object whose vtable slot 0x10 is a scripted stub),
or one buffer for an assignment to itself. The string assign (0x02a279a0) and the array assign
(0x02b4d100) are scripted stubs.

release (0x024712e0): the instance pointer (a device buffer or null) and the "destroyed" byte (0,
1, 2, 0x80, 0xff) with the storage's bytes after it; the device's destroy (0x0246caa0) is a
scripted stub with a spare call.
"""
import json
import os
import struct
import sys

ADDRESSES = {'instance': '0x0246be70', 'locate': '0x0246e3d0', 'assign': '0x024be630', 'release': '0x024712e0'}
RETURNS = {'instance': 'i64', 'locate': 'i32', 'assign': 'i64', 'release': 'void'}
DEVICE_DESTROY = '0x0246caa0'

# instance
INSTANCE, DEVICE_STATE, DEVICE_STORAGE, STORAGE_GUARD = '0x05aa1ab0', '0x05aa1ab8', '0x05aa1ac0', '0x05aa1bc0'
LIGHT_MUTEX, LIGHT_CONSTRUCTED, LIGHT_GUARD = '0x05aa5720', '0x05aa573f', '0x05aa5740'
CONSTRUCT, RELEASE_AT_QUIT = '0x0246bff0', '0x024712e0'
QUIT_QUERY, QUIT_QUERY_CREATE = '0x05aa54f8', '0x024e2550'
SYSTEM_MUTEX_CREATE = '0x02483fe0'
LIGHT_LOCK, LIGHT_UNLOCK, ADD_QUIT_FUNCTION = '0x00500400', '0x00500440', '0x00500480'
LIGHT_MUTEX_SIZE, LIGHT_HANDLE = 0x18, 0x8
DEVICE_SIZE = 0x100
INSTANCE_FROM_LIGHT_MUTEX = int(INSTANCE, 16) - int(LIGHT_MUTEX, 16)

# locate
LOCK, UNLOCK, INFO, FREE, LOCATE = '0x00500200', '0x00500240', '0x005002c0', '0x00500300', '0x00500340'
FATAL_ERROR, ASSIGN_COUNTED = '0x024b55b0', '0x02a2a720'
RESOLVE_ROOT, RESOLVE_ALIAS, FIND = '0x0246cf50', '0x0246d180', '0x0246e630'
COPY_HEAP, COPY_HEAP_OWNER, COPY_HEAP_OFFSET = '0x05940668', '0x0247bbf0', 0x458
ENTRY, LOCK_OFFSET, DISABLED = 0x48, 0xe0, 0xf8
VALID = 0x20
NOT_FOUND = 2

# assign
STRING_ASSIGN, ARRAY_ASSIGN, ALLOCATOR_QUERY = '0x02a279a0', '0x02b4d100', '0x00500500'
REQUEST = 0x148
STRINGS = (0x48, 0xa0, 0xd8)
NPOS = 0xffffffffffffffff


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def stack_string(arg):
    """What is compared of a string on the stack: its first character and the fields from the
    size on (the first word and the rest of the inline text are left as the stack had them)."""
    return [{'arg': arg, 'offset': 8, 'size': 2}, {'arg': arg, 'offset': 0x18, 'size': 0x19}]


class Case:
    def __init__(self, mode, cid):
        self.mode = mode
        self.cid = cid
        self.raw = {}
        self.memory = []
        self.imports = []
        self.stubs = []
        self.args = {}

    def json(self):
        return {'schema': 1, 'address': ADDRESSES[self.mode], 'id': self.cid, 'returns': RETURNS[self.mode],
                'args': self.args,
                'buffers': {k: {'size': len(v), 'bytes': bytes(v).hex()} for k, v in self.raw.items()},
                'memory': self.memory, 'imports': self.imports, 'stubs': self.stubs}


# instance (0x0246be70)

def instance_case(cid, existing=False, state=0, constructed=1, light_guard=1, storage_guard=1,
                  light_acquire=None, storage_acquire=None, create_result=0, query=True, appears=False):
    """The guards' first byte, and what acquire returns when it is clear (None: not called)."""
    c = Case('instance', cid)
    c.raw['device'] = bytearray([0x5a]) * DEVICE_SIZE
    c.raw['mutex_vtable'] = bytearray(0x30)
    c.memory += [{'addr': 'buf:mutex_vtable+24', 'guest': LIGHT_LOCK},
                 {'addr': 'buf:mutex_vtable+40', 'guest': LIGHT_UNLOCK},
                 {'addr': DEVICE_STATE, 'bytes': f'{state:02x}'},
                 {'addr': DEVICE_STORAGE, 'bytes': 'c3' * DEVICE_SIZE},
                 {'addr': STORAGE_GUARD, 'bytes': f'{storage_guard:02x}' + '00' * 7},
                 {'addr': LIGHT_CONSTRUCTED, 'bytes': f'{constructed:02x}'},
                 {'addr': LIGHT_GUARD, 'bytes': f'{light_guard:02x}' + '00' * 7}]
    if existing:
        c.memory.append({'addr': INSTANCE, 'pointer': 'device+0'})
        return c.json()
    c.memory.append({'addr': INSTANCE, 'bytes': u64(0)})
    # A built light mutex: the fake vtable, a handle and its "created" flag.
    c.memory.append({'addr': LIGHT_MUTEX, 'bytes': '00' * 8 + '77' * 8 + '01' + 'a5' * 7})
    c.memory.append({'addr': LIGHT_MUTEX, 'pointer': 'mutex_vtable+0'})
    if light_acquire is not None:
        c.imports.append({'name': '__cxa_guard_acquire', 'argc': 1, 'ret': light_acquire})
        if light_acquire:
            c.imports.append({'name': '__cxa_guard_release', 'argc': 1})
    if not constructed or light_acquire:
        # The mutex as the code built it (its real vtable, "created" clear); then the fake vtable.
        c.stubs.append({'address': SYSTEM_MUTEX_CREATE, 'argc': 1, 'ret': create_result,
                        'argmem': [{'arg': 0, 'offset': -LIGHT_HANDLE, 'size': LIGHT_MUTEX_SIZE}],
                        'writes': [{'arg': 0, 'offset': -LIGHT_HANDLE, 'pointer': 'mutex_vtable+0'}]})
    lock = {'address': LIGHT_LOCK, 'argc': 1, 'argu32': [1]}
    if appears:
        lock['writes'] = [{'arg': 0, 'offset': INSTANCE_FROM_LIGHT_MUTEX, 'pointer': 'device+0'}]
    c.stubs.append(lock)
    if not appears:
        if storage_acquire is not None:
            c.imports.append({'name': '__cxa_guard_acquire', 'argc': 1, 'ret': storage_acquire})
            if storage_acquire:
                c.imports.append({'name': '__cxa_guard_release', 'argc': 1})
        c.stubs.append({'address': CONSTRUCT, 'argc': 1})
        c.raw['query'] = bytearray(8)
        c.raw['query_vtable'] = bytearray(0x10)
        c.memory += [{'addr': 'buf:query+0', 'pointer': 'query_vtable+0'},
                     {'addr': 'buf:query_vtable+8', 'guest': ADD_QUIT_FUNCTION}]
        if query:
            c.memory.append({'addr': QUIT_QUERY, 'pointer': 'query+0'})
        else:
            c.memory.append({'addr': QUIT_QUERY, 'bytes': u64(0)})
            c.stubs.append({'address': QUIT_QUERY_CREATE, 'argc': 0, 'ret': 'buf:query'})
        c.stubs.append({'address': ADD_QUIT_FUNCTION, 'argc': 2, 'argu32': [2]})
    c.stubs.append({'address': LIGHT_UNLOCK, 'argc': 1})
    return c.json()


def instance_cases():
    return [
        instance_case('existing', existing=True),
        instance_case('existing_light_mutex_unbuilt', existing=True, constructed=0, light_guard=0),
        instance_case('build'),
        instance_case('build_after_release', state=1),
        instance_case('destroyed_byte_other_value', state=2),
        instance_case('destroyed_byte_high', state=0x81),
        instance_case('quit_query_made', query=False),
        instance_case('quit_query_made_after_release', query=False, state=1),
        instance_case('storage_guard_acquired', storage_guard=0, storage_acquire=1),
        instance_case('storage_guard_not_acquired', storage_guard=0, storage_acquire=0),
        instance_case('light_mutex_built', constructed=0),
        instance_case('light_mutex_creation_fails', constructed=0, create_result=-1),
        instance_case('light_guard_acquired_clears_constructed', light_guard=0, light_acquire=1),
        instance_case('light_guard_not_acquired', light_guard=0, light_acquire=0),
        instance_case('all_guards_first_time', constructed=0, light_guard=0, storage_guard=0, light_acquire=1,
                      storage_acquire=1, query=False, state=1),
        instance_case('instance_appears_meanwhile', appears=True),
        instance_case('instance_appears_light_mutex_built', appears=True, constructed=0),
    ]


# locate (0x0246e3d0)

class LocateCase(Case):
    def __init__(self, cid, fill=0x6b):
        super().__init__('locate', cid)
        self.raw['device'] = bytearray([fill]) * 0x100
        self.raw['lock_vtable'] = bytearray(0x30)
        self.raw['heap_vtable'] = bytearray(0x78)
        self.memory += [{'addr': f'buf:device+{LOCK_OFFSET}', 'pointer': 'lock_vtable+0'},
                        {'addr': 'buf:lock_vtable+24', 'guest': LOCK},
                        {'addr': 'buf:lock_vtable+40', 'guest': UNLOCK},
                        {'addr': 'buf:heap_vtable+32', 'guest': INFO},
                        {'addr': 'buf:heap_vtable+112', 'guest': FREE}]

    def lock(self):
        self.stubs.append({'address': LOCK, 'argc': 1, 'argu32': [1]})

    def unlock(self):
        self.stubs.append({'address': UNLOCK, 'argc': 1})

    def file_device(self, name):
        if 'locate_vtable' not in self.raw:
            self.raw['locate_vtable'] = bytearray(0x28)
            self.memory.append({'addr': 'buf:locate_vtable+32', 'guest': LOCATE})
        self.raw[name] = bytearray(0x10)
        self.memory.append({'addr': f'buf:{name}+0', 'pointer': 'locate_vtable+0'})

    def ask(self, result, preferred):
        self.stubs.append({'address': LOCATE, 'argc': 3, 'argmem': [{'arg': 1, 'size': 2}], 'ret': result})
        return result


def locate_case(cid, path='data:/x', preferred=1, disabled=0, aliases=(1, 0), roots=(0,), named=True,
                named_result=0, devices=0, named_flags=(), results=(), present=True, flags=VALID,
                long_copy=False, fill=0x6b):
    """aliases/roots: the scripted resolution results in call order (0 once they run out); devices:
    how many in the device list, named_flags which of them are registered under a name, results
    what each unnamed device asked returns, in order."""
    c = LocateCase(cid, fill)
    c.raw['device'][DISABLED] = disabled
    # A non-empty alias table: an alias resolution the case does not script would run the
    # original over it, with library calls the case does not script either.
    c.raw['alias_decoy'] = bytearray(0x70)
    c.memory += [{'addr': 'buf:device+136', 'pointer': 'alias_decoy+0'},
                 {'addr': 'buf:device+144', 'pointer': 'alias_decoy+112'}]
    c.raw['path'] = bytearray((path + '\0').encode('utf-16-le'))
    c.args = {'rdi': 'buf:device', 'rsi': 'buf:path', 'rdx': preferred}
    for i in range(devices):
        c.file_device(f'dev{i}')
    c.raw['list'] = bytearray(8 * max(devices, 1))
    for i in range(devices):
        c.memory.append({'addr': f'buf:list+{8 * i}', 'pointer': f'dev{i}+0'})
    c.memory += [{'addr': 'buf:device+8', 'pointer': 'list+0'},
                 {'addr': 'buf:device+16', 'pointer': f'list+{8 * devices}'}]
    entries = [f'dev{i}' for i in range(devices) if i in named_flags] + ['decoy']
    c.raw['decoy'] = bytearray(8)
    c.raw['named'] = bytearray(ENTRY * len(entries))
    for i, device in enumerate(entries):
        c.memory.append({'addr': f'buf:named+{ENTRY * i + 0x38}', 'pointer': f'{device}+0'})
    c.memory += [{'addr': 'buf:device+176', 'pointer': 'named+0'},
                 {'addr': 'buf:device+184', 'pointer': f'named+{ENTRY * len(entries)}'}]

    c.lock()
    if present:
        c.raw['copy_heap'] = bytearray(0x30)
        c.memory += [{'addr': 'buf:copy_heap+0', 'pointer': 'heap_vtable+0'},
                     {'addr': COPY_HEAP, 'pointer': 'copy_heap+0'}]
    else:
        c.raw['owner'] = bytearray(COPY_HEAP_OFFSET + 0x10)
        c.memory += [{'addr': COPY_HEAP, 'bytes': u64(0)},
                     {'addr': f'buf:owner+{COPY_HEAP_OFFSET}', 'pointer': 'heap_vtable+0'}]
        c.stubs.append({'address': COPY_HEAP_OWNER, 'argc': 0, 'ret': 'buf:owner'})
    c.stubs.append({'address': INFO, 'argc': 3, 'argi': [1, 2],
                    'writes': [{'arg': 0, 'offset': 0, 'bytes': f'{flags:02x}'}]})
    if not flags & VALID:
        c.stubs.append({'address': FATAL_ERROR, 'argc': 3})
    if path:
        c.imports.append({'name': 'wcslen', 'argc': 1, 'ret': len(path)})
    assign = {'address': ASSIGN_COUNTED, 'argc': 3, 'argmem': stack_string(0)}
    if long_copy:
        c.raw['copy_text'] = bytearray((path + '\0').encode('utf-16-le'))
        assign['writes'] = [{'arg': 0, 'offset': 8, 'pointer': 'copy_text+0'},
                            {'arg': 0, 'offset': 0x18, 'bytes': u64(len(path))},
                            {'arg': 0, 'offset': 0x20, 'bytes': u64(len(path) + 3)}]
    c.stubs.append(assign)

    aliases, roots = list(aliases), list(roots)
    resolve = {'argc': 2, 'argmem': stack_string(1)}
    if disabled or not preferred:
        while True:
            r = roots.pop(0) if roots else 0
            c.stubs.append({'address': RESOLVE_ROOT, 'ret': r, **resolve})
            if not r:
                break
    else:
        while True:
            aliased = False
            while True:
                r = aliases.pop(0) if aliases else 0
                c.stubs.append({'address': RESOLVE_ALIAS, 'ret': r, **resolve})
                if not r:
                    break
                aliased = True
            r = roots.pop(0) if roots else 0
            c.stubs.append({'address': RESOLVE_ROOT, 'ret': r, **resolve})
            if not aliased and not r:
                break

    c.lock()
    c.stubs.append({'address': FIND, 'argc': 2, 'argmem': stack_string(1), 'ret': 'buf:found' if named else 0})
    if named:
        c.file_device('found')
        c.ask(named_result, preferred)
    c.unlock()
    if not named:
        tried = 0
        for i in range(devices):
            if i in named_flags:
                continue
            r = c.ask(results[tried], preferred)
            tried += 1
            if r != NOT_FOUND:
                break
    if long_copy:
        c.stubs.append({'address': FREE, 'argc': 2})
    c.unlock()
    return c.json()


def locate_cases():
    return [
        locate_case('named_first_reader'),
        locate_case('named_second_reader', named_result=1),
        locate_case('named_not_found', named_result=NOT_FOUND),
        locate_case('named_not_found_devices_left', named_result=NOT_FOUND, devices=2),
        locate_case('named_other_code', named_result=-7),
        locate_case('not_preferred', preferred=0, roots=(1, 0)),
        locate_case('preferred_byte_2', preferred=2, named_result=1),
        locate_case('disabled_roots_only', disabled=1, roots=(1, 1, 0)),
        locate_case('disabled_other_value', disabled=0x80, roots=(0,)),
        locate_case('disabled_not_preferred', disabled=1, preferred=0, roots=(0,)),
        locate_case('no_alias_round', aliases=(0,), roots=(0,)),
        locate_case('root_repeats', aliases=(0, 0, 0), roots=(1, 1, 0)),
        locate_case('alias_chain', aliases=(1, 1, 1, 0), roots=(0,)),
        locate_case('alias_second_round', aliases=(0, 1, 0, 0), roots=(1, 0, 0)),
        locate_case('alias_then_root_again', aliases=(1, 0, 0), roots=(1, 0)),
        locate_case('unnamed_no_devices', named=False),
        locate_case('unnamed_first_finds', named=False, devices=3, results=(0,)),
        locate_case('unnamed_second_finds', named=False, devices=3, results=(NOT_FOUND, 1)),
        locate_case('unnamed_none_finds', named=False, devices=3, results=(NOT_FOUND,) * 3),
        locate_case('unnamed_other_code', named=False, devices=2, results=(-1,)),
        locate_case('unnamed_named_skipped', named=False, devices=3, named_flags=(0,), results=(1,)),
        locate_case('unnamed_all_named', named=False, devices=2, named_flags=(0, 1)),
        locate_case('unnamed_named_between', named=False, devices=4, named_flags=(1, 2),
                    results=(NOT_FOUND, 0)),
        locate_case('unnamed_not_preferred', named=False, preferred=0, devices=2, results=(NOT_FOUND, 1)),
        locate_case('empty_path', path='', aliases=(0,), roots=(0,)),
        locate_case('heap_found_on_first_use', present=False),
        locate_case('heap_not_valid', flags=0xdf),
        locate_case('heap_other_flags', flags=0xff, named=False, devices=1, results=(1,)),
        locate_case('long_copy_freed', path='data:/param/x.bin', long_copy=True),
        locate_case('long_copy_unnamed', path='data:/param/x.bin', long_copy=True, named=False, devices=2,
                    results=(NOT_FOUND, NOT_FOUND)),
        locate_case('zero_fill', fill=0, named_result=1),
    ]


# assign (0x024be630)

def put(raw, offset, data):
    raw[offset:offset + len(data)] = data


def request_strings(c, name, fill, texts, owns):
    raw = bytearray((fill + i) & 0xff for i in range(REQUEST))
    for i, (offset, text, own) in enumerate(zip(STRINGS, texts, owns)):
        put(raw, offset + 0x18, struct.pack('<QQ', len(text), 7))
        put(raw, offset + 8, (text + '\0').encode('utf-16-le'))
        raw[offset + 0x30] = own
        allocator = f'{name}_alloc{i}'
        c.raw[allocator] = bytearray(8)
        c.memory += [{'addr': f'buf:{name}+{offset + 0x28}', 'pointer': f'{allocator}+0'},
                     {'addr': f'buf:{allocator}+0', 'pointer': 'query_vtable+0'}]
    c.raw[name] = raw


def assign_case(cid, texts=('c0000', 'a', ''), owns=(1, 0, 1), target_owns=(0, 1, 0), same=False,
                fill=0xa5, source_fill=0x3c):
    c = Case('assign', cid)
    c.raw['query_vtable'] = bytearray(0x18)
    c.memory.append({'addr': 'buf:query_vtable+16', 'guest': ALLOCATOR_QUERY})
    request_strings(c, 'source', source_fill, texts, owns)
    target = 'source'
    if not same:
        target = 'target'
        request_strings(c, 'target', fill, ('zz', 'yyyyyyy', 'x'), target_owns)
    c.args = {'rdi': f'buf:{target}', 'rsi': 'buf:source'}
    if not same:
        for i in range(3):
            c.stubs += [{'address': ALLOCATOR_QUERY, 'argc': 1, 'ret': 0x70 + i},
                        {'address': ALLOCATOR_QUERY, 'argc': 1, 'ret': 0x80 + i},
                        {'address': STRING_ASSIGN, 'argc': 4, 'argmem': [{'arg': 0, 'size': 0x38}],
                         'ret': f'buf:target'}]
    c.stubs.append({'address': ARRAY_ASSIGN, 'argc': 2, 'ret': f'buf:{target}'})
    return c.json()


def assign_cases():
    return [
        assign_case('short_strings'),
        assign_case('empty_strings', texts=('', '', ''), owns=(0, 0, 0)),
        assign_case('full_inline', texts=('1234567', '7654321', 'abcdefg'), owns=(1, 1, 1), fill=0),
        assign_case('onto_itself', same=True),
        assign_case('onto_itself_zero_fill', same=True, source_fill=0, owns=(0, 0, 0)),
        assign_case('owns_bytes_other', owns=(2, 0xff, 0x80), target_owns=(0x11, 0x22, 0x33)),
        assign_case('owns_same_as_target', owns=(0, 1, 0)),
        assign_case('ones_source', source_fill=0xff, fill=0x11),
        assign_case('zero_source', source_fill=0, fill=0xff),
    ]


# release (0x024712e0)

def release_case(cid, state=0, instance=True):
    c = Case('release', cid)
    c.raw['device'] = bytearray([0x5a]) * DEVICE_SIZE
    c.memory += [{'addr': INSTANCE, 'pointer': 'device+0'} if instance else {'addr': INSTANCE, 'bytes': u64(0)},
                 {'addr': DEVICE_STATE, 'bytes': f'{state:02x}' + 'c3' * 7}]
    if state == 0:
        c.stubs.append({'address': DEVICE_DESTROY, 'argc': 1})
    c.stubs.append({'address': DEVICE_DESTROY, 'argc': 1})
    return c.json()


def release_cases():
    return [release_case('release'), release_case('release_null_instance', instance=False),
            release_case('already_released', state=1), release_case('state_two', state=2),
            release_case('state_high_bit', state=0x80), release_case('state_all_ones', state=0xff, instance=False)]


GENERATORS = {'instance': instance_cases, 'locate': locate_cases, 'assign': assign_cases, 'release': release_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in GENERATORS:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    generated = GENERATORS[sys.argv[2]]()
    for c in generated:
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(generated)} cases in {out}')


if __name__ == '__main__':
    main()
