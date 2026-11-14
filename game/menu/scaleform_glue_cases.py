#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/menu/scaleform_glue.cpp, for tools/verify.py run.

usage: scaleform_glue_cases.py OUT_DIR 0x023411d0|0x02341330|0x023548d0|0x02354d90|0x02360d80|
    0x02363c00|0x02363d20|0x02387050|0x02387650|0x0238a210|0x013a2a00|0x013a33c0|0x01401720|0x0233ef00

Every case lays out invented objects in buffers (filled with a pattern, so a field read by
mistake shows), invented singletons and fake vtables whose slots name scripted stubs. The
originals the functions call (the memory copy, DLNew's heap lookup, the fatal error report, the
condition signal's set, the Scaleform and loader calls) are scripted stubs too. DLNew's release
reads a thread pointer (`tls`, the pointer itself at gs:[0]) and the per-thread state at the
offset its global holds. The registry, handle and queue cases also give every callee a spare
scripted call, so a call that should not happen is recorded rather than run unseen. No game data
is used.
"""
import json
import os
import struct
import sys

FATAL_ERROR, HEAP_OWNING, MEMCPY = '0x024b55b0', '0x0247b720', '0x02a1a1b0'
NEW_STATE_OFFSET_GLOBAL = '0x057e44a8'
SPRJ_SCALEFORM, MENU_STATE, TEX_REPOSITORY = '0x05940368', '0x05962878', '0x05940358'
PLAYER_FLUSH, PLAYER_STEP, CLOSE_MOVIE, QUEUE = '0x02360860', '0x0235d0e0', '0x02357a20', '0x0233ef00'
TEX_FIND, TEX_ADD = '0x0232fc50', '0x0232ff90'
SIGNAL_SET = '0x0247e780'
LOADER_FINISH, BUILD = '0x0238a2c0', '0x02bd5620'
REGISTRY, REGISTRY_FIND, REGISTRY_RELEASE, HANDLE_RELEASE = '0x058b3570', '0x013a2a00', '0x013a33c0', '0x01401720'
TABLE_FIND, TABLE_RELEASE = '0x013abad0', '0x013abd30'
PENDING_JOB, SCALEFORM_LOCK, PLAYER_QUEUE = '0x059941f8', '0x02336d40', '0x0235b140'
# Fake vtable targets (any address: the case stubs them).
ALLOCATE, FREE, DESTROY, MAKE_TEXTURE = '0x00500100', '0x00500200', '0x00500300', '0x00500400'
OWNER_RELEASE, JOB_WAIT = '0x00500500', '0x00500600'

THREAD = 0x100           # the thread pointer's offset in `tls`
STATE_OFFSET = -0x60     # DLNew's per-thread state's offset from the thread pointer
RELEASE_HEAP = 0x10      # its release heap field
LOADED = 4


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def word(memory, addr, value):
    if isinstance(value, str):
        memory.append({'addr': addr, 'pointer': value})
    else:
        memory.append({'addr': addr, 'bytes': u64(value)})


def fixture(address, cid, args, buffers, memory, stubs, returns='void', gs=None):
    f = {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': args,
         'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': []}
    if gs:
        f['gs'] = gs
    return f


def heaps(buffers, memory):
    """Two heaps sharing one fake vtable (allocate at 0x58, free at 0x70)."""
    buffers['heap_vtable'] = {'size': 0x80}
    for name in ('heap', 'thread_heap'):
        buffers[name] = {'size': 8}
        word(memory, f'buf:{name}+0', 'heap_vtable')
    memory.append({'addr': 'buf:heap_vtable+0x58', 'guest': ALLOCATE})
    memory.append({'addr': 'buf:heap_vtable+0x70', 'guest': FREE})


def release_setup(buffers, memory, thread_heap):
    buffers['tls'] = {'size': THREAD + 8}
    word(memory, NEW_STATE_OFFSET_GLOBAL, STATE_OFFSET)
    word(memory, f'buf:tls+{THREAD}', f'tls+{THREAD}')
    word(memory, f'buf:tls+{THREAD + STATE_OFFSET + RELEASE_HEAP}', 'thread_heap' if thread_heap else 0)


def release_stubs(thread_heap, owner):
    """The stubs a DLNew release calls: the owner lookup unless the thread set a heap, the fatal
    error when no heap owns the buffer (then the original frees through a null heap and faults,
    so the cases always have an owner or a thread heap), and the free."""
    stubs = []
    if not thread_heap:
        stubs.append({'address': HEAP_OWNING, 'argc': 1, 'ret': 'buf:heap' if owner else 0})
    stubs.append({'address': FREE, 'argc': 2})
    return stubs


def adapter(buffers, memory, fill):
    buffers['adapter'] = {'size': 0x30, 'bytes': f'{fill:02x}' * 0x30}
    word(memory, 'buf:adapter+0x10', 'heap')
    word(memory, 'buf:adapter+0x18', 'thread_heap')   # heap index 1


def free_case(cid, buffer=True, thread_heap=False, fill=0xa5):
    buffers, memory = {'old': {'size': 0x20, 'bytes': '3c' * 0x20}}, []
    heaps(buffers, memory)
    adapter(buffers, memory, fill)
    release_setup(buffers, memory, thread_heap)
    stubs = release_stubs(thread_heap, True) if buffer else []
    args = {'rdi': 'buf:adapter', 'rsi': 'buf:old' if buffer else '0x0'}
    return fixture('0x023411d0', cid, args, buffers, memory, stubs, gs=f'buf:tls+{THREAD}')


def reallocate_case(cid, old_size, new_size, buffer=True, allocation=True, thread_heap=False, index=0,
                    alignment=0x10, fill=0xa5):
    buffers, memory = {'old': {'size': 0x40, 'bytes': ''.join(f'{i:02x}' for i in range(0x40))},
                       'new': {'size': 0x40, 'bytes': 'c3' * 0x40}}, []
    heaps(buffers, memory)
    adapter(buffers, memory, fill)
    release_setup(buffers, memory, thread_heap)
    stubs = []
    if old_size != new_size:
        stubs.append({'address': ALLOCATE, 'argc': 3, 'ret': 'buf:new' if allocation else 0})
        if allocation:
            stubs.append({'address': MEMCPY, 'argc': 3, 'ret': 'buf:new'})
        if buffer:
            stubs += release_stubs(thread_heap, True)
    args = {'rdi': 'buf:adapter', 'rsi': 'buf:old' if buffer else '0x0', 'rdx': hex(old_size),
            'rcx': hex(new_size), 'r8': hex(alignment), 'r9': hex(index)}
    return fixture('0x02341330', cid, args, buffers, memory, stubs, returns='i64', gs=f'buf:tls+{THREAD}')


def scaleform(buffers, memory, singleton=True, player=True, menu_state=True):
    buffers['sprj'] = {'size': 0x20, 'bytes': '5a' * 0x20}
    buffers['player'] = {'size': 0x10}
    buffers['menu_state'] = {'size': 0x40, 'bytes': '00' * 0x40}
    buffers['screen'] = {'size': 0xe0, 'bytes': 'a5' * 0xe0}
    word(memory, SPRJ_SCALEFORM, 'sprj' if singleton else 0)
    word(memory, 'buf:sprj+0x8', 'player' if player else 0)
    word(memory, MENU_STATE, 'menu_state' if menu_state else 0)


def close_case(cid, singleton=True, player=True, menu_state=True):
    """No singleton is only a case without the menu state: with it, both sides read the missing
    singleton's player after the report and fault."""
    buffers, memory = {}, []
    scaleform(buffers, memory, singleton, player, menu_state)
    stubs = []
    missing = [] if singleton else [{'address': FATAL_ERROR, 'argc': 4}]
    if menu_state:
        stubs += missing
        if singleton and player:
            stubs.append({'address': PLAYER_FLUSH, 'argc': 1})
        stubs.append({'address': CLOSE_MOVIE, 'argc': 1})
    stubs += missing + [{'address': QUEUE, 'argc': 2}]
    args = {'rdi': 'buf:screen', 'rsi': '0x1234'}
    return fixture('0x023548d0', cid, args, buffers, memory, stubs)


def step_case(cid, results, player=True, steps=0):
    """results: what the player's step returns on each try (the function stops at a failure)."""
    buffers, memory = {}, []
    scaleform(buffers, memory, True, player, True)
    memory.append({'addr': 'buf:screen+0xd0', 'bytes': u32(steps)})
    stubs = []
    if player:
        for r in results:
            stubs.append({'address': PLAYER_STEP, 'argc': 2, 'argu32': [1], 'ret': r})
            if not r:
                break
        if not all(results[:2]):
            stubs.append({'address': CLOSE_MOVIE, 'argc': 1})
    else:
        stubs.append({'address': CLOSE_MOVIE, 'argc': 1})
    stubs.append({'address': QUEUE, 'argc': 2})
    args = {'rdi': 'buf:screen', 'rsi': '0xfeed'}
    return fixture('0x02354d90', cid, args, buffers, memory, stubs)


def tex_case(cid, found, repository=True):
    buffers = {'owner': {'size': 0x950, 'bytes': 'a5' * 0x950}, 'repository': {'size': 0x10},
               'source': {'size': 8}, 'source_vtable': {'size': 0x140}, 'texture': {'size': 0x10}}
    memory = []
    word(memory, TEX_REPOSITORY, 'repository' if repository else 0)
    word(memory, 'buf:owner+0x940', 'source')
    word(memory, 'buf:source+0', 'source_vtable')
    memory.append({'addr': 'buf:source_vtable+0x130', 'guest': MAKE_TEXTURE})
    missing = [] if repository else [{'address': FATAL_ERROR, 'argc': 4}]
    stubs = missing + [{'address': TEX_FIND, 'argc': 2, 'ret': 'buf:texture' if found else 0}]
    if not found:
        stubs += missing + [{'address': MAKE_TEXTURE, 'argc': 1, 'ret': 'buf:texture'},
                            {'address': TEX_ADD, 'argc': 3}]
    return fixture('0x02360d80', cid, {'rdi': 'buf:owner', 'rsi': '0x77'}, buffers, memory, stubs)


def holder_case(address, cid, work=True, users=1, references=2, fill=0xa5):
    buffers = {'holder': {'size': 0x10, 'bytes': f'{fill:02x}' * 0x10},
               'work': {'size': 0x40, 'bytes': '5a' * 0x40}, 'work_vtable': {'size': 8}}
    memory = []
    word(memory, 'buf:holder+0x8', 'work' if work else 0)
    word(memory, 'buf:work+0', 'work_vtable')
    memory.append({'addr': 'buf:work_vtable+0', 'guest': DESTROY})
    memory.append({'addr': 'buf:work+0x8', 'bytes': u32(references)})
    memory.append({'addr': 'buf:work+0x30', 'bytes': u32(users)})
    stubs = []
    if work:
        if users <= 1 or users >= 0x80000000:
            stubs.append({'address': SIGNAL_SET, 'argc': 2, 'argu32': [1]})
        if references == 1:
            stubs.append({'address': DESTROY, 'argc': 1})
        elif references <= 0 or references >= 0x80000000:
            stubs.append({'address': FATAL_ERROR, 'argc': 3})
    return fixture(address, cid, {'rdi': 'buf:holder'}, buffers, memory, stubs)


def loader_buffers(files, fill=0xa5, name_heap=False):
    """files: three entries, each None (no file) or (state, size)."""
    buffers = {'loader': {'size': 0x1a0, 'bytes': f'{fill:02x}' * 0x1a0},
               'name_text': {'size': 0x20, 'bytes': b'invented-name\0'.hex()}}
    memory = []
    for i, f in enumerate(files):
        if f is None:
            word(memory, f'buf:loader+{0x168 + 0x10 * i:#x}', 0)
            continue
        state, size = f
        buffers[f'file{i}'] = {'size': 0xa0, 'bytes': '96' * 0xa0}
        buffers[f'bytes{i}'] = {'size': 0x20, 'bytes': f'{i + 1:02x}' * 0x20}
        word(memory, f'buf:loader+{0x168 + 0x10 * i:#x}', f'file{i}')
        memory.append({'addr': f'buf:file{i}+0x78', 'bytes': f'{state:02x}'})
        word(memory, f'buf:file{i}+0x88', f'bytes{i}' if size is not None else 0)
        word(memory, f'buf:file{i}+0x90', size or 0)
    if name_heap:
        word(memory, 'buf:loader+0x128', 'name_text')
        word(memory, 'buf:loader+0x140', 0x1f)
    else:
        memory.append({'addr': 'buf:loader+0x128', 'bytes': b'short\0'.hex()})
        word(memory, 'buf:loader+0x140', 0xf)
    return buffers, memory


def wait_case(cid, files, fill=0xa5):
    buffers, memory = loader_buffers(files, fill)
    states = [f[0] if f else None for f in files]
    finished = states[2] in (None, LOADED) and all(s in (None, LOADED) for s in states[:2])
    stubs = [{'address': LOADER_FINISH, 'argc': 1}] if finished else []
    return fixture('0x02387050', cid, {'rdi': 'buf:loader'}, buffers, memory, stubs)


def build_case(cid, files, result=1, name_heap=False, fill=0xa5):
    buffers, memory = loader_buffers(files, fill, name_heap)
    first = files[0]
    stubs = []
    if first and first[1]:
        stubs.append({'address': BUILD, 'argc': 6, 'argu32': [3, 5], 'ret': result,
                      'argmem': [{'arg': 2, 'size': 0x10}]})
    stubs.append({'address': LOADER_FINISH, 'argc': 1})
    return fixture('0x02387650', cid, {'rdi': 'buf:loader'}, buffers, memory, stubs)


def named_case(cid, held=True, handle=True, registry=True, entry=True, name_heap=False, fill=0xa5):
    buffers = {'resource': {'size': 0xc0, 'bytes': f'{fill:02x}' * 0xc0}, 'handle': {'size': 0x10},
               'registry': {'size': 0x10}, 'entry': {'size': 0x10},
               'name_text': {'size': 0x30, 'bytes': 'invented resource\0'.encode('utf-16-le').hex()}}
    memory = [{'addr': 'buf:resource+0x70', 'bytes': '01' if held else '00'}]
    word(memory, 'buf:resource+0x78', 'handle' if handle else 0)
    word(memory, REGISTRY, 'registry' if registry else 0)
    if name_heap:
        word(memory, 'buf:resource+0x88', 'name_text')
        word(memory, 'buf:resource+0xa0', 0x17)
    else:
        memory.append({'addr': 'buf:resource+0x88', 'bytes': 'abc\0'.encode('utf-16-le').hex()})
        word(memory, 'buf:resource+0xa0', 0x7)
    word(memory, 'buf:resource+0x98', 3)
    stubs = []
    if held:
        if handle:
            stubs.append({'address': HANDLE_RELEASE, 'argc': 1})
        if registry:
            stubs.append({'address': REGISTRY_FIND, 'argc': 2, 'ret': 'buf:entry' if entry else 0,
                          'argmem': [{'arg': 1, 'size': 0x8}]})
            # listed even when no entry is found, so a release of a null entry would show
            stubs.append({'address': REGISTRY_RELEASE, 'argc': 4, 'argu32': [2, 3]})
    return fixture('0x0238a210', cid, {'rdi': 'buf:resource'}, buffers, memory, stubs)


def spares(stubs, callees):
    """One more scripted call per callee, so an unexpected call is recorded."""
    return stubs + [{'address': a, 'argc': n, 'ret': 0} for a, n in callees]


def registry(buffers, memory, active, table, fill):
    buffers['registry'] = {'size': 0x20, 'bytes': f'{fill:02x}' * 0x20}
    buffers['table'] = {'size': 0x10}
    memory.append({'addr': 'buf:registry+0', 'bytes': u32(active)})
    word(memory, 'buf:registry+0x18', 'table' if table else 0)


def find_case(cid, active=1, table=True, found=True, fill=0xa5):
    buffers = {'name': {'size': 0x10, 'bytes': 'abc\0'.encode('utf-16-le').hex()}, 'entry': {'size': 0x10}}
    memory = []
    registry(buffers, memory, active, table, fill)
    stubs = []
    if active and table:
        stubs.append({'address': TABLE_FIND, 'argc': 2, 'ret': 'buf:entry' if found else 0})
    stubs = spares(stubs, [(TABLE_FIND, 2)])
    return fixture('0x013a2a00', cid, {'rdi': 'buf:registry', 'rsi': 'buf:name'}, buffers, memory, stubs,
                   returns='i64')


def registry_release_case(cid, active=1, table=True, entry=True, first=1, second=1, fill=0xa5):
    buffers, memory = {'entry': {'size': 0x10}}, []
    registry(buffers, memory, active, table, fill)
    stubs = []
    if active and table and entry:
        stubs.append({'address': TABLE_RELEASE, 'argc': 4, 'argu32': [2, 3]})
    stubs = spares(stubs, [(TABLE_RELEASE, 4)])
    args = {'rdi': 'buf:registry', 'rsi': 'buf:entry' if entry else '0x0', 'rdx': hex(first & 0xffffffff),
            'rcx': hex(second & 0xffffffff)}
    return fixture('0x013a33c0', cid, args, buffers, memory, stubs)


def handle_case(cid, owner=True, answer=0x1234567890abcdef, fill=0xa5):
    buffers = {'handle': {'size': 0x18, 'bytes': f'{fill:02x}' * 0x18}, 'owner': {'size': 0x10},
               'owner_vtable': {'size': 0x88}}
    memory = []
    word(memory, 'buf:handle+0x8', 'owner' if owner else 0)
    word(memory, 'buf:owner+0', 'owner_vtable')
    memory.append({'addr': 'buf:owner_vtable+0x80', 'guest': OWNER_RELEASE})
    stubs = [{'address': OWNER_RELEASE, 'argc': 2, 'ret': answer}] if owner else []
    stubs = spares(stubs, [(OWNER_RELEASE, 2)])
    return fixture('0x01401720', cid, {'rdi': 'buf:handle'}, buffers, memory, stubs, returns='i64')


def queue_case(cid, skip=0, player=True, job=True, references=2, flags=(0, 1), fill=0x5a):
    """flags: requested_before and requested as the queue finds them."""
    buffers = {'sprj': {'size': 0x30, 'bytes': f'{fill:02x}' * 0x30}, 'player': {'size': 0x10},
               'job': {'size': 0x20, 'bytes': 'c6' * 0x20}, 'job_vtable': {'size': 8},
               'waiter_vtable': {'size': 0x20}}
    memory = [{'addr': 'buf:sprj+0x20', 'bytes': f'{flags[0]:02x}{flags[1]:02x}{skip:02x}'}]
    word(memory, 'buf:sprj+0x8', 'player' if player else 0)
    word(memory, PENDING_JOB, 'job' if job else 0)
    word(memory, 'buf:job+0', 'job_vtable')
    memory.append({'addr': 'buf:job+0x8', 'bytes': u32(references)})
    word(memory, 'buf:job+0x10', 'waiter_vtable')
    memory.append({'addr': 'buf:job_vtable+0', 'guest': DESTROY})
    memory.append({'addr': 'buf:waiter_vtable+0x18', 'guest': JOB_WAIT})
    stubs = []
    if not skip and player:
        stubs.append({'address': SCALEFORM_LOCK, 'argc': 0, 'ret': 'buf:player'})
        if job:
            stubs.append({'address': JOB_WAIT, 'argc': 2, 'argu32': [1]})
            if references == 1:
                stubs.append({'address': DESTROY, 'argc': 1})
            elif references <= 0 or references >= 0x80000000:
                stubs.append({'address': FATAL_ERROR, 'argc': 3})
        stubs.append({'address': PLAYER_QUEUE, 'argc': 2})
    stubs = spares(stubs, [(SCALEFORM_LOCK, 0), (JOB_WAIT, 2), (DESTROY, 1), (FATAL_ERROR, 3), (PLAYER_QUEUE, 2)])
    args = {'rdi': 'buf:sprj', 'rsi': '0xfeedface12345678'}
    return fixture('0x0233ef00', cid, args, buffers, memory, stubs)


def cases_for(address):
    if address == '0x013a2a00':
        return [find_case('found'), find_case('not_found', found=False), find_case('inactive', active=0),
                find_case('no_table', table=False), find_case('inactive_no_table', active=0, table=False),
                find_case('active_negative', active=-1), find_case('active_high_byte', active=0x100),
                find_case('zero_filled', fill=0), find_case('all_bits', fill=0xff)]
    if address == '0x013a33c0':
        return [registry_release_case('released'), registry_release_case('no_entry', entry=False),
                registry_release_case('inactive', active=0), registry_release_case('no_table', table=False),
                registry_release_case('active_high_byte', active=0x100),
                registry_release_case('other_arguments', first=0, second=7),
                registry_release_case('negative_arguments', first=-1, second=-0x80000000),
                registry_release_case('inactive_no_entry', active=0, entry=False),
                registry_release_case('zero_filled', fill=0), registry_release_case('all_bits', fill=0xff)]
    if address == '0x01401720':
        return [handle_case('owner'), handle_case('owner_answers_zero', answer=0),
                handle_case('owner_answers_one', answer=1), handle_case('no_owner', owner=False),
                handle_case('zero_filled', fill=0), handle_case('all_bits', fill=0xff)]
    if address == '0x0233ef00':
        return [queue_case('player_job_kept'), queue_case('player_job_last_reference', references=1),
                queue_case('player_job_no_references', references=0),
                queue_case('player_job_negative_references', references=0xfffffffb),
                queue_case('player_no_job', job=False), queue_case('no_player', player=False),
                queue_case('no_player_no_job', player=False, job=False),
                queue_case('skipped', skip=1), queue_case('skipped_no_player', skip=1, player=False),
                queue_case('skip_any_byte', skip=0x80), queue_case('flags_set', flags=(1, 1)),
                queue_case('flags_moved_down', flags=(0, 1)), queue_case('flags_cleared', flags=(1, 0)),
                queue_case('flags_other_bytes', flags=(0x5a, 0x80)),
                queue_case('skipped_flags', skip=1, flags=(0, 1)),
                queue_case('zero_filled', fill=0), queue_case('all_bits', fill=0xff)]
    if address == '0x023411d0':
        return [free_case('owner_heap'), free_case('thread_heap', thread_heap=True),
                free_case('null_buffer', buffer=False), free_case('null_buffer_thread_heap_kept', buffer=False,
                                                                 thread_heap=True),
                free_case('zero_filled', fill=0), free_case('all_bits', fill=0xff)]
    if address == '0x02341330':
        return [reallocate_case('grow', 0x10, 0x30), reallocate_case('shrink', 0x30, 0x10),
                reallocate_case('same_size', 0x20, 0x20), reallocate_case('same_size_null', 0, 0, buffer=False),
                reallocate_case('from_null', 0, 0x20, buffer=False),
                reallocate_case('allocation_fails', 0x10, 0x30, allocation=False),
                reallocate_case('allocation_fails_null', 0, 0x30, buffer=False, allocation=False),
                reallocate_case('thread_heap', 0x10, 0x30, thread_heap=True),
                reallocate_case('second_heap', 0x10, 0x30, index=1),
                reallocate_case('to_zero', 0x10, 0), reallocate_case('alignment_passed', 0x8, 0x40, alignment=0x40),
                reallocate_case('huge_old_size', 0xffffffffffffff00, 0x40),
                reallocate_case('zero_filled', 0x10, 0x30, fill=0), reallocate_case('all_bits', 0x10, 0x30, fill=0xff)]
    if address == '0x023548d0':
        return [close_case('with_player'), close_case('no_player', player=False),
                close_case('no_menu_state', menu_state=False),
                close_case('no_menu_state_no_player', menu_state=False, player=False),
                close_case('no_singleton_no_menu_state', singleton=False, menu_state=False)]
    if address == '0x02354d90':
        return [step_case('both_steps', [1, 1]), step_case('first_fails', [0]),
                step_case('second_fails', [1, 0]), step_case('no_player', [], player=False),
                step_case('counter_carries_on', [1, 1], steps=41), step_case('counter_wraps', [1, 0], steps=0x7fffffff),
                step_case('no_player_negative_counter', [], player=False, steps=-5),
                step_case('step_returns_other_true', [0x55, 0x80])]
    if address == '0x02360d80':
        return [tex_case('found', True), tex_case('missing', False),
                tex_case('no_repository_found', True, repository=False),
                tex_case('no_repository_missing', False, repository=False)]
    if address in ('0x02363c00', '0x02363d20'):
        return [holder_case(address, 'last_user_last_reference', users=1, references=1),
                holder_case(address, 'last_user_more_references', users=1, references=3),
                holder_case(address, 'more_users_last_reference', users=2, references=1),
                holder_case(address, 'more_users_more_references', users=5, references=2),
                holder_case(address, 'no_users_left', users=0, references=2),
                holder_case(address, 'negative_users', users=0xfffffffe, references=2),
                holder_case(address, 'no_references_left', users=3, references=0),
                holder_case(address, 'negative_references', users=3, references=0xffffffff),
                holder_case(address, 'no_work', work=False),
                holder_case(address, 'zero_filled_holder', fill=0),
                holder_case(address, 'all_bits_holder', fill=0xff)]
    if address == '0x02387050':
        L = LOADED
        return [wait_case('all_loaded', [(L, 0x10), (L, 0x20), (L, 0x30)]),
                wait_case('no_files', [None, None, None]),
                wait_case('first_loading', [(1, 0x10), (L, 0x20), (L, 0x30)]),
                wait_case('second_loading', [(L, 0x10), (3, 0x20), (L, 0x30)]),
                wait_case('third_loading', [(L, 0x10), (L, 0x20), (0, 0x30)]),
                wait_case('first_and_third_loading', [(2, 0x10), (L, 0x20), (5, 0x30)]),
                wait_case('only_third', [None, None, (L, 0x40)]),
                wait_case('only_first_loading', [(0xff, 0x10), None, None]),
                wait_case('state_above_loaded', [(5, 0x10), None, (L, 0x30)]),
                wait_case('zero_filled', [(L, 0), (L, 0), None], fill=0),
                wait_case('all_bits', [(L, 0x10), None, (L, 0x30)], fill=0xff)]
    if address == '0x02387650':
        L = LOADED
        return [build_case('built', [(L, 0x20), None, (L, 0x10)]),
                build_case('declined', [(L, 0x20), None, (L, 0x10)], result=0),
                build_case('failed', [(L, 0x20), None, (L, 0x10)], result=2),
                build_case('failed_negative', [(L, 0x20), None, (L, 0x10)], result=-1),
                build_case('no_extra', [(L, 0x20), None, None]),
                build_case('no_first', [None, (L, 0x10), (L, 0x10)]),
                build_case('first_without_bytes', [(L, None), None, (L, 0x10)]),
                build_case('first_empty', [(L, 0), None, (L, 0x10)]),
                build_case('long_name', [(L, 0x20), None, (L, 0x10)], name_heap=True),
                build_case('extra_size_truncated', [(L, 0x20), None, (L, 0x1_0000_0010)]),
                build_case('zero_filled', [(L, 0x20), None, (L, 0x10)], fill=0),
                build_case('all_bits', [(L, 0x20), None, (L, 0x10)], fill=0xff)]
    if address == '0x0238a210':
        return [named_case('held'), named_case('not_held', held=False), named_case('no_handle', handle=False),
                named_case('no_registry', registry=False), named_case('not_in_registry', entry=False),
                named_case('long_name', name_heap=True), named_case('long_name_not_in_registry', name_heap=True,
                                                                    entry=False),
                named_case('nothing_but_held', handle=False, registry=False),
                named_case('zero_filled', fill=0), named_case('all_bits', fill=0xff)]
    raise KeyError(address)


ADDRESSES = ('0x023411d0', '0x02341330', '0x023548d0', '0x02354d90', '0x02360d80', '0x02363c00', '0x02363d20',
             '0x02387050', '0x02387650', '0x0238a210', '0x013a2a00', '0x013a33c0', '0x01401720', '0x0233ef00')


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    cases = cases_for(sys.argv[2])
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
