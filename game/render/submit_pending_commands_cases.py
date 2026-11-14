#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for render_submit_pending_commands_02aafa00 (game/render/submit_pending_commands.cpp),
for tools/verify.py run.

usage: submit_pending_commands_cases.py OUT_DIR

The context's command buffer points into a `payload` buffer (or holds plain numbers when only the
distance between cursor and limit matters); its space callback and the snapshot submitter
(0x02ab0b00) are scripted stubs, the submitter capturing the snapshot and the context's buffer
prefix. Cases: counters settled (also with no room, wrapped counts, sign bits), a wait pending with
exactly seven words, one short (callback refusing, granting, moving the cursor), more room, a limit
below the cursor, distances that are not a multiple of four, distances whose word count only fits
in 32 bits after cutting, the submitter moving the cursor, flag byte values with the neighbouring
bytes set, and an empty region.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02aafa00'
SUBMITTER = '0x02ab0b00'
CALLBACK = '0x00500200'  # any address: the buffer's callback points at it and it is stubbed
CONTEXT_SIZE = 0xb500
BEGIN, LIMIT, CURSOR, CALLBACK_SLOT, ARGUMENT = 0x0, 0x8, 0x10, 0x18, 0x20
OWNER, LAST_RESET, FLAGS = 0xb490, 0xb4b8, 0xb4f0
COUNTERS, PRODUCER, OBSERVED = 0xb728, 0x18, 0x20
WORD = 4
PAYLOAD = 0x100
WAIT_WORDS = 7


def qword(value):
    return struct.pack('<Q', value & ((1 << 64) - 1)).hex()


def dword(value):
    return struct.pack('<I', value & 0xffffffff).hex()


def case(cid, produced=1, observed=0, room=None, cursor=0x20, flags=0x5a, below=False, raw=None,
         callback=None, callback_moves=False, submitter_moves=False):
    # The context's bytes all differ from their neighbours, so the snapshot shows where it was copied from.
    pattern = bytes((i * 11 + 5) & 0xff for i in range(CONTEXT_SIZE))
    memory = []
    if raw is not None:  # plain numbers: neither is read through
        begin_value, cursor_value, limit_value = raw
        memory += [{'addr': f'buf:context+{BEGIN:#x}', 'bytes': qword(begin_value)},
                   {'addr': f'buf:context+{CURSOR:#x}', 'bytes': qword(cursor_value)},
                   {'addr': f'buf:context+{LIMIT:#x}', 'bytes': qword(limit_value)}]
    else:
        room = WAIT_WORDS if room is None else room
        limit = cursor - WORD if below else cursor + room * WORD
        memory += [{'addr': f'buf:context+{BEGIN:#x}', 'pointer': 'payload+0x8'},
                   {'addr': f'buf:context+{CURSOR:#x}', 'pointer': f'payload+{cursor:#x}'},
                   {'addr': f'buf:context+{LIMIT:#x}', 'pointer': f'payload+{limit:#x}'}]
    memory += [{'addr': f'buf:context+{CALLBACK_SLOT:#x}', 'guest': CALLBACK},
               {'addr': f'buf:context+{ARGUMENT:#x}', 'bytes': qword(0x7766554433221100)},
               {'addr': f'buf:context+{OWNER:#x}', 'pointer': 'owner'},
               {'addr': f'buf:context+{LAST_RESET:#x}', 'bytes': qword(0x1122334455667788)},
               {'addr': f'buf:context+{FLAGS - 1:#x}', 'bytes': f'a5{flags:02x}c3'},
               {'addr': f'buf:owner+{COUNTERS:#x}', 'pointer': 'counters'},
               {'addr': f'buf:counters+{PRODUCER:#x}', 'pointer': 'produced'},
               {'addr': f'buf:counters+{OBSERVED:#x}', 'pointer': 'observed'},
               {'addr': 'buf:produced', 'bytes': dword(produced) + 'ffffffff'},
               {'addr': 'buf:observed', 'bytes': dword(observed) + 'eeeeeeee'}]
    buffers = {'context': {'size': CONTEXT_SIZE, 'bytes': pattern.hex()},
               'payload': {'size': PAYLOAD, 'bytes': '3c' * PAYLOAD},
               'owner': {'size': COUNTERS + 8}, 'counters': {'size': 0x30},
               'produced': {'size': 8}, 'observed': {'size': 8}}
    stubs = []
    if callback is not None:
        entry = {'address': CALLBACK, 'argc': 3, 'ret': callback}
        if callback_moves:
            entry['writes'] = [{'arg': 0, 'offset': CURSOR, 'pointer': 'payload+0x80', 'size': 8}]
        stubs.append(entry)
    else:
        entry = {'address': SUBMITTER, 'argc': 4, 'ret': 0,
                 'argmem': [{'arg': 2, 'size': 0x40}, {'arg': 0, 'size': 0x28}]}
        if submitter_moves:
            entry['writes'] = [{'arg': 0, 'offset': CURSOR, 'pointer': 'payload+0x90', 'size': 8}]
        stubs.append(entry)
    return {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:context'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def cases():
    pending = {'produced': 5, 'observed': 3}
    out = [case('settled'),
           case('settled_no_room', room=0),
           case('settled_limit_below', below=True),
           case('settled_counts_wrap', produced=0, observed=0xffffffff),
           case('settled_sign_bits', produced=0x80000000, observed=0x7fffffff),
           case('settled_submitter_moves_cursor', submitter_moves=True),
           case('pending_equal_counts', produced=5, observed=5),
           case('pending_observed_is_produced_plus_one', produced=5, observed=6),
           case('pending_exact_room', **pending),
           case('pending_more_room', room=0x20, **pending),
           case('pending_one_short_refused', room=WAIT_WORDS - 1, callback=0, **pending),
           case('pending_one_short_granted', room=WAIT_WORDS - 1, callback=1, **pending),
           case('pending_one_short_callback_moves_cursor', room=WAIT_WORDS - 1, callback=1,
                callback_moves=True, **pending),
           case('pending_empty_refused', room=0, callback=0, **pending),
           case('pending_limit_below_cursor', below=True, **pending),
           case('pending_submitter_moves_cursor', submitter_moves=True, **pending),
           case('pending_empty_region', cursor=0x8, **pending)]
    # Distances that are not whole words, and word counts that only fit after cutting to 32 bits.
    base = 0x1000
    for name, distance, short in (('27_bytes', 27, True), ('28_bytes', 28, False), ('29_bytes', 29, False),
                                  ('high_words_3', WORD * ((1 << 32) + 3), True),
                                  ('high_words_7', WORD * ((1 << 32) + 7), False),
                                  ('high_words_6_unaligned', WORD * ((1 << 32) + 6) + 3, True),
                                  ('words_2_pow_31', WORD * (1 << 31), False)):
        out.append(case(f'pending_distance_{name}', raw=(0x500, base, base + distance),
                        callback=0 if short else None, **pending))
    for value in (0, 1, 0x80, 0xff):
        out.append(case(f'settled_flags_{value:#x}', flags=value))
        out.append(case(f'pending_flags_{value:#x}', flags=value, **pending))
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
