#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for game/engine/scaleform_heap_large_blocks.cpp, for tools/verify.py run.

usage: scaleform_heap_large_blocks_cases.py OUT_DIR allocate_large|release_page

allocate_large: the engine (`eng`: heap root at +0, heap at +8, page bytes at +0x230, used bytes at
+0x238, limit at +0x240, limit handler at +0x248, allocation count at +0x250), the retry flag and
the report (both pre-filled so every store shows) are buffers. The heap root (`root`) holds the
arenas' allocators from +0x10, the heap (`heap`) its arena at +0xb8. The arena allocator's slot 11
and the limit handler's slot 2 are scripted, and so is the page manager's lock (the page manager is
`pm`: its mutex at +8, the tree's root at +0x28). Tree nodes are in `tree`, 0x20 bytes each: a
chain whose nodes point at the next one from both sides (or one side) and whose last has no
children, so the new node goes in by the key bit at the chain's depth.

- the request: 0, rounded up to 8 or already a multiple;
- the alignment: 1, 2, 4, 8, 16 (flags), 0, 3, 32 and one with the top bit set (stored), above 16
  (the extra word);
- the limit: none, reached exactly, passed with the handler answering 1, 0, or a value whose low
  byte is 0, passed with no handler, page bytes that wrap, a handler already in a call;
- the allocation failing (after the handler too);
- the arena: 0, a later one, a negative one; a heap pointer with low bits set;
- the tree: empty, a chain ending on either side, a level with one child, the node already in the
  tree (as the root or deeper).

release_page: the engine (`eng`: the bins at +0x18, the ring of pages at +0x220, page bytes at
+0x230), the page's descriptor (`pg`) and its 4 KiB in `mem` (boundary at mem+0x1000) are buffers;
the bins' page removal, the page manager's allocator (slot 5) and lock are scripted. The page
manager (`pm`) holds its allocator at +0 and its ring of spare descriptors at +0x10.

- the lock flag: 1, 0, values whose low byte is 0 or not;
- the page's base: headers on both sides, only above (base at or one unit before the boundary, at
  a machine page), only below, exactly one header's room below or above;
- the engine's ring: the page alone, or between others;
- the spare descriptors: none, or some;
- page bytes that wrap.
"""
import json
import os
import struct
import sys

ADDRESSES = {'allocate_large': '0x0233c660', 'release_page': '0x0233be70'}
BINS_REMOVE_PAGE = '0x0233a630'
ALLOCATE, HANDLER, FREE = '0x00500200', '0x00500300', '0x00500200'
PAGE_MANAGER = 0x05940360
RING, TAKEN, USED, LIMIT, LIMIT_HANDLER, COUNT = 0x220, 0x230, 0x238, 0x240, 0x248, 0x250
NODE = 0x20

LOCK = {'name': 'pthread_mutex_lock', 'argc': 1, 'ret': 0}
UNLOCK = {'name': 'pthread_mutex_unlock', 'argc': 1, 'ret': 0}
FILL = '5a' * 8


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def tree_memory(chain, sides):
    """A chain of `chain` nodes in `tree`; sides[i]: which children of node i point at node i + 1
    ('both', 'left' or 'right'). The last node has none."""
    memory = []
    for i in range(chain - 1):
        side = sides.get(i, 'both')
        for child, name in ((0, 'left'), (1, 'right')):
            if side in ('both', name):
                memory.append({'addr': f'buf:tree+{NODE * i + 8 + 8 * child}', 'pointer': f'tree+{NODE * (i + 1)}'})
    return memory


def allocate_case(cid, request=0x300, alignment=8, taken=0x5000, limit=0, handler=None, block=True, arena=0,
                  heap_offset=0, chain=0, sides=None, root=None, calls_in_progress=0, extra=()):
    """handler: the limit handler's answer (None: no handler). chain: tree nodes (0: empty tree).
    root: the tree's root as a buffer reference, instead of the chain's first node."""
    length = (request + 7) & ~7
    total = length + NODE + (8 if alignment % (1 << 64) > 16 else 0)
    memory = [{'addr': hex(PAGE_MANAGER), 'pointer': 'pm'},
              {'addr': 'buf:eng+0', 'pointer': 'root'},
              {'addr': 'buf:eng+8', 'pointer': f'heap+{heap_offset}'},
              {'addr': f'buf:heap+{heap_offset + 0xb8}', 'bytes': u32(arena)},
              {'addr': f'buf:root+{0x10 + 8 * arena}', 'pointer': 'alloc'},
              {'addr': 'buf:alloc+0', 'pointer': 'avt'},
              {'addr': 'buf:avt+88', 'guest': ALLOCATE},
              {'addr': f'buf:eng+{TAKEN}', 'bytes': u64(taken)},
              {'addr': f'buf:eng+{USED}', 'bytes': u64(0x1230)},
              {'addr': f'buf:eng+{LIMIT}', 'bytes': u64(limit)},
              {'addr': f'buf:eng+{COUNT}', 'bytes': u64(7)},
              {'addr': 'buf:retry+0', 'bytes': FILL},
              {'addr': 'buf:info+0', 'bytes': FILL * 3},
              # stale words where the node goes, so its clearing shows
              {'addr': f'buf:blk+{length}', 'bytes': FILL * 6}]
    if root is not None:
        memory.append({'addr': 'buf:pm+40', 'pointer': root})
    elif chain:
        memory.append({'addr': 'buf:pm+40', 'pointer': 'tree+0'})
    memory += tree_memory(chain, sides or {}) + list(extra)
    imports, stubs = [], []
    asks = limit != 0 and handler is not None and (taken + total) % (1 << 64) > limit
    if handler is not None:
        memory += [{'addr': f'buf:eng+{LIMIT_HANDLER}', 'pointer': 'lim'},
                   {'addr': 'buf:lim+0', 'pointer': 'lvt'},
                   {'addr': 'buf:lim+8', 'bytes': u32(calls_in_progress)},
                   {'addr': 'buf:lvt+16', 'guest': HANDLER}]
    if asks:
        imports += [UNLOCK, LOCK]
        stubs.append({'address': HANDLER, 'argc': 3, 'ret': handler})
    stubs.append({'address': ALLOCATE, 'argc': 3, 'ret': 'buf:blk' if block else 0})
    return {'schema': 1, 'address': ADDRESSES['allocate_large'], 'id': cid, 'returns': 'i64',
            'args': {'rdi': 'buf:eng', 'rsi': request, 'rdx': alignment, 'rcx': 'buf:retry', 'r8': 'buf:info'},
            'buffers': {'eng': {'size': 0x260}, 'root': {'size': 0x40}, 'heap': {'size': 0xc8},
                        'alloc': {'size': 8}, 'avt': {'size': 0x60}, 'lim': {'size': 0x10}, 'lvt': {'size': 0x18},
                        'pm': {'size': 0x30}, 'tree': {'size': 0x800}, 'blk': {'size': 0x400},
                        'retry': {'size': 8}, 'info': {'size': 0x18}},
            'memory': memory, 'stubs': stubs, 'imports': imports}


def allocate_cases():
    c = allocate_case
    return [
        c('request_multiple_of_8'),
        c('request_rounded', request=0x2f9),
        c('request_zero', request=0),
        c('request_one_short', request=0x207),
        c('alignment_1', alignment=1),
        c('alignment_2', alignment=2),
        c('alignment_4', alignment=4),
        c('alignment_16', alignment=16),
        c('alignment_0_stored', alignment=0),
        c('alignment_3_stored', alignment=3),
        c('alignment_12_stored', alignment=12),
        c('alignment_17_extra_word', alignment=17),
        c('alignment_32_extra_word', alignment=32),
        c('alignment_top_bit', alignment=-16),
        c('no_limit_with_handler', handler=1),
        c('limit_reached_exactly', limit=0x5000 + 0x320, handler=1),
        c('limit_passed_handler_true', limit=0x5100, handler=1),
        c('limit_passed_handler_false', limit=0x5100, handler=0),
        c('limit_passed_handler_high_byte', limit=0x5000, handler=0x100),
        c('limit_passed_wide_alignment', limit=0x5000 + 0x320, alignment=64, handler=1),
        c('limit_passed_no_handler', limit=0x100),
        c('limit_passed_in_call', limit=0x10, handler=1, calls_in_progress=4),
        c('taken_wraps_past_limit', taken=-0x100, limit=0x100, handler=1),
        c('taken_wraps_under_limit', taken=-0x100, limit=0x300, handler=1),
        c('allocation_fails', block=False),
        c('allocation_fails_after_handler', block=False, limit=0x100, handler=1),
        c('arena_later', arena=3),
        c('arena_negative', arena=-1),
        c('heap_low_bits', heap_offset=1, alignment=8),
        c('heap_low_bits_stored', heap_offset=3, alignment=64),
        c('tree_one_node', chain=1),
        c('tree_chain_bit_35_right', chain=29),
        c('tree_chain_bit_34_left', chain=30),
        c('tree_chain_bit_32_right', chain=32),
        c('tree_chain_low_bits', chain=50),
        c('tree_one_side_right', chain=31, sides={28: 'right'}),
        c('tree_one_side_left', chain=31, sides={i: 'left' for i in range(28)}),
        c('tree_node_is_root', root='blk+768'),
        c('tree_node_deeper', chain=1, extra=[{'addr': 'buf:tree+8', 'pointer': 'blk+768'},
                                              {'addr': 'buf:tree+16', 'pointer': 'blk+768'}]),
    ]


def release_case(cid, locked=1, base=0x800, ring=0, ring_at=0, spares=0, taken=0x9000):
    """ring: other pages in the engine's ring (in `old`), the page at position ring_at among them.
    spares: descriptors in the page manager's spare ring (in `spare`)."""
    memory = [{'addr': hex(PAGE_MANAGER), 'pointer': 'pm'},
              {'addr': 'buf:pm+0', 'pointer': 'palloc'},
              {'addr': 'buf:palloc+0', 'pointer': 'pvt'},
              {'addr': 'buf:pvt+40', 'guest': FREE},
              {'addr': f'buf:eng+{TAKEN}', 'bytes': u64(taken)},
              {'addr': 'buf:pg+16', 'pointer': 'heap'},
              {'addr': 'buf:pg+24', 'pointer': f'mem+{base}'}]
    # header slots and their neighbours hold stale bytes, so the clearing shows
    for where in (0, 0xfd0, 0xfe0, 0xff0, 0x1000, 0x1010, 0x1020):
        memory.append({'addr': f'buf:mem+{where}', 'bytes': FILL * 2})
    others = [f'old+{0x20 * i}' for i in range(ring)]
    nodes = [f'eng+{RING}'] + others[:ring_at] + ['pg+0'] + others[ring_at:]
    for i, node in enumerate(nodes):
        buf, off = node.split('+')
        memory += [{'addr': f'buf:{buf}+{off}', 'pointer': nodes[(i + 1) % len(nodes)]},
                   {'addr': f'buf:{buf}+{int(off) + 8}', 'pointer': nodes[i - 1]}]
    spare_nodes = ['pm+16'] + [f'spare+{0x20 * i}' for i in range(spares)]
    for i, node in enumerate(spare_nodes):
        buf, off = node.split('+')
        memory += [{'addr': f'buf:{buf}+{off}', 'pointer': spare_nodes[(i + 1) % len(spare_nodes)]},
                   {'addr': f'buf:{buf}+{int(off) + 8}', 'pointer': spare_nodes[i - 1]}]
    imports = [] if locked & 0xff else [LOCK, UNLOCK]
    stubs = [{'address': BINS_REMOVE_PAGE, 'argc': 2}, {'address': FREE, 'argc': 4}]
    return {'schema': 1, 'address': ADDRESSES['release_page'], 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:eng', 'rsi': 'buf:pg', 'rdx': locked},
            'buffers': {'eng': {'size': 0x260}, 'pg': {'size': 0x20}, 'heap': {'size': 0x10},
                        'mem': {'size': 0x2000}, 'old': {'size': 0x60}, 'spare': {'size': 0x60},
                        'pm': {'size': 0x30}, 'palloc': {'size': 8}, 'pvt': {'size': 0x30}},
            'memory': memory, 'stubs': stubs, 'imports': imports}


def release_cases():
    c = release_case
    return [
        c('locked_both_headers'),
        c('unlocked_both_headers', locked=0),
        c('flag_low_byte_zero', locked=0x100),
        c('flag_low_byte_set', locked=0x201),
        c('flag_other_value', locked=0x80),
        c('base_unaligned', base=0x7f3),
        c('base_at_machine_page', base=0x1000),
        c('base_at_mem_start', base=0),
        c('base_one_unit_below_boundary', base=0xff0),
        c('base_below_boundary_unaligned', base=0xff8, locked=0),
        c('header_room_below_two_units', base=0xfe0),
        c('only_lower_header', base=0x8),
        c('upper_room_one_unit', base=0x10),
        c('upper_room_two_units', base=0x20),
        c('ring_first_of_three', ring=2, ring_at=0),
        c('ring_middle', ring=2, ring_at=1, locked=0),
        c('ring_last', ring=3, ring_at=3),
        c('spares_some', spares=2),
        c('spares_some_unlocked', spares=3, locked=0, ring=1),
        c('taken_wraps', taken=0),
        c('taken_one_page', taken=0x1000, locked=0),
    ]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = allocate_cases() if sys.argv[2] == 'allocate_large' else release_cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for one in out:
        with open(os.path.join(sys.argv[1], f"{one['id']}.json"), 'w') as f:
            json.dump(one, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
