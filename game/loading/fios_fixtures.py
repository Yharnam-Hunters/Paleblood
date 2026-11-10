# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared synthetic edge cases for fios_objects.cpp, written by fios_open_file_cases.py
(0x00857cd0), fios_open_file_read_cases.py (0x0085cda0) and fios_directory_delete_cases.py
(0x0084d5a0).

The thread pointer (gs:[0]) points into a fake thread block; the thread-local offset
(0x057e40f8) is preset so the block holds a pointer to an allocator holder, whose allocator's
vtable slots point at stand-in game addresses that the cases stub: the allocation returns a
buffer for the new object, the sized free records its arguments. The FIOS calls are scripted,
with the open parameters compared byte for byte. Generated fixtures, not inputs recorded in a
game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'open': '0x00857cd0', 'open_read': '0x0085cda0', 'delete': '0x0084d5a0'}
THREAD_SLOT = '0x057e40f8'
ALLOCATE_FN = '0x00400020'   # stand-ins for the allocator's slots +0x10 and +0x18
FREE_SIZED_FN = '0x004000a0'
FREE_HEADED = '0x00829a90'    # game functions the destructor calls; stubbed
STRING_RELEASE = '0x00836840'
BLOCK = 0x80                  # thread pointer offset inside the tls buffer
SLOT = -0x30                  # thread-local offset of the holder pointer
PATH = b'/app0/dvdroot_ps4/event/m21_00_00_00.emevd.dcx\0'


def thread_allocator(c):
    c['buffers'].update({'tls': {'size': BLOCK + 0x10}, 'holder': {'size': 0x60},
                         'allocator': {'size': 0x10}, 'allocator_vt': {'size': 0x20}})
    c['gs'] = f'buf:tls+{BLOCK}'
    c['memory'] += [{'addr': f'buf:tls+{BLOCK}', 'pointer': f'tls+{BLOCK}'},
                    {'addr': THREAD_SLOT, 'bytes': struct.pack('<q', SLOT).hex()},
                    {'addr': f'buf:tls+{BLOCK + SLOT}', 'pointer': 'holder'},
                    {'addr': 'buf:holder+0x58', 'pointer': 'allocator'},
                    {'addr': 'buf:allocator', 'pointer': 'allocator_vt'},
                    {'addr': 'buf:allocator_vt+0x10', 'guest': ALLOCATE_FN},
                    {'addr': 'buf:allocator_vt+0x18', 'guest': FREE_SIZED_FN}]


def open_case(kind, cid, result=0, handle=5, flags=0x0a, write_handle=True, junk=0x1234):
    c = {'schema': 1, 'address': ADDRESSES[kind], 'id': cid, 'returns': 'i64',
         'args': {'rdi': 'buf:path', 'rsi': hex(junk), 'rdx': hex(flags)},
         'buffers': {'path': {'size': len(PATH), 'bytes': PATH.hex()}, 'object': {'size': 0x20, 'bytes': 'e1' * 0x20}},
         'memory': [], 'imports': [], 'stubs': []}
    thread_allocator(c)
    fios = {'name': 'sceFiosFHOpenSync', 'argc': 3, 'ret': hex(result), 'argmem': [{'arg': 3, 'size': 0x18}]}
    if write_handle:
        fios['writes'] = [{'arg': 1, 'size': 4, 'bytes': struct.pack('<i', handle).hex()}]
    c['imports'].append(fios)
    if result & 0xffffffff == 0:
        c['stubs'].append({'address': ALLOCATE_FN, 'argc': 2, 'ret': 'buf:object'})
    return c


def open_cases(kind):
    out = [
        open_case(kind, 'ordinary', handle=5),
        open_case(kind, 'handle_zero', handle=0),
        open_case(kind, 'handle_negative', handle=-3),
        open_case(kind, 'handle_int32_min', handle=-0x80000000),
        open_case(kind, 'fails_negative', result=0x80010002),
        open_case(kind, 'fails_positive', result=1),
        open_case(kind, 'fails_low_word_only', result=0xffffffff00000000 | 0x10),
        open_case(kind, 'ok_upper_word_set', result=0xffffffff00000000),
        open_case(kind, 'fails_handle_written', result=0x80010002, handle=7),
    ]
    for flags in (0x00, 0x01, 0x1a, 0xffff, 0xdeadbeefcafe000a):
        out.append(open_case(kind, f'flags_{flags:x}', flags=flags))
    if kind == 'open':
        # Only this variant starts the handle at -1; the read variant leaves it uninitialized,
        # so a successful open that writes no handle is not a case for it.
        out.append(open_case(kind, 'handle_not_written', write_handle=False))
    return out


def delete_case(cid, handle=4, close_result=0, entries=True, header=0xffff0001, junk_vtable=True):
    data = bytearray(b'\x5d' * 0x40)
    struct.pack_into('<Q', data, 0, 0x1111111111111111 if junk_vtable else 0)
    struct.pack_into('<I', data, 0x8, header)
    struct.pack_into('<QQ', data, 0x18, 0x2223, 0x3335)
    struct.pack_into('<i', data, 0x28, handle)
    c = {'schema': 1, 'address': ADDRESSES['delete'], 'id': cid, 'returns': 'void',
         'args': {'rdi': 'buf:directory'}, 'buffers': {'directory': {'size': 0x40, 'bytes': data.hex()}},
         'memory': [], 'imports': [], 'stubs': []}
    thread_allocator(c)
    if entries:
        c['buffers']['entries'] = {'size': 0x20}
        c['memory'].append({'addr': 'buf:directory+0x30', 'pointer': 'entries+0x10'})
    else:
        c['memory'].append({'addr': 'buf:directory+0x30', 'bytes': '00' * 8})
    if handle:
        c['imports'].append({'name': 'sceFiosDHCloseSync', 'argc': 1, 'argu32': [1], 'ret': hex(close_result)})
    if entries:
        c['stubs'].append({'address': FREE_HEADED, 'argc': 2})
    c['stubs'] += [{'address': STRING_RELEASE, 'argc': 1, 'argmem': [{'arg': 0, 'size': 8}]},
                   {'address': STRING_RELEASE, 'argc': 1, 'argmem': [{'arg': 0, 'size': 8}]},
                   {'address': FREE_SIZED_FN, 'argc': 3}]
    return c


def delete_cases():
    return [
        delete_case('open_with_entries'),
        delete_case('closed_no_entries', handle=0, entries=False),
        delete_case('open_no_entries', entries=False),
        delete_case('closed_with_entries', handle=0),
        delete_case('negative_handle', handle=-1),
        delete_case('close_fails', close_result=0x80010003),
        delete_case('recorded_size', header=0x00480001),
        delete_case('recorded_size_zero', header=0x00000001),
        delete_case('recorded_size_fffe', header=0xfffe0000),
        delete_case('count_bits_ignored', header=0xffffffff),
        delete_case('null_vtable_overwritten', junk_vtable=False),
    ]


def write(items, usage):
    """Write the cases to the directory named on the command line."""
    if len(sys.argv) != 2:
        print(usage, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for item in items:
        with open(os.path.join(sys.argv[1], f"{item['id']}.json"), 'w') as stream:
            json.dump(item, stream, indent=1)
    print(f'{len(items)} synthetic cases written to {sys.argv[1]}')
    return 0
