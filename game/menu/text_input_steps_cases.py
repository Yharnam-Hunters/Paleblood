#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the text input steps (game/menu/text_input_steps.cpp), for tools/verify.py run.

usage: text_input_steps_cases.py OUT_DIR take|status|open|fonts|wait

take: the entered-text helper (0x01b6dc50) is a stub that fills the step's text object (a cleared
word, then a wide string: inline text with capacity 7 or less, or a heap buffer with capacity 8
or more, and its allocator). The step's callback (+0x40) is an invented target whose call slot is
a stub compared with the target and the text it gets (0x39 bytes: the 7 after the string's
owns byte are padding the helper never writes, so they hold whatever the stack held); the allocator's free slot
is a stub compared with the allocator and the buffer. The callback may also change the text (take
the buffer, leaving capacity 7, or swap in another buffer), so the release reads it again. A step
with no callback throws in the game (no return) and is not cased. The stack guard slot points at
an invented guard.

status: the menu state global (0x05962878) points at an invented menu state. Its running byte
(+0x18d8) is clear or any nonzero value (1, 2, 0x80, 0xff all count as running), and the dialog's
result (+0x189c) is each recorded value (none, canceled, entered) and values with upper bits set
(0x102, 0x80000002, -1), so only the whole 32-bit value 2 counts as entered. The dialog's state
(+0x1898) takes every value too, which the step must ignore.

open: the same invented menu state. The dialog's close (0x02416040), Dantelion's status
(0x02d03420), set-up (0x02d03500) and open (0x02d03450) are stubs compared with the dialog (and
the step's settings for the set-up), answering in turn: each step failing on its own (false, and
answers whose low byte is 0 with upper bits set, since only the low byte counts; a status of 0,
2, 3, INT_MAX or with only upper bits set, all 0 or more in 32 bits), and opening (a status of -1
or INT_MIN, true answers with upper junk). One case checks that the running byte goes through
the menu state global read again: its settings pointer is the global itself, so the set-up's stub
stores a second menu state there; the dialog's state is still written in the first one.

fonts: the SprjScaleform global (0x05940368) holds an invented singleton with a player (+8) or a
null one; the font load (0x0235ca90) is a stub compared with the player pointer. A missing
singleton is reported (the fatal error, a stub compared with its file, line, message and name);
the report's stub stores a singleton into the global through its name argument, so the second
read finds one (with or without a player). A singleton left missing is not a case: the second
read is null too and both sides fault reading its player.

wait: the localize request byte (0x05973810) starts at every value class: clear, 1, and other
nonzero bytes (2, 0x7f, 0x80, 0xfe, 0xff), which the step must also treat as pending. The bytes
next to it are set too, so a wider read or any write shows up.

Every case also scripts one spare call per callee, so an unexpected call is recorded instead of
running the real function. The step object is a buffer the steps must not touch. Generated fixtures, not inputs recorded in
a game run.
"""
import json
import os
import struct
import sys

ADDRESSES = {'take': '0x02034990', 'status': '0x02034b50', 'open': '0x02034c70', 'fonts': '0x02034e00',
             'wait': '0x02034f40'}

OPEN_CLOSE, OPEN_STATUS, OPEN_SET_UP, OPEN_OPEN = '0x02416040', '0x02d03420', '0x02d03500', '0x02d03450'
NOT_OPEN = 0xffffffff   # the system dialog's status when it is not open (-1)

GUARD_GOT, TAKE_TEXT = '0x057e3be0', '0x01b6dc50'
CALLBACK_CALL, ALLOCATOR_FREE = '0x00400140', '0x00400100'   # invented slots, stubbed
TEXT_COMPARED = 0x39   # up to the string's owns byte: the 7 bytes after it are padding the helper never writes
# The wide string's fields inside the text object (the string starts at +8).
TEXT_HEAP, TEXT_SIZE, TEXT_CAPACITY, TEXT_ALLOCATOR, TEXT_OWNS = 0x10, 0x20, 0x28, 0x30, 0x38

MENU_STATE = '0x05962878'
MENU_STATE_SIZE = 0x1900
DIALOG_STATE, DIALOG_RESULT, TEXT_INPUT_RUNNING = 0x1898, 0x189c, 0x18d8
RUNNING_VALUES = (0x01, 0x02, 0x80, 0xff)
RESULTS = (0, 1, 2, 3, 0x102, 0x80000002, 0xffffffff)
STATES = (0, 1, 2)

SPRJ_SCALEFORM, SPRJ_SCALEFORM_NAME = 0x05940368, 0x04d3b2b3
SINGLETON_FILE, SINGLETON_MESSAGE, SINGLETON_LINE = '0x04d3b369', '0x04d3b3bd', 0xb1
FATAL_ERROR, LOAD_FONTS = '0x024b55b0', '0x0235ca90'

REQUEST_BYTE = 0x05973810
FLAG_VALUES = (0x00, 0x01, 0x02, 0x7f, 0x80, 0xfe, 0xff)
NEIGHBOURS = ('00', 'a5', 'ff')


def case(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'i32',
            'buffers': {'step': {'size': 0x50, 'bytes': '3c' * 0x50}},
            'args': {'rdi': 'buf:step'}, 'memory': [], 'imports': [], 'stubs': []}


def spares(c, callees):
    """One more scripted call per callee, so an unexpected call is recorded instead of running
    the real function (as in scaleform_glue_cases.py)."""
    c['stubs'] += [{'address': a, 'argc': n, 'ret': 0} for a, n in callees]
    return c


def menu_state(running, result, state, fill='a5'):
    """An invented menu state buffer: the dialog's state and result, and the running byte."""
    menu = bytearray.fromhex(fill * MENU_STATE_SIZE)
    struct.pack_into('<I', menu, DIALOG_STATE, state)
    struct.pack_into('<I', menu, DIALOG_RESULT, result)
    menu[TEXT_INPUT_RUNNING] = running
    return {'size': MENU_STATE_SIZE, 'bytes': menu.hex()}


def status_case(cid, running, result, state, fill='a5'):
    c = case('status', cid)
    c['buffers']['menu'] = menu_state(running, result, state, fill)
    c['memory'].append({'addr': MENU_STATE, 'pointer': 'menu'})
    return c


def open_case(cid, answers, swap_menu_state=False, fill='a5'):
    """answers: what close, status, set-up and open return, in order, up to the first failure
    (the step stops there). swap_menu_state: the settings pointer is the menu state global
    itself, and the set-up's stub stores a second menu state there through it."""
    c = case('open', cid)
    b = c['buffers']
    b['menu'] = menu_state(0, 1, 0, fill)
    b['settings'] = {'size': 0xd0, 'bytes': '5c' * 0xd0}
    c['memory'].append({'addr': MENU_STATE, 'pointer': 'menu'})
    if swap_menu_state:
        b['other_menu'] = menu_state(0, 1, 0, '6e')
        c['memory'].append({'addr': 'buf:step+8', 'guest': MENU_STATE})
    else:
        c['memory'].append({'addr': 'buf:step+8', 'pointer': 'settings'})
    calls = [(OPEN_CLOSE, 1), (OPEN_STATUS, 1), (OPEN_SET_UP, 2), (OPEN_OPEN, 1)]
    for (address, argc), answer in zip(calls, answers):
        stub = {'address': address, 'argc': argc, 'ret': hex(answer)}
        if address == OPEN_SET_UP and swap_menu_state:
            stub['writes'] = [{'arg': 1, 'pointer': 'other_menu'}]
        c['stubs'].append(stub)
    return spares(c, calls)


def open_cases():
    true, false = 1, 0
    out = [open_case('opens', [true, NOT_OPEN, true, true]),
           open_case('opens_fill_00', [true, NOT_OPEN, true, true], fill='00'),
           open_case('opens_status_int_min', [true, 0x80000000, true, true]),
           open_case('opens_bool_upper_junk', [0xffffff01, NOT_OPEN, 0x7f01, 0xa501]),
           open_case('opens_status_upper_junk', [true, 0x12345678ffffffff, true, true]),
           open_case('menu_state_read_again', [true, NOT_OPEN, true, true], swap_menu_state=True)]
    for answer in (false, 0xff00, 0x100):
        out.append(open_case(f'close_fails_{answer:x}', [answer]))
        out.append(open_case(f'set_up_fails_{answer:x}', [true, NOT_OPEN, answer]))
        out.append(open_case(f'open_fails_{answer:x}', [true, NOT_OPEN, true, answer]))
    for status in (0, 2, 3, 0x7fffffff, 0x100000000):
        out.append(open_case(f'system_dialog_open_{status:x}', [true, status]))
    return out


def status_cases():
    out = []
    for running in RUNNING_VALUES:
        for result in (0, 2, 0xffffffff):
            out.append(status_case(f'running_{running:02x}_result_{result:x}', running, result, 1))
    for result in RESULTS:
        for state in STATES:
            out.append(status_case(f'ended_result_{result:x}_state_{state}', 0, result, state))
    for fill in ('00', 'ff'):
        out.append(status_case(f'entered_fill_{fill}', 0, 2, 2, fill))
        out.append(status_case(f'canceled_fill_{fill}', 0, 1, 2, fill))
    return out


def take_case(cid, capacity, size, callback_writes=(), word=0):
    """capacity < 8: inline text; 8 or more: a heap buffer freed through the allocator."""
    c = case('take', cid)
    c['returns'] = 'void'
    b = c['buffers']
    b['guard'] = {'size': 0x10, 'bytes': '5a' * 8}
    b['target'] = {'size': 0x10, 'bytes': '00' * 0x10}
    b['target_vtable'] = {'size': 0x30, 'bytes': 'e1' * 0x30}
    b['allocator'] = {'size': 0x10, 'bytes': '00' * 0x10}
    b['allocator_vtable'] = {'size': 0x80, 'bytes': 'e2' * 0x80}
    b['heap_text'] = {'size': 0x40, 'bytes': ''.join(f'{0x41 + i:02x}00' for i in range(0x20))}
    b['other_text'] = {'size': 0x40, 'bytes': '6100' * 0x20}
    c['memory'] += [{'addr': GUARD_GOT, 'pointer': 'guard'},
                    {'addr': 'buf:step+0x40', 'pointer': 'target'},
                    {'addr': 'buf:target', 'pointer': 'target_vtable'},
                    {'addr': 'buf:target_vtable+0x10', 'guest': CALLBACK_CALL},
                    {'addr': 'buf:allocator', 'pointer': 'allocator_vtable'},
                    {'addr': 'buf:allocator_vtable+0x70', 'guest': ALLOCATOR_FREE}]
    # The text object: the helper's word at +0, then the wide string at +8 (its own unknown word,
    # text, size, capacity, allocator, owns).
    if capacity >= 8:
        text = [{'arg': 0, 'offset': TEXT_HEAP, 'pointer': 'heap_text'},
                {'arg': 0, 'offset': TEXT_HEAP + 8, 'bytes': '00' * 8}]
    else:
        text = [{'arg': 0, 'offset': TEXT_HEAP, 'bytes': ''.join(f'{0x61 + i:02x}00' for i in range(8))}]
    fills = [{'arg': 0, 'offset': 0, 'bytes': struct.pack('<Q', word).hex()},
             {'arg': 0, 'offset': 0x08, 'bytes': '77' * 8}] + text + [
        {'arg': 0, 'offset': TEXT_SIZE, 'bytes': struct.pack('<Q', size).hex()},
        {'arg': 0, 'offset': TEXT_CAPACITY, 'bytes': struct.pack('<Q', capacity).hex()},
        {'arg': 0, 'offset': TEXT_ALLOCATOR, 'pointer': 'allocator'},
        {'arg': 0, 'offset': TEXT_OWNS, 'bytes': '01'}]
    c['stubs'].append({'address': TAKE_TEXT, 'argc': 1, 'writes': fills})
    c['stubs'].append({'address': CALLBACK_CALL, 'argc': 2,
                       'argmem': [{'arg': 1, 'size': TEXT_COMPARED}], 'writes': list(callback_writes)})
    final_capacity = capacity   # the release reads the capacity again after the callback
    for w in callback_writes:
        if w.get('offset') == TEXT_CAPACITY:
            final_capacity = struct.unpack('<Q', bytes.fromhex(w['bytes']))[0]
    if final_capacity >= 8:
        c['stubs'].append({'address': ALLOCATOR_FREE, 'argc': 2})
    return spares(c, [(TAKE_TEXT, 1), (CALLBACK_CALL, 2), (ALLOCATOR_FREE, 2)])


def take_cases():
    out = []
    for capacity, size in ((7, 3), (0, 0), (8, 8), (0x10, 0x0c), (0x1f, 0x1f), (0x8000000000000000, 1)):
        out.append(take_case(f'capacity_{capacity:x}_size_{size:x}', capacity, size))
    out.append(take_case('helper_word_set', 0x10, 4, word=0xfeedfacecafebeef))
    taken = ({'arg': 1, 'offset': TEXT_CAPACITY, 'bytes': struct.pack('<Q', 7).hex()},
             {'arg': 1, 'offset': TEXT_SIZE, 'bytes': '00' * 8})
    out.append(take_case('callback_takes_heap_text', 0x10, 4, taken))
    swapped = ({'arg': 1, 'offset': TEXT_HEAP, 'pointer': 'other_text'},
               {'arg': 1, 'offset': TEXT_CAPACITY, 'bytes': struct.pack('<Q', 0x20).hex()})
    out.append(take_case('callback_swaps_buffer', 0x10, 4, swapped))
    grown = ({'arg': 1, 'offset': TEXT_HEAP, 'pointer': 'other_text'},
             {'arg': 1, 'offset': TEXT_CAPACITY, 'bytes': struct.pack('<Q', 0x10).hex()})
    out.append(take_case('callback_moves_inline_to_heap', 7, 3, grown))
    return out


def fonts_case(cid, singleton, player, fill='5a'):
    """singleton: 'present', or 'reported' (missing until the report's stub stores it)."""
    c = case('fonts', cid)
    c['returns'] = 'void'
    scaleform = bytearray.fromhex(fill * 0x30)
    c['buffers']['scaleform'] = {'size': 0x30, 'bytes': scaleform.hex()}
    c['buffers']['player'] = {'size': 0x40, 'bytes': 'c3' * 0x40}
    if player:
        c['memory'].append({'addr': 'buf:scaleform+8', 'pointer': 'player'})
    else:
        c['memory'].append({'addr': 'buf:scaleform+8', 'bytes': '00' * 8})
    if singleton == 'present':
        c['memory'].append({'addr': hex(SPRJ_SCALEFORM), 'pointer': 'scaleform'})
    else:
        c['memory'].append({'addr': hex(SPRJ_SCALEFORM), 'bytes': '00' * 8})
        c['stubs'].append({'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 2, 3], 'argu32': [1],
                           'writes': [{'arg': 3, 'offset': SPRJ_SCALEFORM - SPRJ_SCALEFORM_NAME,
                                       'pointer': 'scaleform'}]})
    if player:
        c['stubs'].append({'address': LOAD_FONTS, 'argc': 1})
    return spares(c, [(FATAL_ERROR, 4), (LOAD_FONTS, 1)])


def fonts_cases():
    out = []
    for singleton in ('present', 'reported'):
        for player in (True, False):
            for fill in ('00', '5a', 'ff'):
                out.append(fonts_case(f'{singleton}_{"player" if player else "no_player"}_fill_{fill}',
                                      singleton, player, fill))
    return out


def wait_case(cid, flag, neighbour):
    c = case('wait', cid)
    c['memory'] = [{'addr': hex(REQUEST_BYTE - 1), 'bytes': neighbour},
                   {'addr': hex(REQUEST_BYTE), 'bytes': f'{flag:02x}'},
                   {'addr': hex(REQUEST_BYTE + 1), 'bytes': neighbour}]
    return c


def wait_cases():
    return [wait_case(f'flag_{flag:02x}_next_{neighbour}', flag, neighbour)
            for flag in FLAG_VALUES for neighbour in NEIGHBOURS]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = {'take': take_cases, 'status': status_cases, 'open': open_cases, 'fonts': fonts_cases, 'wait': wait_cases}[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
