#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for frame_timing_item_group_apply_requests (0x0144d860,
game/frame_timing/item_group_apply_requests.cpp), for tools/verify.py run.

usage: item_group_apply_requests_cases.py OUT_DIR

Everything the function touches lies in one buffer, `mem`, filled with a pattern, so every byte
it leaves or writes is compared: the group at GROUP, its item vector at VECTOR, the items at
ITEMS. The function calls nothing.
- empty vectors (end equal to the start; a 1..7 byte length, under one pointer);
- 1 to 16 items with every pair of enabled and requested bytes from 0, 1, 0x80 and 0xff; an item
  listed twice; a length that is not a whole number of pointers (the remainder is ignored);
- an end below the start (a negative count: nothing is updated);
- a length of exactly 2^31 pointers (negative as a 32-bit count: nothing is updated) and one of
  2^32 + 2 pointers (only the low 32 bits count: two items are updated);
- an item whose enabled byte is the low byte of the next vector entry (the vector is read again
  after each write, so the next item is the one the write points at), and one whose enabled byte
  is the low byte of the group's end (the end is read once, so the count does not change).
The last two kinds place `mem` at a fixed address (`origin`, in the harness's guest window) and
write their pointers as bytes, since they depend on the address's bytes.
"""
import json
import struct
import sys
from pathlib import Path

ADDRESS = '0x0144d860'
FUNCTION = 'frame_timing_item_group_apply_requests'

SIZE = 0x1000
GROUP = 0x80
BEGIN, END = GROUP + 0x20, GROUP + 0x28
VECTOR = 0x100                          # item pointers
ITEMS, ITEM_STRIDE = 0x200, 0x80        # item j at ITEMS + ITEM_STRIDE * j
ENABLED, REQUESTED = 0x68, 0x69
ORIGIN = 0x2000100000                   # page aligned; its low 20 bits are zero
STATES = [0x00, 0x01, 0x80, 0xff]


def item(j):
    return ITEMS + ITEM_STRIDE * j


def pointer(at, target):
    return {'addr': f'buf:mem+{at}', 'pointer': f'mem+{target}'}


def fixed_pointer(at, target):
    """A pointer to mem+target with `mem` at ORIGIN, as bytes."""
    return {'addr': f'buf:mem+{at}', 'bytes': struct.pack('<Q', ORIGIN + target).hex()}


def states(at, enabled, requested):
    return {'addr': f'buf:mem+{at + ENABLED}', 'bytes': bytes([enabled, requested]).hex()}


def case(cid, memory, fill='a5', fixed=False):
    row = {'schema': 1, 'address': ADDRESS, 'id': cid, 'returns': 'void',
           'args': {'rdi': f'buf:mem+{GROUP}', 'rsi': '0xfedcba9876543210',
                    'rdx': '0xffffffffffffffff'},
           'buffers': {'mem': {'size': SIZE, 'bytes': fill * SIZE}},
           'memory': memory}
    if fixed:
        row['buffers']['mem']['origin'] = f'{ORIGIN:#x}'
    return row


def group(items, item_states, extra_bytes=0):
    """A vector listing `items` (item numbers), whose items hold `item_states`."""
    memory = [pointer(BEGIN, VECTOR), pointer(END, VECTOR + 8 * len(items) + extra_bytes)]
    memory += [pointer(VECTOR + 8 * i, item(j)) for i, j in enumerate(items)]
    memory += [states(item(j), *pair) for j, pair in item_states.items()]
    return memory


def cases():
    for extra in range(8):
        yield case(f'empty-extra-{extra}', group([], {}, extra))
    for enabled in STATES:
        for requested in STATES:
            yield case(f'one-{enabled:02x}-{requested:02x}', group([0], {0: (enabled, requested)}))
    for n in [2, 3, 5, 8, 16]:
        item_states = {j: (STATES[j % 4], STATES[(j * 3 + 1) % 4]) for j in range(n)}
        yield case(f'items-{n}', group(list(range(n)), item_states))
        for extra in [1, 4, 7]:
            yield case(f'items-{n}-extra-{extra}', group(list(range(n)), item_states, extra))
    yield case('item-twice', group([0, 1, 0], {0: (0, 1), 1: (0xff, 0)}))
    yield case('item-twice-other-order', group([1, 0, 1], {0: (0x80, 0), 1: (1, 0xff)}))
    for fill in ['00', '01', '7f', 'ff']:
        yield case(f'fill-{fill}', group([0, 1, 2], {0: (0, 1), 1: (1, 0), 2: (0xff, 0xff)}),
                   fill)

    # The end below the start: a negative count.
    for back in [8, 0x80]:
        yield case(f'end-below-start-{back:x}',
                   [pointer(BEGIN, VECTOR), pointer(END, VECTOR - back), pointer(VECTOR, item(0)),
                    states(item(0), 0, 1)])

    # Far ends: 2^31 pointers is a negative 32-bit count; 2^32 + 2 pointers counts as two.
    for name, length in [('2-31', 1 << 31), ('2-32-plus-2', (1 << 32) + 2)]:
        yield case(f'far-end-{name}', [
            fixed_pointer(BEGIN, VECTOR), fixed_pointer(END, VECTOR + 8 * length),
            fixed_pointer(VECTOR, item(0)), fixed_pointer(VECTOR + 8, item(1)),
            fixed_pointer(VECTOR + 16, item(2)),
            states(item(0), 0, 1), states(item(1), 1, 0), states(item(2), 0, 0xff)], fixed=True)

    # Entry 0's item has its enabled byte on entry 1's low byte, its requested byte on entry 1's
    # second byte: its write moves entry 1 from item 1 (0x280) to 0x202, and the second step
    # updates the item there.
    alias = VECTOR + 8 - ENABLED
    moved = (item(1) & ~0xff) | (item(1) >> 8 & 0xff)
    yield case('write-moves-next-entry', [
        fixed_pointer(BEGIN, VECTOR), fixed_pointer(END, VECTOR + 16),
        fixed_pointer(VECTOR, alias), fixed_pointer(VECTOR + 8, item(1)),
        states(item(1), 0, 1), states(moved, 0x80, 0xff)], fixed=True)

    # The first item has its enabled byte on the end's low byte and its requested byte on the
    # end's second byte: its write moves the end from VECTOR + 16 to VECTOR + 1, but the count
    # (two) was taken before, so item 1 is updated too.
    yield case('write-moves-end', [
        fixed_pointer(BEGIN, VECTOR), fixed_pointer(END, VECTOR + 16),
        fixed_pointer(VECTOR, END - ENABLED), fixed_pointer(VECTOR + 8, item(1)),
        states(item(1), 0, 1)], fixed=True)


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    generated = list(cases())
    assert len(generated) == len({row['id'] for row in generated})
    assert all('_' not in row['id'] for row in generated)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for row in generated:
        (out / (row['id'] + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(f'{len(generated)} synthetic cases written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
