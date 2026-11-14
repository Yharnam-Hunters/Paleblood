#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the frame update's list splice and object update and the swap-chain level copy
(game/render/update_list_and_level_copy.cpp), for tools/verify.py run.

usage: update_list_and_level_copy_cases.py OUT_DIR list|object|copy

- list: circular lists with a sentinel (next +0x0, prev +0x8, value +0x10) and a header (sentinel
  +0x8, size +0x10, allocator +0x18). One heap: whole lists, a middle range, an empty range, a
  move inside one list, the size limit met exactly and passed only by a list moving into itself,
  heap identities equal in their low 32 bits only. Two heaps: every value inserted (scripted, and
  once the real insert with a scripted node allocation), then a whole list emptied and freed, an
  empty list, ranges from the start, to the end, in the middle and across the sentinel (never
  freed), and one list on two heaps.
- object: every state (idle, running, finishing, done, others) with the finish and notify
  requests, a start that fails, succeeds with a value above the low byte or only in the upper
  half, or changes the state or flags itself, an update that requests a finish, the notify
  parameter set (and none created). The visible flag from both owner queries (0, 1, 2 and a value
  above the low byte; unchanged, raised, dropped), and from the masks of the owner's view, of the
  render manager's view source, of the defaults, with overlap in each lane of each mask, empty
  masks that are not zero, and a missing render manager (reported, then read again).
- copy: a null level, plain and block-compressed formats (each end of the block range, a value
  above the low byte, zero), the blocked level flag, widths and heights 0 to 5 and at the 32-bit
  wrap, a product past 32 bits, and a failed box copy.
"""
import json
import os
import struct
import sys

ADDRESSES = {'list': '0x02379d60', 'object': '0x02373850', 'copy': '0x014a3410'}
RETURNS = {'list': 'void', 'object': 'i8', 'copy': 'i32'}
GUARD_GOT = '0x057e3be0'
INSERT, LENGTH_ERROR, FATAL_ERROR = '0x02379f00', '0x02fbf1b8', '0x024b55b0'
SELF_HEAP, OTHER_HEAP, FREE, ALLOCATE = '0x00400100', '0x00400140', '0x00400180', '0x004001c0'
START, UPDATE, FINISH, APPLY = '0x00400200', '0x00400240', '0x00400280', '0x004002c0'
QUERY_FIRST, QUERY_SECOND, HEAP_FREE = '0x00400300', '0x00400340', '0x00400380'
CREATE_SET, SET_PARAMETER, CLEAR_SET, HEAP_OWNING = '0x02c51c30', '0x02c51f60', '0x02c51fc0', '0x0247b720'
REND_MAN, EMPTY_MASKS, DEFAULT_MASKS, ALLOCATOR = '0x05940298', '0x0593e950', '0x0593e970', '0x05940420'
COPY_BOX = '0x014a31a0'
SINGLETON_FILE = '0x04d3b369'  # the fatal error's first argument: the rend man slot is reached from it
LIST_MAX = 0x0aaaaaaaaaaaaaa9


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def base(variant, cid):
    return {'schema': 1, 'address': ADDRESSES[variant], 'id': cid, 'returns': RETURNS[variant], 'args': {},
            'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}


# ---- list ------------------------------------------------------------------------------------

def ring(c, name, count, size=None, heap=SELF_HEAP):
    """A list `name` with `count` nodes `name_0`..; returns the node names, sentinel first."""
    b, m = c['buffers'], c['memory']
    nodes = [f'{name}_s'] + [f'{name}_{i}' for i in range(count)]
    b[name] = {'size': 0x20}
    b[name + '_alloc'] = {'size': 8}
    b[name + '_alloc_vt'] = {'size': 0x78}
    m += [{'addr': f'buf:{name}', 'bytes': q(0x5050505050505050)},
          {'addr': f'buf:{name}+8', 'pointer': nodes[0]},
          {'addr': f'buf:{name}+16', 'bytes': q(count if size is None else size)},
          {'addr': f'buf:{name}+24', 'pointer': name + '_alloc'},
          {'addr': f'buf:{name}_alloc', 'pointer': name + '_alloc_vt'},
          {'addr': f'buf:{name}_alloc_vt+16', 'guest': heap},
          {'addr': f'buf:{name}_alloc_vt+88', 'guest': ALLOCATE},
          {'addr': f'buf:{name}_alloc_vt+112', 'guest': FREE}]
    for i, n in enumerate(nodes):
        b[n] = {'size': 0x18}
        nxt = nodes[(i + 1) % len(nodes)]
        m += [{'addr': f'buf:{n}', 'pointer': nxt}, {'addr': f'buf:{nxt}+8', 'pointer': n},
              {'addr': f'buf:{n}+16', 'bytes': q(0x1000 * (ord(name[0]) + 1) + i)}]
    return nodes


def node_at(nodes, index):
    """index into the list in order (0 = first element); len = the sentinel (end)."""
    return nodes[0] if index >= len(nodes) - 1 else nodes[index + 1]


def list_case(cid, own=2, theirs=3, first=0, last=None, where=None, count=None, same_heap=True, same_list=False,
              own_size=None, heap_ids=None, real_insert=False):
    c = base('list', cid)
    mine = ring(c, 'self', own, own_size)
    other_name = 'self' if same_list else 'other'
    theirs_nodes = mine if same_list else ring(c, 'other', theirs, heap=SELF_HEAP if same_heap else OTHER_HEAP)
    n_other = len(theirs_nodes) - 1
    last = n_other if last is None else last
    where = own if where is None else where
    first_node, last_node = node_at(theirs_nodes, first), node_at(theirs_nodes, last)
    count = (last - first) % (n_other + 1) if count is None else count
    c['args'] = {'rdi': 'buf:self', 'rsi': f'buf:{node_at(mine, where)}', 'rdx': f'buf:{other_name}',
                 'rcx': f'buf:{first_node}', 'r8': f'buf:{last_node}', 'r9': count}
    ids = heap_ids or ((7, 7) if same_heap else (7, 9))
    c['stubs'].append({'address': SELF_HEAP, 'argc': 1, 'ret': ids[0]})
    if same_list:
        c['stubs'].append({'address': SELF_HEAP, 'argc': 1, 'ret': ids[1]})
    else:
        c['stubs'].append({'address': SELF_HEAP if same_heap else OTHER_HEAP, 'argc': 1, 'ret': ids[1]})
    if (ids[0] & 0xffffffff) == (ids[1] & 0xffffffff):
        return c
    # Values inserted here: the walk from first to last (the sentinel's value too, if crossed).
    walk, i = [], theirs_nodes.index(first_node)
    while theirs_nodes[i] != last_node:
        walk.append(theirs_nodes[i])
        i = (i + 1) % len(theirs_nodes)
    for k, n in enumerate(walk):
        if real_insert:
            c['buffers'][f'new_{k}'] = {'size': 0x18}
            c['stubs'].append({'address': ALLOCATE, 'argc': 3, 'ret': f'buf:new_{k}'})
        else:
            c['stubs'].append({'address': INSERT, 'argc': 3})
    whole = first_node == theirs_nodes[1 % len(theirs_nodes)] and last_node == theirs_nodes[0]
    freed = [n for n in walk if n != theirs_nodes[0]] if not whole else walk
    c['stubs'] += [{'address': FREE, 'argc': 2} for _ in freed]
    return c


def list_cases():
    return [
        list_case('one_heap_whole_into_empty', own=0, theirs=3),
        list_case('one_heap_whole_into_middle', own=3, theirs=2, where=1),
        list_case('one_heap_middle_range', own=2, theirs=5, first=1, last=4, where=0),
        list_case('one_heap_empty_range', own=2, theirs=3, first=1, last=1),
        list_case('one_heap_empty_other_list', own=2, theirs=0),
        list_case('one_heap_count_differs_from_range', own=1, theirs=3, first=0, last=2, count=11),
        list_case('one_heap_size_limit_met', own=1, theirs=2, own_size=LIST_MAX - 2, count=2),
        list_case('one_heap_size_limit_zero_count', own=1, theirs=2, own_size=LIST_MAX, count=0),
        list_case('one_list_moves_inside', own=5, first=3, last=5, where=1, same_list=True, count=1 << 63),
        list_case('one_list_huge_size', own=3, first=0, last=1, where=3, same_list=True, own_size=-1, count=9),
        list_case('heaps_equal_low_bits', own=1, theirs=2, heap_ids=(0x100000005, 0x5)),
        list_case('heaps_equal_low_bits_other', own=1, theirs=2, heap_ids=(0x5, 0xffffffff00000005)),
        list_case('two_heaps_whole_list', own=2, theirs=3, same_heap=False),
        list_case('two_heaps_whole_list_into_empty', own=0, theirs=1, same_heap=False),
        list_case('two_heaps_empty_list', own=2, theirs=0, same_heap=False),
        list_case('two_heaps_from_start', own=1, theirs=4, first=0, last=2, same_heap=False),
        list_case('two_heaps_to_end', own=1, theirs=4, first=2, same_heap=False),
        list_case('two_heaps_middle', own=1, theirs=5, first=1, last=3, same_heap=False, count=77),
        list_case('two_heaps_empty_range', own=1, theirs=3, first=1, last=1, same_heap=False),
        list_case('two_heaps_across_sentinel', own=1, theirs=4, first=2, last=1, same_heap=False),
        list_case('two_heaps_heap_ids_differ_high', own=1, theirs=2, same_heap=False, heap_ids=(5, 6 | 1 << 32)),
        list_case('two_heaps_real_insert', own=2, theirs=2, same_heap=False, real_insert=True),
        list_case('one_list_two_heaps', own=4, first=1, last=3, same_list=True, heap_ids=(1, 2)),
    ]


# ---- object ----------------------------------------------------------------------------------

def object_case(cid, state=0, flags=0, start=None, start_writes=None, update_writes=None, owner=None, query=0,
                view=None, notify_set=True, obj_masks=None, empty=None, defaults=None, rendman='source'):
    """owner: None, 'first', 'second' (owner queries) or 'view' / 'rendman' (masks)."""
    c = base('object', cid)
    b, m, s = c['buffers'], c['memory'], c['stubs']
    b['obj'] = {'size': 0x50}
    b['obj_vt'] = {'size': 0x50}
    b['step'] = {'size': 8}
    c['args'] = {'rdi': 'buf:obj', 'rsi': 'buf:step'}
    m += [{'addr': 'buf:obj', 'pointer': 'obj_vt'}, {'addr': 'buf:obj+8', 'bytes': u32(1)},
          {'addr': 'buf:obj+12', 'bytes': u32(state)}, {'addr': 'buf:obj+16', 'bytes': f'{flags:02x}'},
          {'addr': 'buf:obj_vt+32', 'guest': START}, {'addr': 'buf:obj_vt+40', 'guest': UPDATE},
          {'addr': 'buf:obj_vt+48', 'guest': FINISH}, {'addr': 'buf:obj_vt+72', 'guest': APPLY}]
    masks = obj_masks or [0x0000000f, 0x000000f0, 0x00000f00, 0x0000f000, 0x000f0000, 0x00f00000, 0x0f000000,
                          0xf0000000]
    m.append({'addr': 'buf:obj+48', 'bytes': ''.join(u32(v) for v in masks)})
    m.append({'addr': EMPTY_MASKS, 'bytes': ''.join(u32(v) for v in (empty or [0] * 8))})
    m.append({'addr': DEFAULT_MASKS, 'bytes': ''.join(u32(v) for v in (defaults or [0] * 8))})
    if owner:
        b['owner'] = {'size': 0x78}
        b['owner_vt'] = {'size': 0x50}
        m += [{'addr': 'buf:obj+24', 'pointer': 'owner'}, {'addr': 'buf:owner', 'pointer': 'owner_vt'},
              {'addr': 'buf:owner_vt+64', 'guest': QUERY_FIRST}, {'addr': 'buf:owner_vt+72', 'guest': QUERY_SECOND}]
        if owner in ('first', 'second'):
            s.append({'address': QUERY_FIRST if owner == 'first' else QUERY_SECOND, 'argc': 1, 'ret': query})
        elif owner == 'view':
            b['view'] = {'size': 0x28}
            m += [{'addr': 'buf:owner+112', 'pointer': 'view'},
                  {'addr': 'buf:view+8', 'bytes': ''.join(u32(v) for v in view)}]
        else:
            if rendman is None:
                m.append({'addr': REND_MAN, 'bytes': q(0)})
                b['rendman'] = {'size': 0x18}
                s.append({'address': FATAL_ERROR, 'argc': 4,
                          'writes': [{'arg': 0, 'offset': int(REND_MAN, 16) - int(SINGLETON_FILE, 16), 'pointer': 'rendman'}]})
            else:
                b['rendman'] = {'size': 0x18}
                m.append({'addr': REND_MAN, 'pointer': 'rendman'})
            if rendman == 'source':
                b['vsrc'] = {'size': 0x500}
                m += [{'addr': 'buf:rendman+16', 'pointer': 'vsrc'},
                      {'addr': 'buf:vsrc+1248', 'bytes': ''.join(u32(v) for v in view)}]
    # Life cycle.
    flags_now = flags
    if owner in ('first', 'second') and ((flags >> 2) & 1) != (query & 0xff):
        flags_now = (flags & 0xeb) | ((query << 2) & 0xff) | 0x10
    if state == 1:
        if not flags_now & 2:
            entry = {'address': UPDATE, 'argc': 2}
            if update_writes is not None:
                entry['writes'] = [{'arg': 0, 'offset': 16, 'bytes': f'{update_writes:02x}'}]
                flags_now = update_writes
            s.append(entry)
            if not flags_now & 2 and flags_now & 1:
                b['params'] = {'size': 0x20}
                if notify_set:
                    b['heap'] = {'size': 8}
                    b['heap_vt'] = {'size': 0x78}
                    m += [{'addr': 'buf:heap', 'pointer': 'heap_vt'}, {'addr': 'buf:heap_vt+112', 'guest': HEAP_FREE}]
                    b['alloc'] = {'size': 8}
                    m.append({'addr': ALLOCATOR, 'pointer': 'alloc'})
                    s += [{'address': CREATE_SET, 'argc': 3, 'ret': 'buf:params'},
                          {'address': SET_PARAMETER, 'argc': 3},
                          {'address': APPLY, 'argc': 2},
                          {'address': HEAP_OWNING, 'argc': 1, 'ret': 'buf:heap'},
                          {'address': CLEAR_SET, 'argc': 1},
                          {'address': HEAP_FREE, 'argc': 2}]
                else:
                    m.append({'addr': ALLOCATOR, 'bytes': q(0x1234)})
                    s.append({'address': CREATE_SET, 'argc': 3, 'ret': 0})
        s.append({'address': FINISH, 'argc': 1})
    elif state == 0:
        if not flags_now & 3:
            entry = {'address': START, 'argc': 1, 'ret': 0 if start is None else start}
            if start_writes:
                entry['writes'] = [{'arg': 0, 'offset': off, 'bytes': v} for off, v in start_writes]
            s.append(entry)
        s.append({'address': FINISH, 'argc': 1})
    else:
        s.append({'address': FINISH, 'argc': 1})
    return c


VIEW_NONE = [0xf0f0f0f0, 0xffff0fff, 0xfff0ffff, 0x0fffffff, 0xfff0ffff, 0xff0fffff, 0xf0ffffff, 0x0fffffff]


def lane_hit(k):
    v = list(VIEW_NONE)
    v[k] |= [0xf, 0xf0, 0xf00, 0xf000, 0xf0000, 0xf00000, 0xf000000, 0xf0000000][k]
    return v


def object_cases():
    out = []
    for st in (0, 1, 2, 3, 4, 0xffffffff):
        for fl in (0, 1, 2, 3, 0x18, 0xff):
            out.append(object_case(f'state_{st:x}_flags_{fl:02x}', state=st, flags=fl, start=0))
    out += [
        object_case('idle_start_succeeds', start=1),
        object_case('idle_start_value_above_low_byte', start=0x100),
        object_case('idle_start_only_upper_half', start=1 << 32),
        object_case('idle_start_fails_sets_done', start=0, start_writes=[(12, u32(3))]),
        object_case('idle_start_fails_sets_finishing', start=0, start_writes=[(12, u32(2))]),
        object_case('idle_start_fails_sets_running', start=0, start_writes=[(12, u32(1))]),
        object_case('idle_start_requests_finish', start=1, start_writes=[(16, '02')]),
        object_case('idle_start_requests_notify', start=1, start_writes=[(16, '01')]),
        object_case('running_update_requests_finish', state=1, update_writes=0x1a),
        object_case('running_update_requests_notify', state=1, update_writes=0x19),
        object_case('running_notify', state=1, flags=0x1d),
        object_case('running_notify_no_set', state=1, flags=0x01, notify_set=False),
        object_case('running_update_clears_requests', state=1, flags=0x1c, update_writes=0x04),
    ]
    for which in ('first', 'second'):
        bit = 0x40 if which == 'first' else 0
        for q_ in (0, 1, 2, 0x100, 0xff):
            for vis in (0, 4):
                out.append(object_case(f'owner_{which}_query_{q_:x}_visible_{vis}', state=1, flags=0x20 | bit | vis,
                                       owner=which, query=q_))
    out.append(object_case('owner_query_then_start', flags=0x60, owner='first', query=1, start=1))
    out.append(object_case('owner_query_then_done', state=3, flags=0x24, owner='second', query=0))
    for vis in (0, 4):
        out.append(object_case(f'view_no_overlap_visible_{vis}', state=1, flags=vis, owner='view', view=VIEW_NONE))
        for k in range(8):
            out.append(object_case(f'view_lane_{k}_visible_{vis}', state=1, flags=vis, owner='view', view=lane_hit(k)))
    out += [
        object_case('view_empty_masks_nonzero_match', state=1, owner='view', view=[0xffffffff] * 8,
                    empty=[0xf, 0xf0, 0xf00, 0xf000, 0xf0000, 0xf00000, 0xf000000, 0xf0000000], flags=4),
        object_case('view_empty_masks_nonzero_miss', state=1, owner='view', view=[0xffffffff] * 8,
                    empty=[0xf, 0xf0, 0xf00, 0xf000, 0xf0000, 0xf00000, 0xf000000, 0xf0000001]),
        object_case('view_source_hit', state=1, owner='rendman', view=lane_hit(5)),
        object_case('view_source_no_overlap', state=1, flags=4, owner='rendman', view=VIEW_NONE),
        object_case('view_defaults_hit', state=1, owner='rendman', rendman='bare', view=None, defaults=lane_hit(7)),
        object_case('view_defaults_no_overlap', state=1, flags=4, owner='rendman', rendman='bare', view=None,
                    defaults=VIEW_NONE),
        object_case('view_rend_man_missing', state=0, owner='rendman', rendman=None, view=None, defaults=lane_hit(2),
                    start=1),
    ]
    return out


# ---- copy ------------------------------------------------------------------------------------

def copy_case(cid, fmt=0, width=8, height=8, depth=1, blocked=0, status=0, null=False):
    c = base('copy', cid)
    c['buffers'] = {'guard': {'size': 0x10, 'bytes': '5a' * 0x10}, 'level': {'size': 0x30}}
    c['memory'] = [{'addr': GUARD_GOT, 'pointer': 'guard'}]
    c['args'] = {'rdi': 0x7f0000100000, 'rsi': 0x7f0000200040, 'rdx': 0 if null else 'buf:level'}
    if null:
        return c
    c['memory'].append({'addr': 'buf:level', 'bytes': u32(5) + u32(fmt) + u32(width) + u32(height) + u32(depth) +
                        u32(0x11) * 6 + f'{blocked:02x}' + 'abcdef'})
    c['stubs'] = [{'address': COPY_BOX, 'argc': 6, 'ret': status, 'argmem': [{'arg': 3, 'size': 0x18}]}]
    return c


def copy_cases():
    out = [copy_case('null_level', null=True), copy_case('copy_fails', status=0x80d30001),
           copy_case('copy_fails_upper_half', status=0x123456789)]
    for fmt in (0, 1, 0x22, 0x23, 0x26, 0x29, 0x2a, 0xff, 0x100, 0x123, 0x80000029, 0xffffffff):
        out.append(copy_case(f'format_{fmt:x}', fmt=fmt, width=13, height=7, depth=3))
    for blocked in (1, 0x80):
        out.append(copy_case(f'blocked_flag_{blocked:x}', blocked=blocked, width=9, height=6))
    out.append(copy_case('blocked_flag_and_format', fmt=0x24, blocked=1, width=17, height=3))
    for w in (0, 1, 2, 3, 4, 5, 0xfffffffc, 0xfffffffd, 0xffffffff):
        out.append(copy_case(f'block_width_{w:x}', fmt=0x23, width=w, height=w ^ 1, depth=w))
        out.append(copy_case(f'plain_width_{w:x}', fmt=0x0a, width=w, height=5))
    out.append(copy_case('product_past_32_bits', width=0x10001, height=0x10003))
    out.append(copy_case('block_product_past_32_bits', fmt=0x25, width=0x40004, height=0x4000c))
    return out


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = {'list': list_cases, 'object': object_cases, 'copy': copy_cases}[sys.argv[2]]()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
