# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared synthetic edge cases for kernel_file.cpp, written by kernel_file_open_cases.py
(0x02a70890), kernel_file_read_cases.py (0x02a70920), kernel_file_write_cases.py (0x02a70950),
kernel_file_seek_cases.py (0x02a70980) and kernel_file_close_cases.py (0x02a709c0).

The kernel calls are scripted. Open: every combination of the six mode bits the function reads
plus unrelated bits, and kernel results that are zero, positive, negative and positive only in
the low 32 bits. Read and write: counts zero, positive, negative, the largest positive and a
value whose low 32 bits look negative, with and without a place for the count, and descriptors
with the sign bit set. Seek: the same results as positions, each origin the kernel knows and
ones it does not, an origin with bits above the low 32, and offsets that are zero, negative and
the largest. Close: kernel results that are zero and errors, flag bytes with the open bit set
and clear beside other bits, and descriptors that are zero, negative and the largest. The
descriptor, the flags and the origin are int-typed, so only their low 32 bits are compared. The
file object's other bytes start at a fill pattern so stray writes show. Generated fixtures, not
inputs recorded in a game run.
"""
import json
import os
import struct
import sys

OPEN_ADDRESS = '0x02a70890'
READ_ADDRESS = '0x02a70920'
WRITE_ADDRESS = '0x02a70950'
SEEK_ADDRESS = '0x02a70980'
CLOSE_ADDRESS = '0x02a709c0'
# Kernel results that read, write and seek share: a name and the 64-bit value returned.
RESULTS = (('zero', 0), ('short', 7), ('full', 0x40), ('low_word_negative', 0x80000000),
           ('largest', (1 << 63) - 1), ('minus_one', (1 << 64) - 1), ('int64_min', 1 << 63),
           ('negative_sce_code', 0xffffffff80020009))
MODE_BITS = (0x01, 0x02, 0x08, 0x10, 0x40, 0x80)
PATH = b'/app0/dvdroot_ps4/param.sfo\0'


def file_bytes(descriptor=0x5a5a5a5a, flags=0xa4):
    return struct.pack('<iB', descriptor, flags).hex() + 'c3' * 11


def open_case(cid, mode, result=3):
    return {'schema': 1, 'address': OPEN_ADDRESS, 'id': cid, 'returns': 'i32',
            'args': {'rdi': 'buf:file', 'rsi': 'buf:path', 'rdx': hex(mode)},
            'buffers': {'file': {'size': 16, 'bytes': file_bytes()}, 'path': {'size': len(PATH), 'bytes': PATH.hex()}},
            'memory': [], 'imports': [{'name': 'sceKernelOpen', 'argc': 1, 'argu32': [1, 2], 'ret': hex(result)}]}


def open_cases():
    out = []
    for combination in range(1 << len(MODE_BITS)):
        mode = sum(bit for n, bit in enumerate(MODE_BITS) if combination >> n & 1)
        out.append(open_case(f'mode_{mode:02x}', mode))
    out += [
        open_case('unrelated_bits_ignored', 0xffffff24 | 0x01),
        open_case('upper_register_bits_ignored', 0xabcdef0100000003),
        open_case('descriptor_zero', 0x03, result=0),
        open_case('descriptor_large', 0x03, result=0x7fffffff),
        open_case('error_minus_one', 0x03, result=(1 << 64) - 1),
        open_case('error_sce_code', 0x42, result=0x80020002),
        open_case('error_int32_min', 0x01, result=0x80000000),
        open_case('low_word_positive', 0x01, result=0xffffffff00000007),
    ]
    return out


def read_case(cid, result, count_place=True, descriptor=9, size=0x40):
    c = {'schema': 1, 'address': READ_ADDRESS, 'id': cid, 'returns': 'i32',
         'args': {'rdi': 'buf:file', 'rsi': 'buf:data+0x8', 'rdx': hex(size),
                  'rcx': 'buf:count+0x8' if count_place else '0x0'},
         'buffers': {'file': {'size': 16, 'bytes': file_bytes(descriptor)}, 'data': {'size': 0x50, 'bytes': '77' * 0x50},
                     'count': {'size': 0x18, 'bytes': '99' * 0x18}},
         'memory': [], 'imports': [{'name': 'sceKernelRead', 'argc': 3, 'argi': [1, 2], 'argu32': [0], 'ret': hex(result)}]}
    if result < 1 << 63 and result:
        c['imports'][0]['writes'] = [{'arg': 1, 'bytes': bytes(range(1, 1 + min(result, 16))).hex()}]
    return c


def read_cases():
    out = []
    for name, result in RESULTS:
        out.append(read_case(f'{name}_with_count', result))
        out.append(read_case(f'{name}_without_count', result, count_place=False))
    out += [
        read_case('negative_descriptor', 4, descriptor=-1),
        read_case('zero_size', 0, size=0),
        read_case('huge_size_passed_on', 3, size=(1 << 64) - 1),
    ]
    return out


def write_case(cid, result, count_place=True, descriptor=9, size=0x40, source='buf:data+0x8'):
    return {'schema': 1, 'address': WRITE_ADDRESS, 'id': cid, 'returns': 'i32',
            'args': {'rdi': 'buf:file', 'rsi': source, 'rdx': hex(size),
                     'rcx': 'buf:count+0x8' if count_place else '0x0'},
            'buffers': {'file': {'size': 16, 'bytes': file_bytes(descriptor)},
                        'data': {'size': 0x50, 'bytes': bytes(range(0x50)).hex()},
                        'count': {'size': 0x18, 'bytes': '99' * 0x18}},
            'memory': [], 'imports': [{'name': 'sceKernelWrite', 'argc': 3, 'argi': [1, 2], 'argu32': [0], 'ret': hex(result)}]}


def write_cases():
    out = []
    for name, result in RESULTS:
        out.append(write_case(f'{name}_with_count', result))
        out.append(write_case(f'{name}_without_count', result, count_place=False))
    out += [
        write_case('negative_descriptor', 4, descriptor=-1),
        write_case('zero_size', 0, size=0),
        write_case('huge_size_passed_on', 3, size=(1 << 64) - 1),
        write_case('no_source_passed_on', 0, size=0, source='0x0'),
    ]
    return out


def seek_case(cid, result, position_place=True, descriptor=9, origin=0, offset=0x1000):
    return {'schema': 1, 'address': SEEK_ADDRESS, 'id': cid, 'returns': 'i32',
            'args': {'rdi': 'buf:file', 'rsi': hex(origin), 'rdx': hex(offset),
                     'rcx': 'buf:position+0x8' if position_place else '0x0'},
            'buffers': {'file': {'size': 16, 'bytes': file_bytes(descriptor)},
                        'position': {'size': 0x18, 'bytes': '99' * 0x18}},
            'memory': [], 'imports': [{'name': 'sceKernelLseek', 'argc': 3, 'argi': [1], 'argu32': [0, 2], 'ret': hex(result)}]}


def seek_cases():
    out = []
    for name, result in RESULTS:
        out.append(seek_case(f'{name}_with_position', result))
        out.append(seek_case(f'{name}_without_position', result, position_place=False))
    out += [
        seek_case('from_current', 0x1010, origin=1, offset=0x10),
        seek_case('from_end', 0x38, origin=2, offset=(1 << 64) - 8),
        seek_case('unknown_origin_passed_on', 5, origin=7),
        seek_case('origin_sign_bit', 5, origin=0x80000000),
        seek_case('upper_register_bits_of_origin_ignored', 5, origin=0xabcdef0100000002),
        seek_case('zero_offset', 0, offset=0),
        seek_case('largest_offset', (1 << 63) - 1, offset=(1 << 63) - 1),
        seek_case('negative_offset_passed_on', (1 << 64) - 1, offset=1 << 63),
        seek_case('negative_descriptor', 4, descriptor=-1),
    ]
    return out


def close_case(cid, descriptor=9, flags=0xa5, result=0):
    return {'schema': 1, 'address': CLOSE_ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:file'},
            'buffers': {'file': {'size': 16, 'bytes': file_bytes(descriptor, flags)}},
            'memory': [], 'imports': [{'name': 'sceKernelClose', 'argc': 0, 'argu32': [0], 'ret': hex(result)}]}


def close_cases():
    return [
        close_case('open_with_other_bits'),
        close_case('only_open_bit', flags=0x01),
        close_case('every_bit', flags=0xff),
        close_case('not_open_still_closes', flags=0xa4),
        close_case('no_bits_still_closes', flags=0x00),
        close_case('descriptor_zero', descriptor=0),
        close_case('descriptor_already_none', descriptor=-1),
        close_case('descriptor_largest', descriptor=0x7fffffff),
        close_case('descriptor_int32_min', descriptor=-(1 << 31)),
        close_case('error_minus_one_ignored', result=(1 << 64) - 1),
        close_case('error_sce_code_ignored', result=0x80020009),
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
