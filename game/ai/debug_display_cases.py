#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the AI group reset and debug display functions (game/ai/debug_display.cpp), for
tools/verify.py run.

usage: debug_display_cases.py OUT_DIR reset|end|flush|position

Objects are named buffers; the AI world's entity map is an MSVC tree (left +0x0, parent +0x8,
right +0x10, nil flag +0x19, key +0x20, value +0x28). Every original the functions call is a
stub scripted once per expected call (calls, arguments and the bytes they point at compared),
except the id list's push_back (0x02222210), which runs as is on the stubbed allocator. The
fatal error is a stub that returns. Singletons read again after it (the menu manager aside: its
null only reaches stubs) are not left missing: the second read is null too and both sides fault.

- reset: every menu state (0 to 5, 6, -1 and one with the top bit) with and without the show /
  hide requests, the menu manager missing, no parent item, an inline and a heap label; the blocks
  off (also a second argument with only high bits), none, a negative count, skip flags, a step
  that swaps the block array (read for every block), one that lowers or raises the count (read
  again after a step), and a skip flag that is the count's low byte (not read again after a skip).
- end: marks off, on through the global or the view, with and without the world marks (and
  their manager missing); the bounds off, not shown, the render state already set or differing
  field by field (dirty bits kept), slots 1 and -1; boxes inverted in one lane (x or w), flat,
  NaN lanes, NaN payloads in both corners, infinite and negative-zero boxes.
- flush: no object, the default allocator missing (created), an invalid heap (reported), no link
  owner, 0, 1, 3 and negative link counts, ids found, missing, with a null entity or a null
  object, allocations that lower or raise the link count or swap the display's object (the
  allocator object lies inside them), and the navigation manager missing.
- position: refresh (also with only high bits set), no object, every gate (flag, index, kind,
  point index below 0, at and past the count, counts that wrap, flag table empty, leave flags 1,
  4 and others only), the distance inside, at and past the threshold, NaN, infinite and
  overflowing; no model, points found near the object, an id of -1 or a negative index (world
  search), entities found or not, and the navigation manager missing.
"""
import json
import os
import struct
import sys

ADDRESSES = {'reset': '0x02224090', 'end': '0x0163bb50', 'flush': '0x01633ac0', 'position': '0x016334b0'}
FATAL = '0x024b55b0'
GUARD_GOT = '0x057e3be0'
AI_MANAGER, NVM_MANAGER = '0x059401a0', '0x05940208'
REND_MAN = '0x05940298'
# reset
MENU_MANAGER = '0x058b3570'
MENU_FIND, MENU_ADD, MENU_REMOVE = '0x013a2a00', '0x0139cd80', '0x013a33a0'
FORMAT, NAME_CONSTRUCT, NAME_DESTROY = '0x016e6cf0', '0x013aeaf0', '0x013aebc0'
BLOCK_STEP = '0x02224a60'
BLOCK = 0xb0
# end
ALWAYS_MARKS, WORLD_AI_MANAGER, WORLD_MARKS = '0x0593b090', '0x0593b0a0', '0x059454f0'
DRAW_MARKS, DRAW_WORLD_MARKS, DRAW_BOUNDS = '0x01637550', '0x01630da0', '0x0135dca0'
# flush
DEFAULT_ALLOCATOR, ALLOCATOR_CREATE = '0x05940670', '0x02fa2b10'
# position
MODEL_FIND, WORLD_FIND = '0x021c97d0', '0x021d2180'
# Fake game functions vtable slots name (stubbed at these addresses).
INFO_SLOT, ALLOCATE_SLOT, FREE_SLOT, BOUNDS_SLOT = '0x00500200', '0x00500210', '0x00500220', '0x00500230'
NAN_A, NAN_B, NAN_C, NAN_D = 0x7fc00011, 0x7fc00022, 0xffc00033, 0x7f800044
INF, NEG_INF, NEG_ZERO = 0x7f800000, 0xff800000, 0x80000000


def f32s(*v):
    return struct.pack(f'<{len(v)}f', *v).hex()


def u32s(*v):
    return b''.join(struct.pack('<I', x & 0xffffffff) for x in v).hex()


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


class Case:
    def __init__(self, variant, cid):
        self.variant, self.cid = variant, cid
        self.buffers = {}
        self.memory = [{'addr': GUARD_GOT, 'pointer': 'guard'}]
        self.stubs = []
        self.args = {}
        self.buf('guard', 0x10, '5a' * 8)

    def buf(self, name, size, fill=None):
        old = self.buffers.get(name)
        self.buffers[name] = {'size': max(size, old['size'] if old else 0)}
        if fill is not None:
            self.buffers[name]['bytes'] = fill
        elif old and 'bytes' in old:
            self.buffers[name]['bytes'] = old['bytes']
        return name

    def at(self, where, off):
        return where if where.startswith('0x') else f'buf:{where}+{off:#x}'

    def bytes(self, where, off, data):
        self.memory.append({'addr': self.at(where, off), 'bytes': data})

    def ptr(self, where, off, target):
        self.memory.append({'addr': self.at(where, off), 'pointer': target if '+' in target else target + '+0x0'})

    def guest(self, where, off, address):
        self.memory.append({'addr': self.at(where, off), 'guest': address})

    def stub(self, address, argc, **kw):
        self.stubs.append({'address': address, 'argc': argc, **kw})

    def fatal(self, argc=4):
        """A fatal error report (it returns, as in the shipped game)."""
        self.stub(FATAL, argc)

    def tree(self, name, entries):
        """A head and up to three nodes, sorted: (key, value buffer or None)."""
        head = self.buf(name, 0x30)
        self.bytes(head, 0x19, '01')
        nodes = []
        for i, (key, value) in enumerate(entries):
            n = self.buf(f'{name}_n{i}', 0x30)
            self.bytes(n, 0x20, u32s(key))
            if value:
                self.ptr(n, 0x28, value)
            nodes.append(n)
        if not nodes:
            for off in (0x0, 0x8, 0x10):
                self.ptr(head, off, head)
            return head
        if len(nodes) == 1:
            left, root, right = None, nodes[0], None
        elif len(nodes) == 2:
            left, root, right = nodes[0], nodes[1], None
        else:
            left, root, right = nodes
        self.ptr(head, 0x8, root)
        self.ptr(head, 0x0, left or root)
        self.ptr(head, 0x10, right or root)
        self.ptr(root, 0x8, head)
        self.ptr(root, 0x0, left or head)
        self.ptr(root, 0x10, right or head)
        for child in (left, right):
            if child:
                self.ptr(child, 0x0, head)
                self.ptr(child, 0x10, head)
                self.ptr(child, 0x8, root)
        return head

    def json(self):
        return {'schema': 1, 'address': ADDRESSES[self.variant], 'id': self.cid, 'returns': 'void',
                'args': self.args, 'buffers': self.buffers, 'memory': self.memory, 'imports': [],
                'stubs': self.stubs}


def world(c, entities, nvm=True):
    """The AI manager, its world and the entity map; entities: (id, 'object'|'null_object'|None)."""
    if nvm:
        c.ptr(NVM_MANAGER, None, c.buf('nvm', 0x10))
    else:
        c.bytes(NVM_MANAGER, None, q(0))
    m = c.buf('manager', 0x28)
    c.ptr(AI_MANAGER, None, m)
    c.ptr(m, 0x10, c.buf('world', 0x18))
    entries = []
    for key, kind in entities:
        if kind is None:
            entries.append((key, None))
            continue
        e = c.buf(f'entity{key:x}', 0xc0)
        if kind == 'object':
            o = c.buf(f'eobject{key:x}', 0x750, ('77' * 0x740) + '18')
            c.ptr(e, 0xb8, o)
        entries.append((key, e))
    c.ptr('world', 0x10, c.tree('ids', entries))


# ---------------------------------------------------------------- reset

def reset_case(cid, state=1, show=0, hide=0, blocks=(), step=1, manager='present', parent=True,
               label='inline', area_number=7, block_layout='after', step_writes=None, count=None):
    """blocks: skip flags per block (in the array the group points at). step_writes: per step
    call (in order), writes the step makes (list of (field, value)): 'blocks' (to 'alt'), 'count'.
    block_layout 'after': the group sits after the blocks in one buffer ('mem'); 'skip_count':
    the group lies so that block 1's skip flag is its count's low byte."""
    c = Case('reset', cid)
    n = len(blocks)
    count = n if count is None else count
    if block_layout == 'after':
        g = BLOCK * max(n, 1)
    else:
        g = BLOCK + 0x9c - 8
    size = g + 0x20 + BLOCK * 4   # room for blocks past the count (a raised count reads them)
    mem = c.buf('mem', size, 'c3' * size)
    for i in range(n, n + 4):
        if block_layout == 'after':
            c.bytes(mem, BLOCK * i + 0x9c, '00')
    for i, flag in enumerate(blocks):
        c.bytes(mem, BLOCK * i + 0x9c, f'{flag:02x}')
    area = c.buf('area', 0x10, '99' * 0x10)
    c.bytes(area, 0xb, f'{area_number:02x}')
    c.ptr(mem, g, area)
    c.bytes(mem, g + 8, u32s(count))
    c.ptr(mem, g + 0x10, mem)
    c.bytes(mem, g + 0x18, u32s(state) + f'{show:02x}{hide:02x}a5a5')
    if block_layout == 'skip_count':
        c.bytes(mem, BLOCK + 0x9c, f'{count & 0xff:02x}')
    alt = c.buf('alt', BLOCK * 4, '3c' * (BLOCK * 4))
    for i in range(4):
        c.bytes(alt, BLOCK * i + 0x9c, '01' if i == 2 else '00')
    c.args = {'rdi': f'buf:mem+{g:#x}', 'rsi': step, 'rdx': 0x5555}
    menu = c.buf('menu', 0x10)
    if state == 0 or state == 3:
        if manager == 'present':
            c.ptr(MENU_MANAGER, None, menu)
        else:
            c.bytes(MENU_MANAGER, None, q(0))
            c.fatal()
        name = {'arg': 0, 'bytes': q(0x1234567890)}
        if state == 0:
            c.stub(MENU_FIND, 2, ret='buf:parent' if parent else 0)
            c.buf('parent', 0x10)
            if parent:
                capacity = {'inline': 7, 'heap': 0x20, 'heap8': 8}[label]
                lab = {'arg': 0, 'offset': 0x18, 'bytes': q(2) + q(capacity)}
                writes = [lab, {'arg': 0, 'offset': 0x28, 'pointer': 'label_alloc+0x0'}]
                if label == 'inline':
                    writes.append({'arg': 0, 'offset': 0x8, 'bytes': '3100320000000000'})
                else:
                    c.buf('heap_label', 0x40, '41004200430000')
                    writes.append({'arg': 0, 'offset': 0x8, 'pointer': 'heap_label+0x0'})
                c.stub(FORMAT, 3, writes=writes)
                c.stub(NAME_CONSTRUCT, 2, writes=[name])
                c.stub(MENU_ADD, 4, argmem=[{'arg': 1, 'size': 8}, {'arg': 2, 'size': 8}, {'arg': 3, 'size': 16}])
                c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
                c.buf('label_alloc', 8)
                c.ptr('label_alloc', 0, c.buf('label_vt', 0x80))
                c.guest('label_vt', 0x70, FREE_SLOT)
                if label != 'inline':
                    c.stub(FREE_SLOT, 2)
        else:
            c.stub(NAME_CONSTRUCT, 2, writes=[name])
            c.stub(MENU_REMOVE, 4, argmem=[{'arg': 1, 'size': 16}])
            c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
    # The block steps, simulated as the original runs them.
    if step & 0xff:
        arr = 'mem'
        flags = {('mem', i): f for i, f in enumerate(blocks)}
        for i in range(4):
            flags[('alt', i)] = 1 if i == 2 else 0
        if block_layout == 'skip_count':
            flags[('mem', 1)] = count & 0xff
        in_memory = count   # the group's count as it is in memory
        loop_count = count  # the count the loop compares with
        writes = list(step_writes or [])
        i = 0
        while i < loop_count:
            if flags.get((arr, i), 0):
                flags[(arr, i)] = 0
                if block_layout == 'skip_count' and arr == 'mem' and i == 1:
                    in_memory &= ~0xff   # the flag is the count's low byte; the loop keeps its count
                i += 1
                continue
            w = writes.pop(0) if writes else []
            sw = []
            assert not w or arr == 'mem'
            for field, value in w:
                off = g - BLOCK * i + (0x10 if field == 'blocks' else 8)
                if field == 'blocks':
                    sw.append({'arg': 0, 'offset': off, 'pointer': 'alt+0x0'})
                else:
                    sw.append({'arg': 0, 'offset': off, 'bytes': u32s(value)})
                    in_memory = value
            c.stub(BLOCK_STEP, 1, **({'writes': sw} if sw else {}))
            if any(field == 'blocks' for field, _ in w):
                arr = 'alt'
            loop_count = in_memory   # read again after a step
            i += 1
    return c


def reset_cases():
    return [
        reset_case('state0_inline_label', state=0, blocks=(0, 0)),
        reset_case('state0_heap_label', state=0, label='heap', area_number=0xfe),
        reset_case('state0_heap_label_capacity_8', state=0, label='heap8', area_number=0x80),
        reset_case('state0_no_parent', state=0, parent=False),
        reset_case('state0_manager_missing', state=0, manager='missing', parent=False),
        reset_case('state0_requests_kept', state=0, show=1, hide=1),
        reset_case('state1', state=1, show=1, hide=1),
        reset_case('state2_no_request', state=2, show=1),
        reset_case('state2_hide', state=2, hide=1),
        reset_case('state2_hide_0x80', state=2, hide=0x80, show=0x40),
        reset_case('state3', state=3),
        reset_case('state3_manager_missing', state=3, manager='missing'),
        reset_case('state4', state=4, show=1, hide=1),
        reset_case('state5_no_request', state=5, hide=1),
        reset_case('state5_show', state=5, show=1),
        reset_case('state6', state=6, show=1, hide=1),
        reset_case('state_minus_one', state=-1, show=1, hide=1),
        reset_case('state_0x80000002', state=0x80000002, hide=1),
        reset_case('blocks_off', blocks=(0, 1, 0), step=0),
        reset_case('blocks_off_high_bits', blocks=(0, 1, 0), step=0x100),
        reset_case('blocks_low_byte_ff', blocks=(0, 1), step=0x1ff),
        reset_case('no_blocks', blocks=()),
        reset_case('negative_count', blocks=(0,), count=-1),
        reset_case('three_blocks', blocks=(0, 0, 0)),
        reset_case('skip_flags', blocks=(1, 0, 0x80, 0)),
        reset_case('all_skipped', blocks=(1, 2, 3)),
        reset_case('step_swaps_array', blocks=(0, 0, 0), step_writes=[[('blocks', None)]]),
        reset_case('step_lowers_count', blocks=(0, 0, 0, 0), step_writes=[[('count', 2)]]),
        reset_case('step_raises_count', blocks=(0, 0), step_writes=[[], [('count', 3)]]),
        reset_case('step_swaps_and_raises', blocks=(0, 0), step_writes=[[('blocks', None), ('count', 4)]]),
        reset_case('skip_clears_count', blocks=(0, 1, 0, 0), block_layout='skip_count', count=4),
    ]


# ---------------------------------------------------------------- end

def end_state(c, name, s18=3, s1c=0, v20=(0.8, 0.4, 0.1, 0.5), v30=(0.8, 0.4, 0.1, 0.5), dirty=0):
    b = bytearray(b'\x66' * 0x40)
    b[8] = dirty
    b[0x18:0x20] = struct.pack('<II', s18 & 0xffffffff, s1c & 0xffffffff)
    for off, v in ((0x20, v20), (0x30, v30)):
        b[off:off + 16] = struct.pack('<4I', *v) if isinstance(v[0], int) else struct.pack('<4f', *v)
    return c.buf(name, 0x40, b.hex())


def end_case(cid, always=0, draw=0, flag=0x5a, value=2.5, skip_world=0, world_manager=True,
             world_marks=0, bounds=0, shows=1, state=None, slot=0, box=None,
             box_raw=None):
    c = Case('end', cid)
    v = c.buf('view', 0x1d0, 'e1' * 0x1d0)
    c.args = {'rdi': 'buf:view'}
    c.bytes(ALWAYS_MARKS, None, f'{always:02x}')
    c.bytes(WORLD_MARKS, None, f'{world_marks:02x}')
    c.bytes(v, 0x1bc, f32s(value) if isinstance(value, float) else u32s(value))
    c.bytes(v, 0x1c9, f'{draw:02x}{flag:02x}{bounds:02x}{skip_world:02x}')
    subj = c.buf('subject', 0x290, '2d' * 0x290)
    c.ptr(v, 0x1b0, subj)
    c.bytes(subj, 0x288, f'{shows:02x}')
    c.ptr(subj, 0, c.buf('subject_vt', 0x80))
    c.guest('subject_vt', 0x60, BOUNDS_SLOT)
    if world_manager:
        c.ptr(WORLD_AI_MANAGER, None, c.buf('world_ai', 0x10))
    else:
        c.bytes(WORLD_AI_MANAGER, None, q(0))
    if always or draw:
        c.stub(DRAW_MARKS, 3, argmem=[{'arg': 1, 'size': 16}], argf32=[0])
        if not skip_world:
            if not world_manager:
                c.fatal()
            if world_marks:
                c.stub(DRAW_WORLD_MARKS, 1)
    # The marks draws are stubbed even when not expected, so that a call nobody expects shows.
    for address, argc in ((DRAW_MARKS, 3), (DRAW_WORLD_MARKS, 1)):
        if not any(s['address'] == address for s in c.stubs):
            c.stub(address, argc)
    if not (bounds and shows):
        return c
    rm = c.buf('rendman', 0x28)
    c.ptr(REND_MAN, None, rm)
    r = c.buf('renderer', 0x28)
    c.ptr(rm, 0x20, r)
    c.bytes(r, 0x20, u32s(slot))
    for i, name in ((-1, 'slot_m'), (0, 'slot0'), (1, 'slot1')):
        sl = c.buf(name, 0x48)
        c.ptr(r, 0x10 + 8 * i, sl)
        other = {'dirty': 0x11, 's18': 9, 'v20': (1.0, 1.0, 1.0, 1.0)}   # slots not drawn into differ
        st = end_state(c, f'state_{name}', **(state if i == slot and state else other))
        c.ptr(sl, 0x40, st)
    if box_raw:
        data = u32s(*box_raw[0]) + u32s(*box_raw[1])
    else:
        lo, hi = box or ((-1.0, -2.0, -3.0, 0.0), (3.0, 2.5, 5.0, 1.0))
        data = f32s(*lo) + f32s(*hi)
    c.stub(BOUNDS_SLOT, 2, writes=[{'arg': 0, 'bytes': data}], argi=[1])
    lo = struct.unpack('<4f', bytes.fromhex(data[:32]))
    hi = struct.unpack('<4f', bytes.fromhex(data[32:]))
    if not any(h < l for h, l in zip(hi, lo)):
        c.stub(DRAW_BOUNDS, 2, argmem=[{'arg': 1, 'size': 0x30}])
    return c


def end_cases():
    nan_box = ((NAN_A, 1, NAN_B, 0), (NAN_C, 2, NAN_D, 0x3f800000))
    return [
        end_case('nothing'),
        end_case('marks_by_global', always=1),
        end_case('marks_by_view', draw=1, flag=0, value=-0.0),
        end_case('marks_flag_ff', draw=1, flag=0xff, value=NAN_A),
        end_case('marks_global_0x80', always=0x80, draw=0x01),
        end_case('marks_world_marks', draw=1, world_marks=1),
        end_case('marks_world_marks_0x40', always=1, world_marks=0x40),
        end_case('marks_world_skipped', draw=1, world_marks=1, skip_world=1),
        end_case('marks_world_manager_missing', draw=1, world_manager=False),
        end_case('marks_world_manager_missing_marks', draw=1, world_manager=False, world_marks=1),
        end_case('bounds_not_shown', bounds=1, shows=0),
        end_case('bounds_shows_0x80', bounds=0x80, shows=0x80),
        end_case('bounds_state_set', bounds=1, state={'dirty': 0}),
        end_case('bounds_state_set_dirty_kept', bounds=1, state={'dirty': 0xc3}),
        end_case('bounds_state_0x18', bounds=1, state={'s18': 0, 'dirty': 0x41}),
        end_case('bounds_state_0x1c', bounds=1, state={'s1c': 1}),
        end_case('bounds_state_0x20', bounds=1, state={'v20': (0.8, 0.4, 0.1, 0.25)}),
        end_case('bounds_state_0x20_nan', bounds=1,
                 state={'v20': (0x3f4ccccd, 0x3ecccccd, 0x3dcccccd, NAN_A)}),
        end_case('bounds_state_0x30', bounds=1, state={'v30': (0.0, 0.4, 0.1, 0.5)}),
        end_case('bounds_state_all', bounds=1, state={'s18': 7, 's1c': 9, 'v20': (1, 2, 3, 4), 'v30': (5, 6, 7, 8),
                                                      'dirty': 0xff}),
        end_case('bounds_slot_1', bounds=1, slot=1, state={'s18': 1}),
        end_case('bounds_slot_minus_1', bounds=1, slot=-1, state={'s1c': 2}),
        end_case('bounds_with_marks', bounds=1, draw=1, world_marks=1),
        end_case('box_inverted_x', bounds=1, box=((1.0, 0.0, 0.0, 0.0), (0.5, 1.0, 1.0, 1.0))),
        end_case('box_inverted_w', bounds=1, box=((0.0, 0.0, 0.0, 2.0), (1.0, 1.0, 1.0, 1.0))),
        end_case('box_flat', bounds=1, box=((1.0, 2.0, 3.0, 4.0), (1.0, 2.0, 3.0, 4.0))),
        end_case('box_large', bounds=1, box=((-1e30, -3e38, 2.0, -1.0), (1e30, 3e38, 7.0, 0.0))),
        end_case('box_nan_payloads', bounds=1, box_raw=nan_box),
        end_case('box_nan_one_corner', bounds=1,
                 box_raw=((0xbf800000, 0, 0x40000000, 0), (NAN_A, NAN_B, NAN_C, NAN_D))),
        end_case('box_infinite', bounds=1, box_raw=((NEG_INF, 0, NEG_INF, 0), (INF, INF, 0x3f800000, INF))),
        end_case('box_infinite_centre', bounds=1, box_raw=((INF, 0, 0, 0), (INF, 0x3f800000, 0, 0))),
        end_case('box_negative_zero', bounds=1,
                 box_raw=((NEG_ZERO, NEG_ZERO, NEG_ZERO, NEG_ZERO), (0, NEG_ZERO, 0, NEG_ZERO))),
    ]


# ---------------------------------------------------------------- flush

def flush_case(cid, object_=True, allocator='present', valid=True, owner=True, links=(), count=None,
               entities=None, nvm=True, alloc_writes=None):
    """links: entity ids at +0x150 (0x60 apart). alloc_writes: per allocation (head first), writes
    the allocator makes through its object: ('count', n) when the allocator object lies in the
    display's object, ('object', buffer) when it lies in the display."""
    c = Case('flush', cid)
    d = c.buf('display', 0x28, '1e' * 0x28)
    c.args = {'rdi': 'buf:display'}
    if not object_:
        c.bytes(d, 8, q(0))
        return c
    o = c.buf('object', 0x750, '0f' * 0x750)
    c.ptr(d, 8, o)
    count = len(links) if count is None else count
    c.bytes(o, 0x138, u32s(count))
    c.bytes(o, 0x68, q(0x4000 if owner else 0))
    for i, key in enumerate(links):
        c.bytes(o, 0x150 + 0x60 * i, u32s(key))
    other = c.buf('object2', 0x750, '0f' * 0x750)
    c.bytes(other, 0x68, q(1))
    c.bytes(other, 0x138, u32s(1))
    c.bytes(other, 0x150, u32s(0x30))
    # Where the allocator object lies: its own buffer, in the object (to change the link count)
    # or at the display's start (to change the display's object).
    kinds = {w[0] for ws in (alloc_writes or []) for w in ws}
    if 'count' in kinds:
        heap, heap_base = f'{o}+0x10', (o, 0x10)
    elif 'object' in kinds:
        heap, heap_base = f'{d}+0x0', (d, 0)
    else:
        heap, heap_base = c.buf('heap', 0x10) + '+0x0', ('heap', 0)
    c.ptr(heap_base[0], heap_base[1], c.buf('heap_vt', 0x80))
    c.guest('heap_vt', 0x20, INFO_SLOT)
    c.guest('heap_vt', 0x58, ALLOCATE_SLOT)
    c.guest('heap_vt', 0x70, FREE_SLOT)
    if allocator == 'present':
        c.ptr(DEFAULT_ALLOCATOR, None, heap)
    else:
        c.bytes(DEFAULT_ALLOCATOR, None, q(0))
        c.stub(ALLOCATOR_CREATE, 0, ret='buf:' + heap)
    c.stub(INFO_SLOT, 3, writes=[{'arg': 0, 'bytes': '20' if valid else 'df'}], argi=[1, 2])
    if not valid:
        c.fatal(argc=3)
    writes = list(alloc_writes or [])

    def allocate(node):
        c.buf(node, 0x18, 'ee' * 0x18)
        sw = []
        for kind, value in (writes.pop(0) if writes else []):
            if kind == 'count':
                sw.append({'arg': 0, 'offset': 0x138 - 0x10, 'bytes': u32s(value)})
                nonlocal_state['count'] = value
            else:
                sw.append({'arg': 0, 'offset': 8, 'pointer': value + '+0x0'})
                nonlocal_state['object'] = value
        c.stub(ALLOCATE_SLOT, 3, ret='buf:' + node, **({'writes': sw} if sw else {}))

    nonlocal_state = {'count': count, 'object': o}
    allocate('node_head')
    obj = nonlocal_state['object']
    if obj == 'object2':
        links, nonlocal_state['count'] = (0x30,), 1
    count = nonlocal_state['count']   # the object and its count are read after the head's allocation
    nodes = []
    if owner or obj == 'object2':
        ids = []
        for i in range(count):
            ids.append(links[i] if i < nonlocal_state['count'] else 0xffffffff)
            name = f'node{i}'
            allocate(name)
            nodes.append(name)
        world(c, entities or [], nvm=nvm)
        for _ in ids:
            if not nvm:
                c.fatal()
    for name in nodes + ['node_head']:
        c.stub(FREE_SLOT, 2)
    return c


def flush_cases():
    ents = [(0x10, 'object'), (0x20, 'null_object'), (0x30, 'object')]
    return [
        flush_case('no_object', object_=False),
        flush_case('no_owner', owner=False, links=(0x10,)),
        flush_case('allocator_created', allocator='missing', links=(0x10,), entities=ents),
        flush_case('heap_invalid', valid=False, links=(0x10,), entities=ents),
        flush_case('no_links', entities=ents),
        flush_case('negative_count', count=-2, entities=ents),
        flush_case('one_link', links=(0x10,), entities=ents),
        flush_case('three_links', links=(0x30, 0x20, 0x10), entities=ents),
        flush_case('missing_ids', links=(0x15, 0x40, 0x5), entities=ents),
        flush_case('null_entity', links=(0x20, 0x10), entities=[(0x10, 'object'), (0x20, None)]),
        flush_case('id_minus_one', links=(0xffffffff, 0x30), entities=[(0x30, 'object'), (0xffffffff, 'object')]),
        flush_case('empty_world', links=(0x10, 0x30), entities=[]),
        flush_case('count_lowered', links=(0x10, 0x30, 0x20), entities=ents, alloc_writes=[[], [('count', 1)]]),
        flush_case('count_raised_before', links=(0x10, 0x30, 0x20, 0x10), count=2, entities=ents,
                   alloc_writes=[[('count', 4)]]),
        flush_case('count_raised', links=(0x10, 0x30, 0x20, 0x10), count=2, entities=ents,
                   alloc_writes=[[], [('count', 4)]]),
        flush_case('object_swapped', links=(0x10,), entities=ents, alloc_writes=[[('object', 'object2')]]),
        flush_case('nvm_missing', links=(0x10, 0x30), entities=ents, nvm=False),
    ]


# ---------------------------------------------------------------- position

def position_case(cid, refresh=0, object_=True, gated=True, index134=5, kind=1, point=(0x10, 2),
                  previous=(0x77, 9), counts=(3, 2), flag_table=(0, 0, 0, 0, 0), position=(1.0, 2.0, 3.0, 1.0),
                  reference=None, model=True, near=None, far=(0x30, 4), entities=None, nvm=True):
    """near / far: the points the model's and the world's searches find (None: not searched)."""
    c = Case('position', cid)
    d = c.buf('display', 0x28, '1e' * 8)
    c.bytes(d, 0x10, q(0xdeadbeef))
    c.bytes(d, 0x18, u32s(*point) + u32s(*previous))
    c.buf('position', 0x10, u32s(*position) if isinstance(position[0], int) else f32s(*position))
    if reference is None:
        c.args = {'rdi': 'buf:display', 'rsi': 'buf:position', 'rdx': 'buf:position', 'rcx': refresh}
    else:
        c.buf('reference', 0x10, u32s(*reference) if isinstance(reference[0], int) else f32s(*reference))
        c.args = {'rdi': 'buf:display', 'rsi': 'buf:position', 'rdx': 'buf:reference', 'rcx': refresh}
    world(c, entities if entities is not None else [(0x10, 'object'), (0x20, 'null_object'), (0x30, 'object')],
          nvm=nvm)
    if not object_:
        c.bytes(d, 8, q(0))
    else:
        o = c.buf('object', 0x750, '0f' * 0x740 + ('11' if gated else '10'))
        c.ptr(d, 8, o)
        c.bytes(o, 0x30, u32s(kind))
        c.bytes(o, 0x134, u32s(index134))
        if model:
            m = c.buf('model', 0x30)
            c.ptr(o, 0x50, m)
            p = c.buf('points', 0x150, '88' * 0x150)
            c.ptr(m, 0x28, p)
            c.bytes(p, 0x18, u32s(counts[0]))
            c.bytes(p, 0x118, u32s(counts[1]))
            c.bytes(p, 0x148, u32s(len(flag_table)))
            # an empty table's flags are set, as they must not be read
            c.buf('flags', 0x10, ''.join(f'{f:02x}' for f in flag_table) or '05' * 0x10)
            c.ptr(p, 0x140, 'flags')
        else:
            c.bytes(o, 0x50, q(0))
    if near is None and far is None:
        return c
    if object_ and model and near is not None:
        c.stub(MODEL_FIND, 4, writes=[{'arg': 0, 'bytes': u32s(*near)}], argi=[1, 2, 3], argf32=[0],
               argmem=[{'arg': 2, 'size': 16}])
    found = near if (object_ and model and near is not None) else (0xffffffff, -1)
    if not object_ or found[0] == 0xffffffff or found[1] < 0:
        if not nvm:
            c.fatal()
        c.stub(WORLD_FIND, 3, writes=[{'arg': 0, 'bytes': u32s(*far)}], argi=[1, 2], argf32=[0],
               argmem=[{'arg': 2, 'size': 16}])
    if not nvm:
        c.fatal()
    return c


def position_cases():
    moved = (1.0, 2.0, 3.0, 1.0)
    near = (1.0, 2.00005, 3.0, 9.0)
    return [
        position_case('refresh', refresh=1, near=(0x10, 1)),
        position_case('refresh_0xff', refresh=0xff, near=(0x30, 0)),
        position_case('refresh_high_bits', refresh=0x100, reference=near, near=None, far=None),
        position_case('no_object', object_=False),
        position_case('no_object_refresh', object_=False, refresh=1, far=(0x10, 0)),
        position_case('not_gated', gated=False, reference=near, near=(0x10, 3)),
        position_case('index134_minus_one', index134=-1, reference=near, near=(0x10, 3)),
        position_case('index134_zero', index134=0, reference=near, near=None, far=None),
        position_case('index134_minus_two', index134=-2, reference=near, near=None, far=None),
        position_case('kind_2', kind=2, reference=near, near=(0x10, 3)),
        position_case('kind_0', kind=0, reference=near, near=(0x10, 3)),
        position_case('point_index_negative', point=(0x10, -1), reference=near, near=(0x10, 3)),
        position_case('point_index_at_count', point=(0x10, 5), reference=near, near=(0x10, 3)),
        position_case('point_index_past_count', point=(0x10, 6), reference=near, near=(0x10, 3)),
        position_case('point_index_last', point=(0x10, 4), reference=near, near=None, far=None),
        position_case('counts_wrap', point=(0x10, 1), counts=(0xffffffff, 3), reference=near, near=None, far=None),
        position_case('counts_negative', point=(0x10, 1), counts=(0xfffffff0, 3), reference=near, near=(0x10, 0)),
        position_case('flag_table_empty', flag_table=(), reference=near, near=None, far=None),
        position_case('flag_1', flag_table=(0, 0, 1, 0, 0), reference=near, near=(0x30, 1)),
        position_case('flag_4', flag_table=(0, 0, 4, 0, 0), reference=near, near=(0x30, 1)),
        position_case('flag_2_only', flag_table=(0, 0, 0xfa, 0, 0), reference=near, near=None, far=None),
        position_case('flag_on_other_point', flag_table=(1, 5, 0, 4, 1), reference=near, near=None, far=None),
        position_case('same_position', near=None, far=None),
        position_case('moved', reference=(1.0, 2.0, 3.0 + 1.0, 1.0), near=(0x10, 0)),
        position_case('moved_y', reference=(1.0, 2.0 - 1.0, 3.0, 1.0), near=(0x10, 0)),
        position_case('moved_x', reference=(1.5, 2.0, 3.0, 1.0), near=(0x10, 0)),
        position_case('moved_w_only', reference=(1.0, 2.0, 3.0, 500.0), near=None, far=None),
        position_case('at_threshold', reference=(1.0 + 1e-4, 2.0, 3.0, 1.0), position=(1.0, 2.0, 3.0, 1.0),
                      near=(0x10, 0)),
        position_case('below_threshold', reference=(0.0, 0.0, 0.0, 0.0), position=(9e-5, 0.0, 0.0, 0.0),
                      near=None, far=None),
        position_case('above_threshold', reference=(0.0, 0.0, 0.0, 0.0), position=(0.0, 0.0, 1.1e-4, 0.0),
                      near=(0x10, 0)),
        position_case('distance_nan', reference=(0, 0, 0, 0), position=(NAN_A, 0, 0, 0), near=None, far=None),
        position_case('distance_infinite', reference=(0, 0, 0, 0), position=(INF, 0, 0, 0), near=(0x10, 0)),
        position_case('distance_overflow', reference=(-3e38, 0.0, 0.0, 0.0), position=(3e38, 0.0, 0.0, 0.0),
                      near=(0x10, 0)),
        position_case('no_model', refresh=1, model=False),
        position_case('near_null_object', refresh=1, near=(0x20, 0)),
        position_case('near_missing_id', refresh=1, near=(0x15, 0)),
        position_case('near_no_id', refresh=1, near=(0xffffffff, 3), far=(0x10, 1)),
        position_case('near_negative_index', refresh=1, near=(0x30, -5), far=(0x30, 1)),
        position_case('near_index_0x80000000', refresh=1, near=(0x30, -0x80000000), far=(0x99, 1)),
        position_case('far_missing', refresh=1, near=(0xffffffff, -1), far=(0x40, 0)),
        position_case('far_no_point', refresh=1, near=(0xffffffff, -1), far=(0xffffffff, -1)),
        position_case('far_null_entity', refresh=1, near=(0xffffffff, -1), far=(0x20, 0),
                      entities=[(0x10, 'object'), (0x20, None)]),
        position_case('far_empty_world', object_=False, far=(0x10, 0), entities=[]),
        position_case('nvm_missing_near', refresh=1, near=(0x10, 0), nvm=False),
        position_case('nvm_missing_far', object_=False, far=(0x30, 0), nvm=False),
    ]


CASES = {'reset': reset_cases, 'end': end_cases, 'flush': flush_cases, 'position': position_cases}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in CASES:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    cases = [c.json() for c in CASES[sys.argv[2]]()]
    for c in cases:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
