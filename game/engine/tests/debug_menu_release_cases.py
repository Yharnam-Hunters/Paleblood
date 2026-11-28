#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for three engine families (game/engine/debug_menu_entry_family.cpp,
text_for_slot_family.cpp and counted_member_release_family.cpp).

game/engine/family_cases.py hands every member these cases at its own address. Virtual slots
point at spare game addresses that the cases stub.

removes_debug_menu_entry_keyed_by_object: no manager; a manager with the name interning, the
removal and the name's destruction stubbed (the removal compares the whole key, the destruction
the name it is given); interning that yields a null handle; interning that replaces the manager
(read before it) and a removal that changes the key's name (read by the destruction after it);
the interning and destruction run as original code without a name pool, and the removal run as
original code on a disabled manager and on one without its entries.

formats_one_of_three_texts_for_slot_0x80: the byte at +0x68 clear or set to several values; each
of the three texts (the third argument nonzero, or zero with the object's slot 0x20 returning
true or false, also as values whose low byte decides); a thread heap made on first use; a heap
whose info lacks the valid flag (reported, then used); a text kept inline, one on the heap at the
inline limit and above it (freed through the allocator); and a formatter that replaces the second
argument's vtable (its slot is read after the formatting).

releases_counted_member_at_0x30_then_base: no member; counts where the first count reaches 0
with and without the flag, the second count reaching 0 or wrapping, both at once, bits 30 and 31
set, counts of 0 and the largest counts; slot 0x18 rewriting the word (reread after it, with the
flag cleared) and the vtable (reread for slot 0x20); the base release stubbed (comparing the
cleared members) and run as original code on an object with nothing to drop.
"""
from __future__ import annotations

import struct

# Spare game addresses the cases' vtables point at (stubbed).
QUERY, TAKE_TEXT, OTHER_TAKE_TEXT, HEAP_INFO, HEAP_FREE = 0x00500000, 0x00500100, 0x00500180, 0x00500200, 0x00500300
FIRST_ENDED, SECOND_ENDED, OTHER_FIRST_ENDED, OTHER_SECOND_ENDED = 0x00500400, 0x00500500, 0x00500600, 0x00500700

MENU_MANAGER, NAME_POOL = '0x058b3570', '0x058b38f0'
MENU_REMOVE, NAME_CONSTRUCT, NAME_DESTROY = '0x013a33a0', '0x013aeaf0', '0x013aebc0'
THREAD_HEAP_OFFSET, CREATE_THREAD_HEAP, FATAL_ERROR, FORMAT = '0x057e4568', '0x0247d250', '0x024b55b0', '0x02b62d00'
RELEASE_BASE = '0x02751f90'
TLS_SLOT = -0x730


def u32(value: int) -> str:
    return struct.pack('<I', value & 0xffffffff).hex()


def u64(value: int) -> str:
    return struct.pack('<Q', value & 0xffffffffffffffff).hex()


def pattern(seed: int, size: int) -> str:
    return bytes((seed * 37 + i * 11) & 0xff for i in range(size)).hex()


def at(buffer: str, offset: int) -> str:
    return f'buf:{buffer}+0x{offset:x}'


def case(address: str, case_id: str, args: dict, buffers: dict, memory: list, stubs: list, gs: str | None = None) -> dict:
    c = {'schema': 1, 'address': address, 'id': case_id, 'returns': 'void', 'args': args,
         'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}
    if gs:
        c['gs'] = gs
    return c


# --- removes_debug_menu_entry_keyed_by_object ---------------------------------------------------
OBJECT = '0x00007f0012345670'
HANDLE = 0x6a6a00004444aaaa


def menu_case(address: str, case_id: str, manager: bool = True, handle: int | None = HANDLE,
              replace_manager: bool = False, rename: int | None = None, remove_runs: bytes | None = None,
              obj: str = OBJECT) -> dict:
    buffers = {'manager': {'size': 0x20, 'bytes': pattern(1, 0x20)},
               'other_manager': {'size': 0x20, 'bytes': pattern(2, 0x20)}}
    memory = [{'addr': MENU_MANAGER, 'pointer': 'manager'} if manager else {'addr': MENU_MANAGER, 'bytes': u64(0)}]
    stubs = []
    if manager:
        if handle is None:   # the name pool is missing: the original interning stores a null handle
            memory.append({'addr': NAME_POOL, 'bytes': u64(0)})
        else:
            construct = {'address': NAME_CONSTRUCT, 'argc': 2, 'argi': [1],
                         'writes': [{'arg': 0, 'bytes': u64(handle)}]}
            if replace_manager:
                construct['writes'].append({'addr': MENU_MANAGER, 'pointer': 'other_manager'})
            stubs.append(construct)
        if remove_runs is None:
            remove = {'address': MENU_REMOVE, 'argc': 4, 'argi': [0, 2, 3], 'argmem': [{'arg': 1, 'size': 16}]}
            if rename is not None:
                remove['writes'] = [{'arg': 1, 'offset': 8, 'bytes': u64(rename)}]
            stubs.append(remove)
        else:   # the removal as original code: a disabled manager, or one without its entries
            memory.append({'addr': 'buf:manager+0x0', 'bytes': remove_runs.hex()})
        if handle is not None:
            stubs.append({'address': NAME_DESTROY, 'argc': 1, 'argmem': [{'arg': 0, 'size': 8}]})
    return case(address, case_id, {'rdi': obj}, buffers, memory, stubs)


def menu_entry_cases(address: str) -> list[dict]:
    disabled = bytes(0x20)
    no_entries = struct.pack('<I', 1) + bytes(0x1c)
    return [
        menu_case(address, 'no_manager', manager=False),
        menu_case(address, 'removes'),
        menu_case(address, 'null_object', obj='0x0'),
        menu_case(address, 'high_object', obj='0xffffffff00000008'),
        menu_case(address, 'null_handle', handle=0),
        menu_case(address, 'interning_replaces_manager', replace_manager=True),
        menu_case(address, 'removal_renames_key', rename=0x5151000077770000),
        menu_case(address, 'removal_clears_name', rename=0),
        menu_case(address, 'no_name_pool', handle=None),
        menu_case(address, 'removal_disabled', remove_runs=disabled),
        menu_case(address, 'removal_without_entries', remove_runs=no_entries),
    ]


# --- formats_one_of_three_texts_for_slot_0x80 ---------------------------------------------------
LONG_TEXT = '410042004300440045004600470048004900000000000000'


def text_case(address: str, case_id: str, enabled: int = 1, flagged: int = 0, query: int | None = None,
              heap_made: bool = False, heap_info: int = 0x20, long_text: int | None = None,
              replace_target_vtable: bool = False) -> dict:
    below = -TLS_SLOT
    buffers = {
        'self': {'size': 0x70, 'bytes': pattern(3, 0x70)},
        'self_vt': {'size': 0x28},
        'target': {'size': 0x10, 'bytes': pattern(4, 0x10)},
        'target_vt': {'size': 0x88},
        'other_target_vt': {'size': 0x88},
        'tls': {'size': below + 8},
        'heap': {'size': 0x30, 'bytes': pattern(5, 0x30)},
        'heap_vt': {'size': 0x78},
        'heap_text': {'size': 0x20, 'bytes': LONG_TEXT},
    }
    memory = [
        {'addr': 'buf:self+0x0', 'pointer': 'self_vt'},
        {'addr': 'buf:self+0x68', 'bytes': f'{enabled:02x}'},
        {'addr': 'buf:self_vt+0x20', 'guest': f'0x{QUERY:08x}'},
        {'addr': 'buf:target+0x0', 'pointer': 'target_vt'},
        {'addr': 'buf:target_vt+0x80', 'guest': f'0x{TAKE_TEXT:08x}'},
        {'addr': 'buf:other_target_vt+0x80', 'guest': f'0x{OTHER_TAKE_TEXT:08x}'},
        {'addr': THREAD_HEAP_OFFSET, 'bytes': struct.pack('<q', TLS_SLOT).hex()},
        {'addr': f'buf:tls+0x{below:x}', 'pointer': f'tls+0x{below:x}'},
        {'addr': 'buf:heap+0x28', 'pointer': 'heap_vt'},
        {'addr': 'buf:heap_vt+0x20', 'guest': f'0x{HEAP_INFO:08x}'},
        {'addr': 'buf:heap_vt+0x70', 'guest': f'0x{HEAP_FREE:08x}'},
    ]
    stubs = []
    if enabled:
        if heap_made:
            memory.append({'addr': 'buf:tls+0x0', 'bytes': u64(0)})
            stubs.append({'address': CREATE_THREAD_HEAP, 'argc': 0, 'writes': [{'addr': 'buf:tls+0x0', 'pointer': 'heap'}]})
        else:
            memory.append({'addr': 'buf:tls+0x0', 'pointer': 'heap'})
        stubs.append({'address': f'0x{HEAP_INFO:08x}', 'argc': 3, 'argi': [1, 2],
                      'writes': [{'arg': 0, 'bytes': f'{heap_info:02x}'}]})
        if not heap_info & 0x20:
            stubs.append({'address': FATAL_ERROR, 'argc': 3})
        if not flagged & 0xffffffff:
            stubs.append({'address': f'0x{QUERY:08x}', 'argc': 1, 'argi': [0], 'ret': query})
        # The formatter is given the empty string (its first character, size, capacity, allocator
        # and owner flag) and the chosen text.
        fmt = {'address': FORMAT, 'argc': 2, 'argi': [1],
               'argmem': [{'arg': 0, 'offset': 8, 'size': 2}, {'arg': 0, 'offset': 0x18, 'size': 0x19}]}
        if long_text is not None:
            fmt['writes'] = [{'arg': 0, 'offset': 8, 'pointer': 'heap_text'},
                             {'arg': 0, 'offset': 0x18, 'bytes': u64(9) + u64(long_text)}]
        else:
            fmt['writes'] = [{'arg': 0, 'offset': 8, 'bytes': '310032000000000000000000ffff0000'},
                             {'arg': 0, 'offset': 0x18, 'bytes': u64(2) + u64(7)}]
        if replace_target_vtable:
            fmt['writes'].append({'addr': 'buf:target+0x0', 'pointer': 'other_target_vt'})
        stubs.append(fmt)
        take = OTHER_TAKE_TEXT if replace_target_vtable else TAKE_TEXT
        stubs.append({'address': f'0x{take:08x}', 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 16}]})
        if long_text is not None:
            stubs.append({'address': f'0x{HEAP_FREE:08x}', 'argc': 2, 'argi': [0, 1]})
    return case(address, case_id, {'rdi': 'buf:self', 'rsi': 'buf:target', 'rdx': f'0x{flagged:x}'},
                buffers, memory, stubs, gs=f'buf:tls+0x{below:x}')


def text_cases(address: str) -> list[dict]:
    return [
        text_case(address, 'disabled', enabled=0),
        text_case(address, 'flagged', flagged=1),
        text_case(address, 'flagged_negative', flagged=0xffffffff),
        text_case(address, 'flagged_high_bit', flagged=0x80000000),
        text_case(address, 'flag_only_above_32_bits', flagged=0x100000000, query=1),
        text_case(address, 'query_true', query=1),
        text_case(address, 'query_false', query=0),
        text_case(address, 'query_low_byte_false', query=0x100),
        text_case(address, 'query_low_byte_true', query=0xff),
        text_case(address, 'enabled_other_value', enabled=0x80, query=0),
        text_case(address, 'heap_made', query=1, heap_made=True),
        text_case(address, 'heap_made_flagged', flagged=2, heap_made=True),
        text_case(address, 'heap_check_fails', query=0, heap_info=0),
        text_case(address, 'heap_check_other_bits', flagged=1, heap_info=0xdf),
        text_case(address, 'long_text', query=1, long_text=15),
        text_case(address, 'text_at_inline_limit', query=0, long_text=8),
        text_case(address, 'long_text_flagged', flagged=1, long_text=0x40),
        text_case(address, 'format_replaces_target_vtable', query=1, replace_target_vtable=True),
        text_case(address, 'format_replaces_target_vtable_flagged', flagged=1, replace_target_vtable=True,
                  long_text=12),
    ]


# --- releases_counted_member_at_0x30_then_base --------------------------------------------------
def release_case(address: str, case_id: str, counts: int | None = 0x8002, first_writes: int | None = None,
                 replace_vtable: bool = False, base_runs: bool = False) -> dict:
    buffers = {'self': {'size': 0x40, 'bytes': pattern(6, 0x40)},
               'counted': {'size': 0x10, 'bytes': pattern(7, 0x10)},
               'vt': {'size': 0x28}, 'other_vt': {'size': 0x28}}
    memory = [{'addr': 'buf:vt+0x18', 'guest': f'0x{FIRST_ENDED:08x}'},
              {'addr': 'buf:vt+0x20', 'guest': f'0x{SECOND_ENDED:08x}'},
              {'addr': 'buf:other_vt+0x18', 'guest': f'0x{OTHER_FIRST_ENDED:08x}'},
              {'addr': 'buf:other_vt+0x20', 'guest': f'0x{OTHER_SECOND_ENDED:08x}'}]
    stubs = []
    second_slot = SECOND_ENDED
    if counts is None:
        memory.append({'addr': 'buf:self+0x30', 'bytes': u64(0)})
    else:
        memory += [{'addr': 'buf:self+0x30', 'pointer': 'counted'}, {'addr': 'buf:counted+0x0', 'pointer': 'vt'},
                   {'addr': 'buf:counted+0x8', 'bytes': u32(counts)}]
        first = (counts + 0x7fff) & 0x7fff
        word = (counts & 0xffff8000) | first
        if first == 0 and counts & 0x40000000:
            stub = {'address': f'0x{FIRST_ENDED:08x}', 'argc': 1, 'argi': [0],
                    'argmem': [{'arg': 0, 'offset': 8, 'size': 4}]}
            writes = []
            if first_writes is not None:
                writes.append({'arg': 0, 'offset': 8, 'bytes': u32(first_writes)})
                word = first_writes
            if replace_vtable:
                writes.append({'arg': 0, 'offset': 0, 'pointer': 'other_vt'})
                second_slot = OTHER_SECOND_ENDED
            if writes:
                stub['writes'] = writes
            stubs.append(stub)
            word &= 0xbfffffff
        if ((word >> 15) + 0x7fff) & 0x7fff == 0:
            stubs.append({'address': f'0x{second_slot:08x}', 'argc': 1, 'argi': [0],
                          'argmem': [{'arg': 0, 'offset': 8, 'size': 4}]})
    if base_runs:   # nothing at +0x18 or +0x10 for the base release to drop
        memory += [{'addr': 'buf:self+0x10', 'bytes': u64(0)}, {'addr': 'buf:self+0x18', 'bytes': u64(0)}]
    else:
        stubs.append({'address': RELEASE_BASE, 'argc': 1, 'argi': [0], 'argmem': [{'arg': 0, 'offset': 0x28, 'size': 0x10}]})
    return case(address, case_id, {'rdi': 'buf:self'}, buffers, memory, stubs)


def counted_release_cases(address: str) -> list[dict]:
    r = release_case
    return [
        r(address, 'no_member', counts=None),
        r(address, 'no_member_base_runs', counts=None, base_runs=True),
        r(address, 'counts_stay', counts=0x00010002),
        r(address, 'first_ends_without_flag', counts=0x00010001),
        r(address, 'first_ends_with_flag', counts=0x40010001),
        r(address, 'both_end_without_flag', counts=0x00008001),
        r(address, 'both_end_with_flag', counts=0x40008001),
        r(address, 'second_ends', counts=0x00008005),
        r(address, 'second_ends_with_flag_kept', counts=0x40008003),
        r(address, 'second_wraps', counts=0x00000003),
        r(address, 'first_wraps', counts=0x00010000),
        r(address, 'zero_counts', counts=0),
        r(address, 'flag_only', counts=0x40000000),
        r(address, 'top_bit', counts=0x80008001),
        r(address, 'top_bits_first_ends', counts=0xc0010001),
        r(address, 'largest_counts', counts=0x3fffffff),
        r(address, 'all_bits', counts=0xffffffff),
        r(address, 'first_slot_rewrites_counts', counts=0x40018001, first_writes=0x40008005),
        r(address, 'first_slot_ends_second', counts=0x40050001, first_writes=0x40008000),
        r(address, 'first_slot_sets_top_bits', counts=0x40018001, first_writes=0xc0018000),
        r(address, 'first_slot_clears_counts', counts=0x40008001, first_writes=0),
        r(address, 'first_slot_replaces_vtable', counts=0x40008001, replace_vtable=True),
        r(address, 'base_runs', counts=0x40008001, base_runs=True),
    ]
