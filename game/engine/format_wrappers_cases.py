#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for DLFormat's entry points (game/engine/format_wrappers.cpp), for tools/verify.py run.

usage: format_wrappers_cases.py OUT_DIR MODE

MODE: wide|bytes|wide_string|allocated|wide_unbounded|wide_list_unbounded|bytes_list_unbounded|
wide_string_variadic

The two format drivers are scripted stubs: their results are chosen (negative, zero, small,
large) and their arguments compared, including the va_list (the caller's own buffer for the
va_list forms; for the variadic forms its gp_offset / fp_offset, since its pointers lie in each
side's own stack frame). The wide string's resize is a stub whose write may move the text to the
heap; the allocator is a fake object whose allocate slot is a stub returning a buffer or null.
"""
import json
import os
import struct
import sys

WIDE, BYTES, WIDE_STRING, ALLOCATED = '0x024e3ea0', '0x024e33b0', '0x024e4130', '0x024e3460'
WIDE_UNBOUNDED, WIDE_LIST_UNBOUNDED, BYTES_LIST_UNBOUNDED = '0x024e3f50', '0x024e3e80', '0x024e3390'
WIDE_STRING_VARIADIC = '0x024e4010'
MODES = ('wide', 'bytes', 'wide_string', 'allocated', 'wide_unbounded', 'wide_list_unbounded',
         'bytes_list_unbounded', 'wide_string_variadic')
FORMAT_BYTES, FORMAT_WIDE, RESIZE = '0x024e4240', '0x024e59f0', '0x02a2a650'
ALLOCATE = 0x00500300
MAX64 = 0xffffffffffffffff
RESULTS = [-1, 0, 1, 2, 7, 8, 100, 0x7ffffffe, -0x80000000]


def u64(value):
    return struct.pack('<Q', value & MAX64).hex()


def base(address, cid, returns):
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns, 'args': {},
            'buffers': {'out': {'size': 0x40, 'bytes': 'ab' * 0x40},
                        'format': {'size': 0x20, 'bytes': '25006400' + '5a' * 0x1c},
                        'va': {'size': 24, 'bytes': '1000000030000000' + '00' * 16},
                        'regs': {'size': 0x30, 'bytes': '11' * 0x30},
                        'stack': {'size': 0x10, 'bytes': '22' * 0x10}},
            'memory': [{'addr': 'buf:va+8', 'pointer': 'stack'}, {'addr': 'buf:va+16', 'pointer': 'regs'}],
            'imports': [], 'stubs': []}


def variadic(address, driver, cid, room, result, out=True, extra=()):
    case = base(address, cid, 'i32')
    case['args'] = {'rdi': 'buf:out' if out else '0x0', 'rsi': hex(room & MAX64), 'rdx': 'buf:format'}
    for register, value in zip(('rcx', 'r8', 'r9'), extra):
        case['args'][register] = hex(value)
    case['stubs'].append({'address': driver, 'argc': 4, 'argi': [0, 1, 2], 'ret': hex(result & MAX64),
                          'argmem': [{'arg': 3, 'size': 8}]})
    return case


def wide_string(cid, measured, written=5, capacity=7, to_heap=False, variadic_form=False):
    case = base(WIDE_STRING_VARIADIC if variadic_form else WIDE_STRING, cid, 'i32')
    text = bytearray(b'\x3c' * 0x38)
    struct.pack_into('<QQ', text, 0x18, 3, capacity)
    case['buffers']['text'] = {'size': 0x38, 'bytes': text.hex()}
    case['buffers']['heap'] = {'size': 0x40, 'bytes': '66' * 0x40}
    if capacity >= 8:
        case['memory'].append({'addr': 'buf:text+8', 'pointer': 'heap'})
    case['args'] = {'rdi': 'buf:text', 'rsi': 'buf:format'}
    driver = {'address': FORMAT_WIDE, 'argc': 4}
    if variadic_form:
        driver.update({'argi': [0, 1, 2], 'argmem': [{'arg': 3, 'size': 8}]})
        case['args'].update({'rdx': '0x5151', 'rcx': 'buf:out'})
    else:
        case['args']['rdx'] = 'buf:va'
    case['stubs'].append({**driver, 'ret': hex(measured & MAX64)})
    resize = {'address': RESIZE, 'argc': 3, 'ret': '0x0'}
    if to_heap:
        resize['writes'] = [{'arg': 0, 'offset': 8, 'pointer': 'heap'},
                            {'arg': 0, 'offset': 0x20, 'bytes': u64(0x20)}]
    case['stubs'].append(resize)
    if measured > 0:
        case['stubs'].append({**driver, 'ret': hex(written & MAX64)})
    return case


def unbounded(address, driver, cid, result, out=True, list_form=False):
    case = base(address, cid, 'i32')
    case['args'] = {'rdi': 'buf:out' if out else '0x0', 'rsi': 'buf:format'}
    stub = {'address': driver, 'argc': 4, 'ret': hex(result & MAX64)}
    if list_form:
        case['args']['rdx'] = 'buf:va'
    else:
        case['args'].update({'rdx': '0x41', 'rcx': hex(MAX64), 'r8': 'buf:out'})
        stub.update({'argi': [0, 1, 2], 'argmem': [{'arg': 3, 'size': 8}]})
    case['stubs'].append(stub)
    return case


def allocated(cid, measured, buffer=True, written=9):
    case = base(ALLOCATED, cid, 'i64')
    case['buffers']['allocator'] = {'size': 0x10, 'bytes': '44' * 0x10}
    case['buffers']['vtable'] = {'size': 0x78, 'bytes': 'c3' * 0x78}
    case['buffers']['text'] = {'size': 0x20, 'bytes': '77' * 0x20}
    case['memory'] += [{'addr': 'buf:allocator', 'pointer': 'vtable'},
                       {'addr': 'buf:vtable+0x50', 'guest': hex(ALLOCATE)}]
    case['args'] = {'rdi': 'buf:allocator', 'rsi': 'buf:format', 'rdx': 'buf:va'}
    case['stubs'].append({'address': FORMAT_BYTES, 'argc': 4, 'ret': hex(measured & MAX64)})
    case['stubs'].append({'address': hex(ALLOCATE), 'argc': 2, 'ret': 'buf:text' if buffer else '0x0'})
    if buffer and measured > 0:
        case['stubs'].append({'address': FORMAT_BYTES, 'argc': 4, 'ret': hex(written & MAX64)})
    return case


def make(which):
    out = []
    if which in ('wide', 'bytes'):
        address, driver = (WIDE, FORMAT_WIDE) if which == 'wide' else (BYTES, FORMAT_BYTES)
        for result in RESULTS:
            for room in (0, 1, 16, 0x7fffffff, 0x100000000, MAX64):
                out.append(variadic(address, driver, f'r{result & 0xffffffff:x}_room{room:x}', room, result))
            out.append(variadic(address, driver, f'r{result & 0xffffffff:x}_measure', 0x7fffffff, result, out=False))
            out.append(variadic(address, driver, f'r{result & 0xffffffff:x}_args', 16, result,
                                extra=(0x41, 0x1234, MAX64)))
    elif which in ('wide_unbounded', 'wide_list_unbounded', 'bytes_list_unbounded'):
        address, driver = {'wide_unbounded': (WIDE_UNBOUNDED, FORMAT_WIDE),
                           'wide_list_unbounded': (WIDE_LIST_UNBOUNDED, FORMAT_WIDE),
                           'bytes_list_unbounded': (BYTES_LIST_UNBOUNDED, FORMAT_BYTES)}[which]
        for result in RESULTS:
            for out_given in (True, False):
                out.append(unbounded(address, driver, f'r{result & 0xffffffff:x}_out{int(out_given)}', result,
                                     out=out_given, list_form=which != 'wide_unbounded'))
    elif which in ('wide_string', 'wide_string_variadic'):
        variadic_form = which == 'wide_string_variadic'
        for measured in RESULTS:
            m = f'm{measured & 0xffffffff:x}'
            for capacity in (0, 7, 8, 0x40):
                out.append(wide_string(f'{m}_cap{capacity:x}', measured, capacity=capacity,
                                       variadic_form=variadic_form))
            out.append(wide_string(f'{m}_to_heap', measured, to_heap=True, variadic_form=variadic_form))
            out.append(wide_string(f'{m}_written_neg', measured, written=-1, capacity=8,
                                   variadic_form=variadic_form))
    else:
        for measured in RESULTS:
            out.append(allocated(f'm{measured & 0xffffffff:x}', measured))
            out.append(allocated(f'm{measured & 0xffffffff:x}_no_buffer', measured, buffer=False))
            out.append(allocated(f'm{measured & 0xffffffff:x}_written_neg', measured, written=MAX64))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in MODES:
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for index, item in enumerate(make(sys.argv[2])):
        with open(os.path.join(sys.argv[1], f'{index:03d}_{item["id"]}.json'), 'w') as handle:
            json.dump(item, handle, indent=1)


if __name__ == '__main__':
    main()
