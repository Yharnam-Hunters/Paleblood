#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_add_page.cpp, for tools/verify.py run.

usage: scaleform_heap_add_page_cases.py OUT_DIR add_page|add_page_locked

The engine (`eng`: heap at +8, the ring of pages at +0x220, page bytes at +0x230, limit at +0x240,
limit handler at +0x248) and the retry flag (`retry`, pre-filled so every store shows) are buffers.
The limit handler (`lim`, its vtable `vt` with slot 2 at a scripted address), the page manager's
page (a descriptor in `pages` or `low`, or none) and the bins' page add are scripted; so is the page
manager's lock (the page manager is `pm`: its mutex at +8, the page table entries in use at +0x20).
Global page table entries point at `pages` (entry 2 by default) with descriptor masks, and at
arrays that miss the page (above it, so its index is negative, or too small a mask).

- the limit: none, exactly reached (no call), passed with the handler answering 1, 0 or a value
  whose low byte is 0, no handler, a page count that wraps, a handler already in a call;
- the page: none (with and without the handler), found in the first entry, a later one after
  entries that miss it (negative index, index one past the mask), at the mask itself, at a
  non-whole descriptor offset, in no entry in use (none, or only past the entries in use);
- the ring: empty, or with pages already in it.
"""
import json
import os
import struct
import sys

ADDRESSES = {'add_page': '0x0233bbe0', 'add_page_locked': '0x0233bd20'}
ACQUIRE_PAGE, BINS_ADD_PAGE, HANDLER = '0x0233e3a0', '0x0233a230', '0x00500200'
PAGE_MANAGER, PAGE_TABLE = 0x05940360, 0x05994470
RING, TAKEN, LIMIT, LIMIT_HANDLER = 0x220, 0x230, 0x240, 0x248

LOCK = {'name': 'pthread_mutex_lock', 'argc': 1, 'ret': 0}
UNLOCK = {'name': 'pthread_mutex_unlock', 'argc': 1, 'ret': 0}


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


DEFAULT_TABLE = {2: ('pages', 0xff)}


def case(variant, cid, taken=0x3000, limit=0, handler=None, page='pages+0x60', table=None, entries=4,
         ring=0, calls_in_progress=0):
    """handler: the limit handler's answer (None: no handler). page: the found descriptor or None.
    table: {entry: (buffer offset, index mask)}. ring: pages already in the engine's ring."""
    locked = variant == 'add_page_locked'
    memory = [{'addr': hex(PAGE_MANAGER), 'pointer': 'pm'},
              {'addr': 'buf:pm+32', 'bytes': u32(entries)},
              {'addr': 'buf:eng+8', 'pointer': 'heap'},
              {'addr': f'buf:eng+{TAKEN}', 'bytes': u64(taken)},
              {'addr': f'buf:eng+{LIMIT}', 'bytes': u64(limit)},
              {'addr': 'buf:retry+0', 'bytes': '5a5a5a5a5a5a5a5a'}]
    for entry, (where, mask) in (DEFAULT_TABLE if table is None else table).items():
        memory += [{'addr': hex(PAGE_TABLE + 16 * entry), 'pointer': where},
                   {'addr': hex(PAGE_TABLE + 16 * entry + 8), 'bytes': u64(mask)}]
    # The ring: its node in the engine, then `ring` pages of `old` in order.
    nodes = [f'eng+{RING}'] + [f'old+{0x20 * i}' for i in range(ring)]
    for i, node in enumerate(nodes):
        buf, off = node.split('+')
        memory += [{'addr': f'buf:{buf}+{off}', 'pointer': nodes[(i + 1) % len(nodes)]},
                   {'addr': f'buf:{buf}+{int(off) + 8}', 'pointer': nodes[i - 1]}]
    stubs = []
    imports = []

    asks = limit != 0 and handler is not None and (taken + 0x1000) % (1 << 64) > limit
    if handler is not None:
        memory += [{'addr': f'buf:eng+{LIMIT_HANDLER}', 'pointer': 'lim'},
                   {'addr': 'buf:lim+0', 'pointer': 'vt'},
                   {'addr': 'buf:lim+8', 'bytes': u32(calls_in_progress)},
                   {'addr': 'buf:vt+16', 'guest': HANDLER}]
    if asks:
        stubs.append({'address': HANDLER, 'argc': 3, 'ret': handler})
        if locked:
            imports += [UNLOCK, LOCK]
    if not locked:
        imports.append(LOCK)
    stubs.append({'address': ACQUIRE_PAGE, 'argc': 2, 'ret': f'buf:{page}' if page else 0})
    if page:
        stubs.append({'address': BINS_ADD_PAGE, 'argc': 3})
    if not locked:
        imports.append(UNLOCK)
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:eng', 'rsi': 'buf:retry'},
            'buffers': {'eng': {'size': 0x260}, 'retry': {'size': 8}, 'pm': {'size': 0x30},
                        'heap': {'size': 0x10}, 'pages': {'size': 0x400}, 'low': {'size': 0x100},
                        'old': {'size': 0x60}, 'lim': {'size': 0x10}, 'vt': {'size': 0x18}},
            'memory': memory, 'stubs': stubs, 'imports': imports}


def cases(variant):
    c = lambda cid, **kw: case(variant, cid, **kw)  # noqa: E731
    return [
        c('no_limit', handler=1),
        c('no_limit_no_handler'),
        c('limit_reached_exactly', limit=0x4000, handler=1),
        c('limit_passed_handler_true', limit=0x3800, handler=1),
        c('limit_passed_handler_false', limit=0x3800, handler=0),
        c('limit_passed_handler_high_byte', limit=0x1000, handler=0x100),
        c('limit_passed_no_handler', limit=0x1000),
        c('limit_passed_in_call', limit=0x2000, handler=1, calls_in_progress=3),
        c('limit_below_taken', taken=0x9000, limit=0x10, handler=1),
        c('taken_wraps_past_limit', taken=-0x800, limit=0x100, handler=1),
        c('taken_wraps_under_limit', taken=-0x800, limit=0x900, handler=1),
        c('no_page', page=None),
        c('no_page_after_handler', limit=0x1000, handler=1, page=None),
        c('first_entry', table={0: ('pages', 0xff)}),
        c('after_negative_index', table={0: ('pages+0x100', 0xff), 1: ('pages', 0x2), 2: ('pages', 0xff)}),
        c('index_one_past_mask', table={1: ('pages', 0x2), 3: ('pages+0x20', 0x2)}),
        c('index_at_mask', table={1: ('pages', 0x3)}),
        c('partial_descriptor_offset', page='pages+0x64', table={1: ('pages', 0xff)}),
        c('partial_offset_high_bits', page='pages+0x3e4', table={1: ('pages+0x8', 0xff)}),
        c('low_array', page='low+0x40', table={0: ('pages', 0x1f), 3: ('low', 0x7)}),
        c('no_entry_holds', table={0: ('pages+0x100', 0xff), 1: ('pages', 0x1)}),
        c('no_entries_in_use', entries=0),
        c('holding_entry_not_in_use', entries=2),
        c('holding_entry_last_in_use', entries=3),
        c('ring_one_page', ring=1),
        c('ring_three_pages', ring=3, limit=0x9000, handler=0),
    ]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = cases(sys.argv[2])
    os.makedirs(sys.argv[1], exist_ok=True)
    for one in out:
        with open(os.path.join(sys.argv[1], f"{one['id']}.json"), 'w') as f:
            json.dump(one, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
