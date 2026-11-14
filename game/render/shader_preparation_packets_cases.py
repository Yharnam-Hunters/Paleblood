#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the shader preparation packet writers (game/render/shader_preparation_packets.cpp),
for tools/verify.py run.

usage: shader_preparation_packets_cases.py OUT_DIR range|event

The command buffer's prefix points into a `packet` buffer; its space callback is a scripted stub
at a fake game address. Cases: every action bit alone and all of them (bits that are dropped
too), targets with the top bit set, the flag's low bit and higher bits, exactly enough room, one
word short (granted, also moving the cursor; refused, also with only the low byte clear), a limit
below the cursor (the wrapped difference reads as room), and a packet that overlaps the prefix's
cursor (the cursor is read again after the packet is written).
"""
import json
import os
import sys

ADDRESSES = {'range': '0x01475e10', 'event': '0x01477f40'}
CALLBACK = '0x00500200'  # any address: the prefix's callback points at it and it is stubbed
WORD, WORDS = 4, 7
PACKET = 0x200
LIMIT, CURSOR, CALLBACK_SLOT, ARGUMENT = 0x8, 0x10, 0x18, 0x20


def case(variant, cid, actions=0x3b, targets=0x1234, more=0, flag=1, base=0x1000, size=0x40,
         room=WORDS + 4, callback=None, moves_cursor=False, limit_below=False, overlap=None):
    cursor = 0x40
    limit = cursor - WORD if limit_below else cursor + room * WORD
    memory = [{'addr': 'buf:buffer+0x0', 'pointer': 'packet+0x0'},
              {'addr': f'buf:buffer+{LIMIT:#x}', 'pointer': f'packet+{limit:#x}'},
              {'addr': f'buf:buffer+{CURSOR:#x}', 'pointer': f'packet+{cursor:#x}'},
              {'addr': f'buf:buffer+{CALLBACK_SLOT:#x}', 'guest': CALLBACK},
              {'addr': f'buf:buffer+{ARGUMENT:#x}', 'bytes': '7766554433221100'}]
    buffers = {'buffer': {'size': 0x28}, 'packet': {'size': PACKET, 'bytes': '6b' * PACKET},
               'spare': {'size': PACKET, 'bytes': '5a' * PACKET}}
    if overlap is not None:
        # The prefix lives inside `packet`, and the cursor points just below its own slot.
        buffers['packet']['size'] = PACKET
        memory = [{'addr': 'buf:packet+0x0', 'pointer': 'packet+0x0'},
                  {'addr': f'buf:packet+{LIMIT:#x}', 'pointer': f'packet+{PACKET:#x}'},
                  {'addr': f'buf:packet+{CURSOR:#x}', 'pointer': f'packet+{overlap:#x}'},
                  {'addr': f'buf:packet+{CALLBACK_SLOT:#x}', 'guest': CALLBACK},
                  {'addr': f'buf:packet+{ARGUMENT:#x}', 'bytes': '7766554433221100'}]
    stubs = []
    if callback is not None:
        entry = {'address': CALLBACK, 'argc': 3, 'ret': callback}
        if moves_cursor:
            entry['writes'] = [{'arg': 0, 'offset': CURSOR, 'pointer': 'spare+0x10'}]
        stubs.append(entry)
    prefix = 'buf:packet' if overlap is not None else 'buf:buffer'
    if variant == 'range':
        args = {'rdi': prefix, 'rsi': base, 'rdx': size, 'rcx': targets, 'r8': actions, 'r9': more}
    else:
        args = {'rdi': prefix, 'rsi': actions, 'rdx': targets, 'rcx': flag}
    out = {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': args,
           'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}
    if variant == 'range':
        out['stack'] = [flag]  # the 7th argument
    return out


def cases(variant):
    c = lambda cid, **k: case(variant, cid, **k)
    out = [c(f'action_bit_{b}', actions=1 << b, flag=0) for b in range(8)]
    out += [c('all_actions', actions=0xffffffff),
            c('targets_top_bit', targets=0xffffffff, more=0x80000000),
            c('targets_top_bit_flag_clear', targets=0x80000001, more=0x80000000, flag=0),
            c('flag_low_bit_only', flag=0xfffffffe),
            c('flag_all_bits', flag=0xffffffff),
            c('exact_room', room=WORDS),
            c('one_short_granted', room=WORDS - 1, callback=1),
            c('one_short_granted_new_cursor', room=WORDS - 1, callback=0x101, moves_cursor=True),
            c('empty_refused', room=0, callback=0),
            c('one_short_refused_low_byte', room=WORDS - 1, callback=0x7f00),
            c('limit_below_cursor', limit_below=True),
            c('packet_overlaps_cursor', overlap=0x8),
            c('packet_overlaps_cursor_high_word', overlap=0x4)]
    if variant == 'range':
        out += [c('targets_or_more', targets=0x0f0f0f0f, more=0x30303030),
                c('base_and_size_extremes', base=0xffffffff, size=0xffffffff)]
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
