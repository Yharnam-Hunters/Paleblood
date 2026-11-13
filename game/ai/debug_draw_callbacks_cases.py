#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the AI debug draw helpers (game/ai/debug_draw_callbacks.cpp), for tools/verify.py run.

usage: debug_draw_callbacks_cases.py OUT_DIR axes|color

- axes: render slots 0 and 1, the slot index changed by the lock's acquire or by the renderer's
  prepare call (the index is read after both), the index -1 and 2 (sign-extended, not bounded),
  and two frames. Every slot of both tables is a separate stub, so a wrong slot is a different
  call; the calls and their order are compared, and the frame's 48 bytes at the draw call.
- color: no requested bits (the default, with the other mask empty or full), no shared bit (NaN
  but alpha), each of the 20 palette bits alone and with the next, bits 20-31 only (ignored),
  mixed masks (either one wider), all bits, and a sentinel in xmm1 (the default path leaves it,
  the other leaves the scale there). The palette is read-only data, so the game's colours are used.
"""
import json
import os
import struct
import sys

ADDRESSES = {'axes': '0x0135d810', 'color': '0x02232a60'}
RETURNS = {'axes': 'void', 'color': 'vec'}
DRAW_AXES = '0x0135ec80'
PALETTE_SIZE = 20
TABLE_SLOTS = 6
LOCK_SLOT_BASE = 0x00500100      # fake addresses: lock table slot i at base + 0x10 * i
RENDERER_SLOT_BASE = 0x00500200  # renderer table slot i
ACQUIRE, RELEASE, PREPARE = 3, 5, 5
XMM1_SENTINEL = '5a' * 16


def f32s(*v):
    return struct.pack(f'<{len(v)}f', *v).hex()


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def stub_address(table_base, slot):
    return f'0x{table_base + 0x10 * slot:08x}'


def axes_case(cid, slot=0, frame_values=(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12), set_by=None, new_slot=1):
    """set_by: None, 'acquire' or 'prepare' (that call stores new_slot as the current slot)."""
    c = base('axes', cid)
    c['buffers'] = {'renderer': {'size': 0x30}, 'renderer_table': {'size': 8 * TABLE_SLOTS},
                    'lock_table': {'size': 8 * TABLE_SLOTS}, 'slot_minus': {'size': 0x48},
                    'slot0': {'size': 0x48}, 'slot1': {'size': 0x48},
                    'frame': {'size': 0x30, 'bytes': f32s(*frame_values)}}
    m = c['memory']
    m += [{'addr': 'buf:renderer+0x0', 'pointer': 'renderer_table'},
          {'addr': 'buf:renderer+0x8', 'pointer': 'slot_minus'},
          {'addr': 'buf:renderer+0x10', 'pointer': 'slot0'},
          {'addr': 'buf:renderer+0x18', 'pointer': 'slot1'},
          {'addr': 'buf:renderer+0x20', 'bytes': (slot & 0xffffffff).to_bytes(4, 'little').hex()},
          {'addr': 'buf:renderer+0x28', 'pointer': 'lock_table'}]
    for i in range(TABLE_SLOTS):
        m.append({'addr': f'buf:lock_table+{8 * i:#x}', 'guest': stub_address(LOCK_SLOT_BASE, i)})
        m.append({'addr': f'buf:renderer_table+{8 * i:#x}', 'guest': stub_address(RENDERER_SLOT_BASE, i)})
    c['args'] = {'rdi': 'buf:renderer', 'rsi': 'buf:frame'}
    switch = [{'arg': 0, 'offset': 0x20 if set_by == 'prepare' else -0x8,
               'bytes': (new_slot & 0xffffffff).to_bytes(4, 'little').hex()}]
    acquire = {'address': stub_address(LOCK_SLOT_BASE, ACQUIRE), 'argc': 2}
    prepare = {'address': stub_address(RENDERER_SLOT_BASE, PREPARE), 'argc': 1}
    if set_by == 'acquire':
        acquire['writes'] = switch
    elif set_by == 'prepare':
        prepare['writes'] = switch
    c['stubs'] = [acquire, prepare,
                  {'address': DRAW_AXES, 'argc': 2, 'argmem': [{'arg': 1, 'size': 0x30}]},
                  {'address': stub_address(LOCK_SLOT_BASE, RELEASE), 'argc': 1}]
    # Every other slot is scripted too, so that calling it is recorded rather than lost.
    for table_base, used in ((LOCK_SLOT_BASE, (ACQUIRE, RELEASE)), (RENDERER_SLOT_BASE, (PREPARE,))):
        c['stubs'] += [{'address': stub_address(table_base, i), 'argc': 1}
                       for i in range(TABLE_SLOTS) if i not in used]
    return c


def color_case(cid, requested, present, owner=0):
    """The palette (0x04b35040) is read-only data, so every case uses the game's colours."""
    c = base('color', cid)
    c['args'] = {'rdi': owner, 'rsi': requested & 0xffffffff, 'rdx': present & 0xffffffff, 'xmm1': XMM1_SENTINEL}
    return c


def cases(variant):
    if variant == 'axes':
        return [axes_case('slot_0'), axes_case('slot_1', slot=1),
                axes_case('slot_set_by_acquire', set_by='acquire', new_slot=1),
                axes_case('slot_set_by_prepare', set_by='prepare', new_slot=1),
                axes_case('slot_cleared_by_prepare', slot=1, set_by='prepare', new_slot=0),
                axes_case('slot_minus_one', slot=-1),
                axes_case('slot_two_reads_index_word', slot=2),
                axes_case('other_frame', slot=1, frame_values=(-0.0, 1e30, -1e-30, 0.5, 7, 7, 7, 7, 0, 0, 0, 1))]
    out = [color_case('requested_none', 0, 0),
           color_case('requested_none_present_all', 0, 0xffffffff),
           color_case('none_shared', 0x1, 0x2),
           color_case('present_none', 0xfffff, 0),
           color_case('high_bits_only', 0xfff00000, 0xffffffff),
           color_case('bit_31', 0x80000000, 0x80000001),
           color_case('all', 0xffffffff, 0xffffffff),
           color_case('low_20', 0xfffff, 0xfffff),
           color_case('mixed', 0x5555, 0x3333),
           color_case('three', 0x80421, 0xfffff, owner=0x123456789),
           color_case('black_and_white', 0x600, 0x600),
           color_case('requested_wider', 0xffffffff, 0x90009),
           color_case('present_wider', 0x90009, 0xffffffff)]
    out += [color_case(f'bit_{b}', 1 << b, 0xffffffff) for b in range(PALETTE_SIZE)]
    out += [color_case(f'bit_{b}_and_next', 3 << b, 3 << b) for b in range(PALETTE_SIZE - 1)]
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
