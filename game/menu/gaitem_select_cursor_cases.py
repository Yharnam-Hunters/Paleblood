#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the item select dialog's cursor (game/menu/gaitem_select_cursor.cpp), for
tools/verify.py run.

usage: gaitem_select_cursor_cases.py OUT_DIR move|set_row

- move: no movement; a step within the limits; steps past the limits and below 0 (clamped, -1
  included); a
  negative limit (counts as 0); the limits shrinking on the second read (nothing stored); only the
  row changing; an old column below 0 clamped back to 0 (a change); limits at 0x7fffffff.
- set_row: rows below 0, inside, the last and past it; an empty list (row -1); the first shown
  row at, past and before the count, and negative; the list's virtual call changing the row,
  the count and the first shown row (read again after it); NaN payloads in the bar's top and
  length; a bar x too large for an int (0x80000000).
The limits (0x01ed11f0), the list's set-row call (0x01edc580, for move), the virtual calls and the
scroll bar's two functions are scripted stubs. Generated fixtures, not recorded inputs.
"""
import json
import os
import struct
import sys

ADDRESSES = {'move': '0x01ed0dd0', 'set_row': '0x01edc580'}
LIMITS, SET_ROW, PLACE_THUMB, SIZE_THUMB = '0x01ed11f0', '0x01edc580', '0x01ecb8c0', '0x01ecba50'
# Game functions the fake vtables point at; each is replaced by a stub in the cases.
REFRESH, SET_SCROLLABLE = '0x01ed1320', '0x01ee6410'
LIST = 0xe0


def f32(v):
    return struct.pack('<I', v) if isinstance(v, int) else struct.pack('<f', v)


def packed(column, row):
    return (column & 0xffffffff) | (row & 0xffffffff) << 32


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void', 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


def list_bytes(row, count, first, top=40.0, x=12.0, length=200.0):
    data = bytearray(b'\x2e' * 0x178)
    data[0x160:0x164], data[0x164:0x168], data[0x168:0x16c] = f32(top), f32(x), f32(length)
    struct.pack_into('<iii', data, 0x16c, row, count, first)
    return data


def move_case(cid, columns, rows, column, row, limits, limits_again=None):
    c = base('move', cid)
    dialog = bytearray(b'\x1d' * (LIST + 0x178))
    struct.pack_into('<i', dialog, 0x84, column)
    dialog[LIST:] = list_bytes(row, 30, 0)
    c['buffers']['dialog'] = {'size': len(dialog), 'bytes': dialog.hex()}
    c['buffers']['dialog_vt'] = {'size': 0x40, 'bytes': '00' * 0x40}
    c['memory'] += [{'addr': 'buf:dialog', 'pointer': 'dialog_vt'}, {'addr': 'buf:dialog_vt+0x38', 'guest': REFRESH}]
    again = limits if limits_again is None else limits_again
    c['stubs'] += [{'address': LIMITS, 'ret': packed(*limits), 'argc': 1},
                   {'address': LIMITS, 'ret': packed(*again), 'argc': 1}]

    def clamp(v, limit):
        return 0 if v < 0 else min(v, max(limit, 0))
    new_column, new_row = clamp(columns + column, limits[0]), clamp(rows + row, limits[1])
    if new_row <= again[1] and new_column <= again[0] and (new_row, new_column) != (row, column):
        c['stubs'] += [{'address': SET_ROW, 'ret': 0, 'argc': 2, 'argu32': [1], 'argi': [0]},
                       {'address': REFRESH, 'ret': 0, 'argc': 1}]
    c['args'] = {'rdi': 'buf:dialog', 'rsi': f'{columns & 0xffffffff:#x}', 'rdx': f'{rows & 0xffffffff:#x}'}
    return c


def move_cases():
    return [move_case('no_movement', 0, 0, 2, 3, (5, 9)), move_case('step_right', 1, 0, 2, 3, (5, 9)),
            move_case('step_down', 0, 1, 2, 3, (5, 9)), move_case('step_up_left', -1, -1, 2, 3, (5, 9)),
            move_case('past_column_limit', 4, 0, 2, 3, (5, 9)), move_case('past_row_limit', 0, 20, 2, 3, (5, 9)),
            move_case('below_zero', -7, -9, 2, 3, (5, 9)), move_case('at_limit_no_change', 1, 1, 5, 9, (5, 9)),
            move_case('negative_limits', 1, 1, 0, 0, (-4, -1)), move_case('negative_limits_from_2', 0, 0, 2, 2, (-4, -1)),
            move_case('limits_shrink', 1, 0, 2, 3, (5, 9), (2, 9)),
            move_case('limits_shrink_row', 0, 1, 2, 3, (5, 9), (5, 3)),
            move_case('old_column_negative', 0, 0, -5, 3, (5, 9)),
            move_case('large_limits', 0x7fffff00, 0, 0x100, 0, (0x7fffffff, 0x7fffffff)),
            move_case('row_only_to_zero', 0, -3, 2, 3, (5, 9)),
            move_case('left_up_from_zero', -1, -1, 0, 0, (5, 9)), move_case('left_from_zero_row_down', -1, 1, 0, 0, (5, 9))]


def set_row_case(cid, row, count, first, top=40.0, x=12.0, length=200.0, writes=None):
    c = base('set_row', cid)
    c['buffers']['list'] = {'size': 0x178, 'bytes': list_bytes(-77, count, first, top, x, length).hex()}
    c['buffers']['list_vt'] = {'size': 0x10, 'bytes': '00' * 0x10}
    c['memory'] += [{'addr': 'buf:list', 'pointer': 'list_vt'}, {'addr': 'buf:list_vt+8', 'guest': SET_SCROLLABLE}]
    scrollable = {'address': SET_SCROLLABLE, 'ret': 0, 'argc': 2, 'argi': [0], 'argu8': [1]}
    if writes:
        scrollable['writes'] = [{'arg': 0, 'offset': 0x16c, 'bytes': struct.pack('<iii', *writes).hex()}]
    c['stubs'].append(scrollable)
    if count > first:
        c['stubs'] += [{'address': PLACE_THUMB, 'ret': 0, 'argc': 3, 'argi': [0], 'argu32': [1, 2]},
                       {'address': SIZE_THUMB, 'ret': 0, 'argc': 1, 'argf32': [0, 1]}]
    c['args'] = {'rdi': 'buf:list', 'rsi': f'{row & 0xffffffff:#x}'}
    return c


def set_row_cases():
    return [set_row_case('row_negative', -3, 10, 2), set_row_case('row_inside', 4, 10, 2),
            set_row_case('row_last', 9, 10, 2), set_row_case('row_past', 15, 10, 2),
            set_row_case('empty_list', 3, 0, 0), set_row_case('empty_list_negative_row', -1, 0, -2),
            set_row_case('first_at_count', 4, 10, 10), set_row_case('first_past_count', 4, 10, 12),
            set_row_case('first_negative', 4, 10, -3), set_row_case('row_0_first_0', 0, 1, 0),
            set_row_case('call_changes_state', 4, 10, 2, writes=(7, 20, 5)),
            set_row_case('call_changes_count_to_zero', 4, 10, 2, writes=(3, 0, 1)),
            set_row_case('top_nan', 4, 10, 2, top=0x7fc0beef), set_row_case('length_nan', 4, 10, 2, length=0xffc0cafe),
            set_row_case('both_nan', 4, 10, 2, top=0x7fa00001, length=0x7fb00002),
            set_row_case('x_too_large', 4, 10, 2, x=3.0e9), set_row_case('fractional', 1, 3, 1, top=0.5, length=7.25)]


def cases(variant):
    return move_cases() if variant == 'move' else set_row_cases()


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
