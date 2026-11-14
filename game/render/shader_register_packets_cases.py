#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the shader register packet writers (game/render/shader_register_packets.cpp),
for tools/verify.py run.

usage: shader_register_packets_cases.py OUT_DIR mode|descriptor16|descriptor32|sampler16|address|word

The command buffer's prefix (with its flag word at +0x28) points into a `packet` buffer; its space
callback is a scripted stub at a fake game address. Cases: exactly enough room, one word short
(granted, also moving the cursor; refused, also with only the low byte clear), an empty buffer
refused, a limit below the cursor (the wrapped difference reads as room), a packet that overlaps
the prefix (the cursor is read again where the original reads it), the flag word's bit 1 alone,
clear and all bits, every stage the game uses and slots that wrap the register, and per writer:
every shader stages value the game uses and its neighbours; a descriptor that overlaps the packet it is
copied into (the copy order); low and high address halves; word values.
"""
import json
import os
import sys

ADDRESSES = {'mode': '0x01476030', 'descriptor16': '0x014761a0', 'descriptor32': '0x01476240',
             'sampler16': '0x014762f0', 'address': '0x01476390', 'word': '0x01476420'}
WORDS = {'mode': 3, 'descriptor16': 8, 'descriptor32': 12, 'sampler16': 8, 'address': 4, 'word': 3}
CALLBACK = '0x00500200'  # any address: the prefix's callback points at it and it is stubbed
WORD = 4
PACKET = 0x200
LIMIT, CURSOR, CALLBACK_SLOT, ARGUMENT, FLAGS = 0x8, 0x10, 0x18, 0x20, 0x28
SOURCE = bytes(range(0x11, 0x11 + 0x20)).hex()


def case(variant, cid, value=None, stage=1, slot=3, flags=2, room=None, callback=None,
         moves_cursor=False, limit_below=False, overlap=None, source_in_packet=None):
    words = WORDS[variant]
    if value is None:
        value = 0xb0 if variant == 'mode' else 0x44  # a mode the game uses, so the packet is written
    room = words + 4 if room is None else room
    cursor = 0x40
    limit = cursor - WORD if limit_below else cursor + room * WORD
    holder = 'packet' if overlap is not None else 'buffer'
    if overlap is not None:
        cursor, limit = overlap, PACKET
    memory = [{'addr': f'buf:{holder}+0x0', 'pointer': 'packet+0x0'},
              {'addr': f'buf:{holder}+{LIMIT:#x}', 'pointer': f'packet+{limit:#x}'},
              {'addr': f'buf:{holder}+{CURSOR:#x}', 'pointer': f'packet+{cursor:#x}'},
              {'addr': f'buf:{holder}+{CALLBACK_SLOT:#x}', 'guest': CALLBACK},
              {'addr': f'buf:{holder}+{ARGUMENT:#x}', 'bytes': '7766554433221100'},
              {'addr': f'buf:{holder}+{FLAGS:#x}', 'bytes': flags.to_bytes(4, 'little').hex()}]
    # Every packet byte differs from its neighbours, so a copy from inside the packet shows its order.
    pattern = bytes((i * 7 + 3) & 0xff for i in range(PACKET)).hex()
    buffers = {'buffer': {'size': 0x2c}, 'packet': {'size': PACKET, 'bytes': pattern},
               'spare': {'size': PACKET, 'bytes': '5a' * PACKET},
               'source': {'size': 0x20, 'bytes': SOURCE}}
    stubs = []
    if callback is not None:
        entry = {'address': CALLBACK, 'argc': 3, 'ret': callback}
        if moves_cursor:
            entry['writes'] = [{'arg': 0, 'offset': CURSOR, 'pointer': 'spare+0x10'}]
        stubs.append(entry)
    args = {'rdi': f'buf:{holder}'}
    if variant == 'mode':
        args['rsi'] = value
    else:
        args.update({'rsi': stage, 'rdx': slot})
        if variant == 'word':
            args['rcx'] = value
        elif variant == 'address':
            args['rcx'] = value
        else:
            src = 'source+0x0' if source_in_packet is None else f'packet+{cursor + source_in_packet:#x}'
            args['rcx'] = f'buf:{src}'
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': args,
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def cases(variant):
    words = WORDS[variant]
    c = lambda cid, **k: case(variant, cid, **k)
    out = [c('exact_room', room=words),
           c('one_short_granted', room=words - 1, callback=1),
           c('one_short_granted_new_cursor', room=words - 1, callback=0x101, moves_cursor=True),
           c('one_short_refused', room=words - 1, callback=0),
           c('one_short_refused_low_byte', room=words - 1, callback=0x7f00),
           c('empty_refused', room=0, callback=0),
           c('limit_below_cursor', limit_below=True),
           c('packet_overlaps_cursor', overlap=0x8),
           c('packet_overlaps_cursor_high_word', overlap=0x4)]
    if variant == 'mode':
        for m in (0, 0x45, 0xad, 0xb0, 0x200):
            out.append(c(f'mode_{m:#x}', value=m))
        for m in (1, 0x44, 0x46, 0xac, 0xae, 0xaf, 0xb1, 0x1ff, 0x201, 0xffffffff, 0x80000000):
            out.append(c(f'other_mode_{m:#x}', value=m))
        out.append(c('other_mode_refused', value=1, room=words - 1, callback=0))
        out.append(c('mode_flags_ignored', value=0xb0, flags=0xffffffff))
        return out
    out += [c('flag_bit_clear', flags=0xfffffffd), c('flag_bit_only', flags=2), c('flags_all', flags=0xffffffff),
            c('flags_none', flags=0)]
    out += [c(f'stage_{s}', stage=s) for s in range(7)]
    out += [c('slot_zero', slot=0), c('slot_large', slot=0x7fff), c('slot_wraps', slot=0xffffffff)]
    if variant == 'word':
        out += [c('value_zero', value=0), c('value_all', value=0xffffffff)]
    elif variant == 'address':
        out += [c('address_low', value=0x12345678), c('address_high', value=0x0123456789abcdef),
                c('address_all', value=0xffffffffffffffff)]
    else:
        # The descriptor sits inside the packet it is copied into: the copy order decides the result.
        out += [c(f'source_in_packet_{o:#x}', source_in_packet=o) for o in (0x8, 0x10, 0x18, 0x20)]
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
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
