#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/destroy_then_delete_families.cpp's eight families, which
family_cases.py runs on every member at its own address. Each family is the copies of one deleting
destructor; its destructor and operator delete (0x02fbe588) are scripted stubs comparing the object
they are given, and the calls are compared in order. Generated fixtures, not game data.

Objects: a buffer, a pointer 8 bytes into one, null and a large value. The destructor's stub
returns a value the members do not use (the result is void), and in one case it writes the object,
which is passed on to operator delete unread.
"""
from __future__ import annotations

OPERATOR_DELETE = '0x02fbe588'
OBJECT_SIZE = 0x40


def _pattern(size: int, seed: int) -> str:
    return bytes((i * 13 + seed) & 0xff for i in range(size)).hex()


def _case(address: str, cid: str, destroy: str, target: str, writes: list | None = None) -> dict:
    destroy_stub = {'address': destroy, 'argc': 1, 'argi': [0], 'ret': '0x5a5a5a5a00000002'}
    delete_stub = {'address': OPERATOR_DELETE, 'argc': 1, 'argi': [0]}
    if writes:
        # operator delete sees the object as the destructor left it
        destroy_stub['writes'] = writes
        delete_stub['argmem'] = [{'arg': 0, 'size': OBJECT_SIZE}]
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': {'rdi': target},
            'buffers': {'object': {'size': OBJECT_SIZE, 'bytes': _pattern(OBJECT_SIZE, 0x0b)}},
            'memory': [], 'imports': [], 'stubs': [destroy_stub, delete_stub]}


def deleting_cases(destroy: str):
    """The cases of the family whose members call the destructor at destroy."""
    def cases(address: str, value: int | None) -> list[dict]:
        return [_case(address, 'object', destroy, 'buf:object'),
                _case(address, 'interior_pointer', destroy, 'buf:object+0x8'),
                _case(address, 'null', destroy, '0x0'),
                _case(address, 'large_value', destroy, '0xfedcba9876543210'),
                _case(address, 'destructor_writes_object', destroy, 'buf:object',
                      [{'arg': 0, 'bytes': '00' * 8}])]
    return cases
