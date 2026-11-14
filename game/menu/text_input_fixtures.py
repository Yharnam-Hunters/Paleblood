#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases for the text input dialog (game/menu/text_input_dialog.cpp). Each function
has its own generator, <name without menu_>_cases.py, which calls write() here.

The dialog object is a buffer: Dantelion's dialog object (never read here), the state, the result
and the wide string. Dantelion's calls (status, take text, set up, open, destroy) are stubbed with
the status a case needs; the take-text stub also writes text in some cases, so the escaping is
seen to run after it. The string's allocator is a fake object whose free slot points at a stubbed
game function. All text is invented.
"""
import json
import os
import struct
import sys

STATUS, TAKE_TEXT, SET_UP, OPEN, DESTROY = '0x02d03420', '0x02d03490', '0x02d03500', '0x02d03450', '0x02d03410'
# Any game function will do as the free slot's target: it is stubbed, so it never runs.
FREE = '0x0247c460'

STATE, RESULT, TEXT = 0xd0, 0xd4, 0xd8
TEXT_DATA, TEXT_SIZE, TEXT_CAPACITY, TEXT_ALLOCATOR = TEXT + 0x08, TEXT + 0x18, TEXT + 0x20, TEXT + 0x28
DIALOG_SIZE = TEXT + 0x38
NOT_OPEN, RUNNING, CANCELED, ENTERED = -1, 0, 2, 3
IDLE, OPENED, FINISHED = 0, 1, 2

FUNCTIONS = {
    'menu_text_input_close': ('0x02416040', 'close', 'i8'),
    'menu_text_input_update': ('0x02416300', 'update', 'i8'),
    'menu_text_input_open': ('0x024162a0', 'open', 'i8'),
    'menu_text_input_destroy': ('0x02415fe0', 'destroy', 'void'),
}

# Every escaped character, its neighbours, a glyph itself and plain text.
EVERY_CHARACTER = '#%<>?\\¥' + '"$;=@[]¤¦' + '⑲￿' + 'Hunter 7'
INLINE = 'a#%<b'


def u16s(text):
    return text.encode('utf-16-le').hex()


def dialog(state=OPENED, result=0, text='', heap=False, size=None, capacity=None, filler=0x5a):
    """The dialog buffer and the memory entries that point it at its text and allocator."""
    data = bytearray([filler]) * DIALOG_SIZE
    struct.pack_into('<ii', data, STATE, state, result)
    length = len(text) if size is None else size
    struct.pack_into('<q', data, TEXT_SIZE, length)
    if heap:
        struct.pack_into('<Q', data, TEXT_CAPACITY, 31 if capacity is None else capacity)
    else:
        struct.pack_into('<Q', data, TEXT_CAPACITY, 7 if capacity is None else capacity)
        encoded = text.encode('utf-16-le')[:16]
        data[TEXT_DATA:TEXT_DATA + len(encoded)] = encoded
    buffers = {
        'dlg': {'size': DIALOG_SIZE, 'bytes': data.hex()},
        'heap': {'size': 0x80, 'bytes': (u16s(text) if heap else '') + 'cd' * 0x80},
        'alloc': {'size': 0x10},
        'vtable': {'size': 0x80},
        'settings': {'size': 0xc8, 'bytes': '77' * 0xc8},
    }
    buffers['heap']['bytes'] = buffers['heap']['bytes'][:0x100]
    memory = [{'addr': f'buf:dlg+{TEXT_ALLOCATOR}', 'pointer': 'alloc'},
              {'addr': 'buf:alloc+0', 'pointer': 'vtable'},
              {'addr': 'buf:vtable+112', 'guest': FREE}]
    if heap:
        memory.append({'addr': f'buf:dlg+{TEXT_DATA}', 'pointer': 'heap'})
    return buffers, memory


def stub(address, ret=0, argc=1, writes=None):
    s = {'address': address, 'ret': ret, 'argc': argc}
    if writes:
        s['writes'] = writes
    return s


def finished(status, text_written=None):
    """The status call, and for a finished dialog the take-text call (writing `text_written`
    inline, with its size, when given)."""
    stubs = [stub(STATUS, status)]
    if status in (CANCELED, ENTERED):
        writes = None
        if text_written is not None:
            writes = [{'arg': 1, 'offset': 0x08, 'bytes': u16s(text_written)},
                      {'arg': 1, 'offset': 0x18, 'bytes': struct.pack('<q', len(text_written)).hex()}]
        stubs.append(stub(TAKE_TEXT, 1, 2, writes))
    return stubs


def case(address, returns, cid, d, stubs, args=None):
    buffers, memory = d
    return {'schema': 1, 'address': address, 'id': cid, 'returns': returns,
            'args': args or {'rdi': 'buf:dlg'}, 'buffers': buffers, 'memory': memory,
            'stubs': stubs, 'imports': []}


def poll_cases(address, returns, closes):
    """Cases shared by close and update (both poll the dialog first)."""
    c = lambda cid, d, stubs: case(address, returns, cid, d, stubs)
    cases = [
        c('not_open_idle', dialog(state=IDLE, text=INLINE), []),
        c('already_finished', dialog(state=FINISHED, result=2, text=INLINE), []),
        c('state_minus_one', dialog(state=-1, text=INLINE), []),
        c('running', dialog(text=INLINE), finished(RUNNING)),
        c('canceled_inline', dialog(text=INLINE), finished(CANCELED)),
        c('entered_inline', dialog(text=INLINE), finished(ENTERED)),
        c('entered_every_character_heap', dialog(text=EVERY_CHARACTER, heap=True), finished(ENTERED)),
        c('canceled_every_character_heap', dialog(text=EVERY_CHARACTER, heap=True), finished(CANCELED)),
        c('entered_text_written_by_take', dialog(text='zz'), finished(ENTERED, '?<x>\\')),
        c('entered_seven_inline', dialog(text='#######'), finished(ENTERED)),
        c('entered_empty', dialog(text=''), finished(ENTERED)),
        c('entered_negative_size', dialog(text=INLINE, size=-2), finished(ENTERED)),
        c('entered_capacity_eight_is_heap', dialog(text=EVERY_CHARACTER[:8], heap=True, capacity=8), finished(ENTERED)),
        c('status_not_open', dialog(text=INLINE), finished(NOT_OPEN)),
        c('status_one', dialog(text=INLINE), finished(1)),
        c('status_four', dialog(text=INLINE, heap=True), finished(4)),
        c('status_large', dialog(text=INLINE), finished(0x7fffffff)),
        c('zero_filler', dialog(text=INLINE, filler=0), finished(ENTERED)),
    ]
    return cases


def open_cases(address, returns):
    def c(cid, d, stubs):
        return case(address, returns, cid, d, stubs, {'rdi': 'buf:dlg', 'rsi': 'buf:settings'})
    idle = lambda: dialog(state=IDLE, text=INLINE)
    return [
        c('opens', idle(), [stub(STATUS, NOT_OPEN), stub(SET_UP, 1, 2), stub(OPEN, 1)]),
        c('status_minus_two_opens', idle(), [stub(STATUS, -2), stub(SET_UP, 1, 2), stub(OPEN, 1)]),
        c('system_dialog_running', idle(), [stub(STATUS, RUNNING)]),
        # Set up and open would succeed, so only the status check keeps it from opening.
        c('system_dialog_running_would_open', idle(), [stub(STATUS, RUNNING), stub(SET_UP, 1, 2), stub(OPEN, 1)]),
        c('system_dialog_entered', idle(), [stub(STATUS, ENTERED)]),
        c('set_up_refused', idle(), [stub(STATUS, NOT_OPEN), stub(SET_UP, 0, 2)]),
        c('set_up_returns_high_bits', idle(), [stub(STATUS, NOT_OPEN), stub(SET_UP, 0x100, 2)]),
        c('open_refused', idle(), [stub(STATUS, NOT_OPEN), stub(SET_UP, 1, 2), stub(OPEN, 0)]),
        c('earlier_dialog_running', dialog(text=INLINE), [stub(STATUS, RUNNING)]),
        c('earlier_dialog_entered_then_opens', dialog(text=EVERY_CHARACTER, heap=True),
          finished(ENTERED) + [stub(STATUS, NOT_OPEN), stub(SET_UP, 1, 2), stub(OPEN, 1)]),
        c('earlier_dialog_canceled_system_still_open', dialog(text=INLINE),
          finished(CANCELED) + [stub(STATUS, 0)]),
        c('finished_state_opens', dialog(state=FINISHED, result=1, text=INLINE),
          [stub(STATUS, NOT_OPEN), stub(SET_UP, 1, 2), stub(OPEN, 1)]),
    ]


def destroy_cases(address, returns):
    c = lambda cid, d, stubs: case(address, returns, cid, d, stubs)
    free = stub(FREE, 0, 2)
    return [
        c('idle_inline', dialog(state=IDLE, text=INLINE), [stub(DESTROY)]),
        c('idle_heap', dialog(state=IDLE, text=EVERY_CHARACTER, heap=True), [free, stub(DESTROY)]),
        c('idle_capacity_eight', dialog(state=IDLE, text='12345678', heap=True, capacity=8), [free, stub(DESTROY)]),
        c('open_running_inline', dialog(text=INLINE), finished(RUNNING) + [stub(DESTROY)]),
        c('open_running_heap', dialog(text=EVERY_CHARACTER, heap=True), finished(RUNNING) + [free, stub(DESTROY)]),
        c('open_entered_heap', dialog(text=EVERY_CHARACTER, heap=True), finished(ENTERED) + [free, stub(DESTROY)]),
        c('open_canceled_inline', dialog(text=INLINE), finished(CANCELED) + [stub(DESTROY)]),
        c('finished_heap', dialog(state=FINISHED, result=2, text=EVERY_CHARACTER, heap=True), [free, stub(DESTROY)]),
    ]


def write(name, doc):
    if len(sys.argv) != 2:
        sys.exit(doc)
    address, kind, returns = FUNCTIONS[name]
    cases = {
        'close': lambda: poll_cases(address, returns, True),
        'update': lambda: poll_cases(address, returns, False),
        'open': lambda: open_cases(address, returns),
        'destroy': lambda: destroy_cases(address, returns),
    }[kind]()
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    for c in cases:
        with open(os.path.join(target, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases in {target}')
    return 0
