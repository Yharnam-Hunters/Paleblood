# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared synthetic edge cases for devices.cpp, written by pad_set_vibration_cases.py
(0x02a9c790), initialize_mouse_for_initial_user_cases.py (0x02a8c420),
mouse_b_open_for_initial_user_cases.py (0x02a8bf30) and mouse_construct_cases.py
(0x02a8c210).

The PS4 library calls are scripted; the vibration parameters are compared byte for byte, so the
order of the motors shows. The mouse opens run without an initial user, with and without a
previous handle (closed only when positive), and with sceMouseOpen giving positive, zero and
negative handles. The constructor's base constructor (0x02a8d2b0) and its virtual open (slot 32,
0x02a8c420) are stubbed. Generated fixtures, not inputs recorded in a game run.
"""
import json
import os
import struct
import sys

PAD_VIBRATION = '0x02a9c790'
MOUSE_OPEN = {'mouse': ('0x02a8c420', 0x168), 'mouse_b': ('0x02a8bf30', 0x128)}
CONSTRUCT = '0x02a8c210'
BASE_CONSTRUCT = '0x02a8d2b0'
VIRTUAL_OPEN = '0x02a8c420'


def pad_case(cid, is_open, small, large, result=0, handle=0x1234):
    pad = bytearray(b'\xd2' * 0x10)
    struct.pack_into('<iB', pad, 0x8, handle, is_open)
    c = {'schema': 1, 'address': PAD_VIBRATION, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:pad', 'rsi': hex(small), 'rdx': hex(large)},
         'buffers': {'pad': {'size': 0x10, 'bytes': pad.hex()}}, 'memory': [], 'imports': []}
    if is_open:
        c['imports'].append({'name': 'scePadSetVibration', 'argc': 0, 'argu32': [0],
                             'argmem': [{'arg': 1, 'size': 2}], 'ret': hex(result)})
    return c


def pad_cases():
    return [
        pad_case('closed', 0, 0x40, 0x80),
        pad_case('open', 1, 0x40, 0x80),
        pad_case('open_other_value', 0xfe, 0x01, 0xff),
        pad_case('motors_off', 1, 0, 0),
        pad_case('upper_register_bits_ignored', 1, 0xabcdef12345678a5, 0x1122334455667766),
        pad_case('fails', 1, 0x40, 0x80, result=0x80920003),
        pad_case('fails_positive', 1, 0x40, 0x80, result=1),
        pad_case('ok_upper_word_set', 1, 0x40, 0x80, result=0xffffffff00000000),
        pad_case('negative_handle', 1, 9, 10, handle=-1),
    ]


def mouse_open_case(kind, cid, user_result=0, user=0x10000001, previous=0, opened=3):
    address, handle_offset = MOUSE_OPEN[kind]
    device = bytearray(b'\x6e' * (handle_offset + 0x10))
    struct.pack_into('<i', device, handle_offset, previous)
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'i8', 'args': {'rdi': 'buf:device'},
         'buffers': {'device': {'size': len(device), 'bytes': device.hex()}}, 'memory': [],
         'imports': [{'name': 'sceUserServiceGetInitialUser', 'argc': 1, 'ret': hex(user_result),
                      'writes': [{'arg': 0, 'size': 4, 'bytes': struct.pack('<i', user).hex()}]}]}
    if user_result & 0xffffffff == 0:
        if previous > 0:
            c['imports'].append({'name': 'sceMouseClose', 'argc': 0, 'argu32': [0], 'ret': 0})
        c['imports'].append({'name': 'sceMouseOpen', 'argc': 0, 'argu32': [0, 1, 2], 'argi': [3],
                             'ret': hex(opened & 0xffffffffffffffff)})
    return c


def mouse_open_cases(kind):
    return [
        mouse_open_case(kind, 'no_initial_user', user_result=0x80960007),
        mouse_open_case(kind, 'no_initial_user_positive', user_result=1),
        mouse_open_case(kind, 'first_open', previous=0),
        mouse_open_case(kind, 'reopen', previous=5),
        mouse_open_case(kind, 'negative_previous_not_closed', previous=-2),
        mouse_open_case(kind, 'open_gives_zero', opened=0),
        mouse_open_case(kind, 'open_fails', previous=7, opened=0x80df0002),
        mouse_open_case(kind, 'open_gives_large_handle', opened=0x7fffffff),
        mouse_open_case(kind, 'open_result_upper_bits', opened=0xffffffff00000004),
        mouse_open_case(kind, 'user_result_upper_bits', user_result=0xffffffff00000000),
        mouse_open_case(kind, 'user_minus_one', user=-1),
    ]


def construct_case(cid, load=0, init=0, opened=1, open_handle=None):
    c = {'schema': 1, 'address': CONSTRUCT, 'id': cid, 'returns': 'void',
         'args': {'rdi': 'buf:device', 'rsi': '0x5151', 'rdx': '0x7272'},
         'buffers': {'device': {'size': 0x180, 'bytes': '3a' * 0x180}}, 'memory': [], 'imports': [],
         'stubs': [{'address': BASE_CONSTRUCT, 'argc': 2}]}
    c['imports'].append({'name': 'sceSysmoduleLoadModule', 'argc': 0, 'argu32': [0], 'ret': hex(load)})
    if load & 0xffffffff == 0:
        c['imports'].append({'name': 'sceMouseInit', 'argc': 0, 'ret': hex(init)})
        if init & 0xffffffff == 0:
            virtual_open = {'address': VIRTUAL_OPEN, 'argc': 1, 'ret': hex(opened)}
            if open_handle is not None:   # what the real open stores before it answers
                virtual_open['writes'] = [{'arg': 0, 'offset': 0x168, 'bytes': struct.pack('<i', open_handle).hex()}]
            c['stubs'].append(virtual_open)
    return c


def construct_cases():
    return [
        construct_case('all_ok'),
        construct_case('open_fails', opened=0),
        construct_case('open_false_upper_bits_only', opened=0xff00),
        construct_case('open_byte_true', opened=0x101),
        construct_case('load_fails', load=0x80020009),
        construct_case('load_fails_positive', load=1),
        construct_case('init_fails', init=0x80df0001),
        construct_case('load_ok_upper_word', load=0xffffffff00000000),
        construct_case('open_stores_handle', open_handle=9),
        construct_case('open_stores_handle_then_fails', opened=0, open_handle=-5),
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
