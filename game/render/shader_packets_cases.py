#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the shader packet writers (game/render/shader_packets.cpp), for tools/verify.py run.

usage: shader_packets_cases.py OUT_DIR set_ps_shader|update_ps_shader|set_vs_shader

The command buffer's prefix points into a `packet` buffer; its space callback is a scripted stub
at a fake game address. The graphics library call is a scripted import whose arguments are
compared (the packet address, the word count, the registers and, for the vertex shader, the
modifier). Cases: exactly enough room, one word short (the callback grants, also moving the
cursor, or refuses, also with only the low byte clear), plenty of room, a limit below the cursor
(the wrapped difference reads as room), and the library failing (the cursor stays).
"""
import json
import os
import sys

VARIANTS = {
    'set_ps_shader': ('0x014753c0', 'sceGnmSetPsShader', 40, False),
    'update_ps_shader': ('0x01475420', 'sceGnmUpdatePsShader', 40, False),
    'set_vs_shader': ('0x01475480', 'sceGnmSetVsShader', 29, True),
}
CALLBACK = '0x00500200'  # any address: the prefix's callback points at it and it is stubbed
WORD = 4
PACKET = 0x200
CURSOR, LIMIT, CALLBACK_SLOT, ARGUMENT = 0x10, 0x8, 0x18, 0x20


def case(variant, cid, room_words, callback=None, moves_cursor=False, library=0, modifier=0,
         limit_below=False):
    address, name, words, has_modifier = VARIANTS[variant]
    cursor = 0x40
    limit = cursor - WORD if limit_below else cursor + room_words * WORD
    memory = [{'addr': f'buf:buffer+{0:#x}', 'pointer': 'packet+0x0'},
              {'addr': f'buf:buffer+{LIMIT:#x}', 'pointer': f'packet+{limit:#x}'},
              {'addr': f'buf:buffer+{CURSOR:#x}', 'pointer': f'packet+{cursor:#x}'},
              {'addr': f'buf:buffer+{CALLBACK_SLOT:#x}', 'guest': CALLBACK},
              {'addr': f'buf:buffer+{ARGUMENT:#x}', 'bytes': '7766554433221100'}]
    stubs, imports = [], []
    granted = True
    if callback is not None:
        entry = {'address': CALLBACK, 'argc': 3, 'ret': callback}
        if moves_cursor:
            entry['writes'] = [{'arg': 0, 'offset': CURSOR, 'pointer': 'spare+0x10'}]
        stubs.append(entry)
        granted = (callback & 0xff) != 0
    if granted:
        imports.append({'name': name, 'argc': 4 if has_modifier else 3, 'ret': library})
    args = {'rdi': 'buf:buffer', 'rsi': 'buf:registers'}
    if has_modifier:
        args['rdx'] = modifier
    return {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': args,
            'buffers': {'buffer': {'size': 0x28}, 'packet': {'size': PACKET, 'bytes': '6b' * PACKET},
                        'spare': {'size': PACKET}, 'registers': {'size': 0x40, 'bytes': '3c' * 0x40}},
            'memory': memory, 'imports': imports, 'stubs': stubs}


def cases(variant):
    words = VARIANTS[variant][2]
    out = [case(variant, 'exact_room', words),
           case(variant, 'plenty_of_room', words + 50),
           case(variant, 'one_short_granted', words - 1, callback=1),
           case(variant, 'one_short_granted_new_cursor', words - 1, callback=0x101, moves_cursor=True),
           case(variant, 'empty_refused', 0, callback=0),
           case(variant, 'one_short_refused_low_byte', words - 1, callback=0x7f00),
           case(variant, 'limit_below_cursor', 0, limit_below=True),
           case(variant, 'library_fails', words, library=-1),
           case(variant, 'library_fails_after_grant', 2, callback=1, moves_cursor=True, library=0x80000000)]
    if VARIANTS[variant][3]:
        out += [case(variant, 'modifier_set', words, modifier=0xdeadbeef),
                case(variant, 'modifier_high_bit', words, modifier=0x80000001)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in VARIANTS:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
