#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for ai_area_block_step_02224a60 (game/ai/area_block_step.cpp), for tools/verify.py run.

usage: area_block_step_cases.py OUT_DIR

Exercises every outer state (0..5, 6, -1), every state-1 substate (0..8), unload substates (0..3),
missing singletons, resource loading/release, Havok array clearing and capacity doubling, link
connection transforms (with distinct NaN payloads and -0.0f), object pool ring-buffer wrapping and
exhaustion, batch sizing (loading vs normal), bounding-box min/max merges (with NaNs), debug menu
registration and failure cleanup, and thread temporary heap initialization/checks.
"""
import json
import os
import struct
import sys

ADDRESS = '0x02224a60'
GUARD_GOT = '0x057e3be0'
FATAL = '0x024b55b0'

SPRJ_FILE = '0x0593b118'
MENU_MANAGER = '0x058b3570'
NVM_MANAGER = '0x05940208'
AI_MANAGER = '0x059401a0'
AI_UNKNOWN_FLAG = '0x059401a8'
LOADING_HELPER = '0x0593e8b8'
BLOCK_ALLOCATOR = '0x05940408'
WORLD_AREA_MANAGER = '0x0593b148'
REBUILD_LINKS_FLAG = '0x0598e184'
SHAPE_HEAP = '0x058018b0'
THREAD_HEAP_OFFSET = '0x057e4568'
THREAD_HEAP_SIZE_OFFSET = '0x057e4600'
QUIT_QUERY = '0x05aa54f8'
DEFAULT_ALLOCATOR = '0x05940670'

HK_ARRAY_RESERVE = '0x00819040'
MENU_ITEM_CONSTRUCT = '0x0139b5d0'
MENU_BIND_PROPERTY = '0x0139b740'
MENU_ADD = '0x0139cd80'
MENU_FIND = '0x013a29e0'
MENU_REMOVE = '0x013a33a0'
NAME_CONSTRUCT = '0x013aeaf0'
NAME_DESTROY = '0x013aebc0'
LOAD_PARTS = '0x018251f0'
LOAD_NVM = '0x01825de0'
FORMAT_PARTS_PATH = '0x01ec8a00'
FORMAT_NVM_PATH = '0x01ec8be0'
CONNECT_PARTS = '0x021d9890'
OBJECT_UNLOAD = '0x0221ef30'
OBJECT_SETUP = '0x0221f080'
OBJECT_FINALIZE_LINKS = '0x022200e0'
OBJECT_ATTACH_LINK = '0x022204a0'
OBJECT_PREPARE_LINKS = '0x02220880'
OBJECT_ADD_MENU = '0x022216e0'
FIND_LINK_OBJECT = '0x02227980'
REMOVE_OBJECT_MENUS = '0x02227a70'
HEAP_OWNING = '0x0247b720'
THREAD_HEAP_CREATE = '0x0247d250'
WIDE_FORMAT = '0x02b62d00'

# Fake vtable slot target addresses (stubbed per case when called).
SHAPE_FREE_SLOT = '0x00500300'
SPRJ_RELEASE_SLOT = '0x00500310'
BLOCK_ALLOC_SLOT = '0x00500320'
BLOCK_FREE_SLOT = '0x00500330'
MENU_ALLOC_SLOT = '0x00500340'
MENU_FREE_SLOT = '0x00500350'
BANK_ADD_ITEM_SLOT = '0x00500360'
NODE_DESTROY_SLOT = '0x00500370'
NODE_FREE_SLOT = '0x00500380'
HEAP_INFO_SLOT = '0x00500390'
HEAP_FREE_SLOT = '0x005003a0'

NAN_A, NAN_B, NAN_C, NAN_D = 0x7fc00011, 0x7fc00022, 0xffc00033, 0x7f800044
NEG_ZERO = 0x80000000
BLOCK_SIZE = 0xb0
OBJECT_SIZE = 0x750
POOL_SLOTS = 1024
TLS_THREAD_OFF = 0x100
TLS_HEAP_SLOT = -0x20
TLS_SIZE_SLOT = -0x18


def u32s(*v):
    return b''.join(struct.pack('<I', x & 0xffffffff) for x in v).hex()


def u64s(*v):
    return b''.join(struct.pack('<Q', x & 0xffffffffffffffff) for x in v).hex()


def f32s(*v):
    return struct.pack(f'<{len(v)}f', *v).hex()


def u16_bytes(text):
    return (text + '\0').encode('utf-16-le').hex()


class Case:
    def __init__(self, cid):
        self.cid = cid
        self.buffers = {}
        self.memory = [{'addr': GUARD_GOT, 'pointer': 'guard'}]
        self.stubs = []
        self.args = {'rdi': 'buf:block+0x0'}
        self.gs = None
        self.buf('guard', 0x10, '5a' * 8)
        self.buf('block', BLOCK_SIZE, '00' * BLOCK_SIZE)

    def buf(self, name, size, fill=None):
        old = self.buffers.get(name)
        sz = max(size, old['size'] if old else 0)
        entry = {'size': sz}
        if fill is not None:
            entry['bytes'] = fill
        elif old and 'bytes' in old:
            entry['bytes'] = old['bytes']
        self.buffers[name] = entry
        return name

    def at(self, where, off):
        if where is None or where.startswith('0x'):
            return where
        return f'buf:{where}+{off:#x}'

    def bytes(self, where, off, data):
        self.memory.append({'addr': self.at(where, off), 'bytes': data})

    def ptr(self, where, off, target):
        t = target if '+' in target else f'{target}+0x0'
        self.memory.append({'addr': self.at(where, off), 'pointer': t})

    def guest(self, where, off, address):
        self.memory.append({'addr': self.at(where, off), 'guest': address})

    def stub(self, address, argc, **kw):
        self.stubs.append({'address': address, 'argc': argc, **kw})

    def fatal(self, argc=4):
        self.stub(FATAL, argc)

    def json(self):
        out = {
            'schema': 1,
            'address': ADDRESS,
            'id': self.cid,
            'returns': 'void',
            'args': self.args,
            'buffers': self.buffers,
            'memory': self.memory,
            'imports': [],
            'stubs': self.stubs,
        }
        if self.gs:
            out['gs'] = self.gs
        return out


def set_block_fields(c, state, substate=0, unload_substate=0, map_id=0x1c010203, block_index=0,
                     wait_frames=0, object_count=0, step_index=0, reload_req=0, unload_req=0):
    c.bytes('block', 0x30, u32s(map_id, block_index))
    c.bytes('block', 0x58, u32s(object_count, step_index))
    c.bytes('block', 0x90, u32s(state, substate, unload_substate))
    c.bytes('block', 0x9d, f'{reload_req:02x}{unload_req:02x}00')
    c.bytes('block', 0xa0, u32s(wait_frames))


def setup_area(c, area_state=2):
    c.buf('area', 0x20)
    c.bytes('area', 0x18, u32s(area_state))
    c.ptr('block', 0x38, 'area')


def setup_res(c, name, status=4):
    c.buf(name, 0xa0)
    c.bytes(name, 0x78, f'{status:02x}')
    return name


def setup_shape_heap(c):
    c.buf('shape_vt', 0x30)
    c.guest('shape_vt', 0x28, SHAPE_FREE_SLOT)
    c.ptr(SHAPE_HEAP, 0, 'shape_vt')


def setup_nvm_manager(c, active_slots=(), cursor=0, blocks=None):
    pool_bytes = POOL_SLOTS * OBJECT_SIZE
    c.buf('pool', pool_bytes)
    for idx in active_slots:
        off = idx * OBJECT_SIZE
        c.bytes('pool', off + 0x30, u32s(1))
        c.bytes('pool', off + 0x134, u32s(0))
        c.bytes('pool', off + 0x740, '01')
    mgr = c.buf('nvm_mgr', 0x2c10)
    c.ptr(mgr, 0x2be8, 'pool')
    c.bytes(mgr, 0x2c08, u32s(cursor))
    if blocks is not None:
        c.buf('mgr_blocks', max(1, len(blocks)) * BLOCK_SIZE)
        for i, b_state in enumerate(blocks):
            c.bytes('mgr_blocks', i * BLOCK_SIZE + 0x90, u32s(b_state))
        c.bytes(mgr, 0x2bd8, u32s(len(blocks)))
        c.ptr(mgr, 0x2be0, 'mgr_blocks')
    c.ptr(NVM_MANAGER, None, mgr)
    return mgr


def make_object(c, name, active=True, kind=1, idx=5, flags=1, model_id=None, bmin=None, bmax=None):
    c.buf(name, OBJECT_SIZE)
    if active is False:
        kind, idx, flags = 0, -1, 0
    c.bytes(name, 0x30, u32s(kind))
    c.bytes(name, 0x134, u32s(idx))
    c.bytes(name, 0x740, f'{flags:02x}')
    if model_id is not None or bmin is not None or bmax is not None:
        m = c.buf(f'{name}_model', 0xa0)
        if bmin is not None:
            c.bytes(m, 0x70, bmin)
        if bmax is not None:
            c.bytes(m, 0x80, bmax)
        if model_id is not None:
            c.bytes(m, 0x98, u32s(model_id))
        c.ptr(name, 0x48, m)
    return name


def build_cases():
    cases = []

    # ---- Outer switch invalid states ----
    for st in (6, 0xffffffff):
        c = Case(f'outer_invalid_{st:x}')
        set_block_fields(c, state=st)
        cases.append(c)

    # ---- State 0: wait frames & load request ----
    for waited in (0, 0xfd, 0xfe):
        c = Case(f'state0_wait_{waited:x}')
        set_block_fields(c, state=0, wait_frames=waited)
        setup_area(c, area_state=1)
        cases.append(c)

    for path_mode in ('inline', 'heap'):
        c = Case(f'state0_load_{path_mode}')
        set_block_fields(c, state=0, substate=5, wait_frames=7, map_id=0x1d020304)
        setup_area(c, area_state=2)
        c.buf('sprj_file', 0x10)
        c.buf('parts_res', 0xa0)
        c.buf('nvm_res', 0xa0)
        c.ptr(SPRJ_FILE, None, 'sprj_file')

        def make_path_stub(fn_addr, buf_name, text):
            if path_mode == 'inline':
                c.stub(fn_addr, 2, writes=[
                    {'arg': 0, 'offset': 0x8, 'bytes': u16_bytes(text)},
                    {'arg': 0, 'offset': 0x18, 'bytes': u64s(len(text), 7)},
                ])
            else:
                c.buf(buf_name, 0x40, u16_bytes(text))
                c.buf(f'{buf_name}_alloc', 0x10)
                c.buf(f'{buf_name}_vt', 0x80)
                c.ptr(f'{buf_name}_alloc', 0, f'{buf_name}_vt')
                c.guest(f'{buf_name}_vt', 0x70, BLOCK_FREE_SLOT)
                c.stub(fn_addr, 2, writes=[
                    {'arg': 0, 'offset': 0x8, 'pointer': f'{buf_name}+0x0'},
                    {'arg': 0, 'offset': 0x18, 'bytes': u64s(len(text), 0x10)},
                    {'arg': 0, 'offset': 0x28, 'pointer': f'{buf_name}_alloc+0x0'},
                ])

        make_path_stub(FORMAT_PARTS_PATH, 'p_heap', 'p.bnd')
        c.stub(LOAD_PARTS, 3, ret='buf:parts_res')
        if path_mode == 'heap':
            c.stub(BLOCK_FREE_SLOT, 2)
        make_path_stub(FORMAT_NVM_PATH, 'n_heap', 'n.nvm')
        c.stub(LOAD_NVM, 3, ret='buf:nvm_res')
        if path_mode == 'heap':
            c.stub(BLOCK_FREE_SLOT, 2)
        cases.append(c)

    # ---- State 1 gates ----
    c = Case('state1_unload_requested')
    set_block_fields(c, state=1, substate=2, unload_req=1)
    cases.append(c)

    for tag, parts_st, nvm_st in (('null_parts', None, 4), ('parts_pending', 3, 4),
                                  ('null_nvm', 4, None), ('nvm_pending', 4, 2)):
        c = Case(f'state1_not_ready_{tag}')
        set_block_fields(c, state=1, substate=0)
        if parts_st is not None:
            c.ptr('block', 0x48, setup_res(c, 'parts_res', parts_st))
        if nvm_st is not None:
            c.ptr('block', 0x40, setup_res(c, 'nvm_res', nvm_st))
        cases.append(c)

    for sub in (7, 8, 0xffffffff):
        c = Case(f'state1_sub_{sub:x}')
        set_block_fields(c, state=1, substate=sub)
        c.ptr('block', 0x48, setup_res(c, 'parts_res', 4))
        c.ptr('block', 0x40, setup_res(c, 'nvm_res', 4))
        cases.append(c)

    # ---- State 1, Substate 0: clear & rebuild (rebuild_links_on_step == 0) ----
    c = Case('sub0_clear_and_missing_singletons')
    set_block_fields(c, state=1, substate=0, map_id=0x1d000001)
    setup_shape_heap(c)
    c.bytes(NVM_MANAGER, None, u64s(0))
    c.bytes(AI_MANAGER, None, u64s(0))
    c.bytes(AI_UNKNOWN_FLAG, None, u32s(0x12345678))
    c.bytes(REBUILD_LINKS_FLAG, None, '00')
    c.fatal()
    c.fatal()
    c.fatal()
    parts_res = setup_res(c, 'parts_res', 4)
    nvm_res = setup_res(c, 'nvm_res', 4)
    c.ptr('block', 0x48, parts_res)
    c.ptr('block', 0x40, nvm_res)
    # Two sections: section 0 is null, section 1 has graph_a (2 nodes: owned+unowned) and graph_b (1 node).
    c.buf('secs', 0x10)
    sec1 = c.buf('sec1', 0xd0)
    c.bytes(sec1, 0xc0, u32s(42))
    c.ptr('secs', 8, sec1)
    c.bytes(nvm_res, 0x88, u32s(2))
    c.ptr(nvm_res, 0x90, 'secs')
    ga_ptr = c.buf('ga_ptr', 8)
    gb_ptr = c.buf('gb_ptr', 8)
    c.ptr(sec1, 0x88, ga_ptr)
    c.ptr(sec1, 0x98, gb_ptr)
    ga = c.buf('ga', 0x50)
    gb = c.buf('gb', 0x80)
    c.ptr(ga_ptr, 0, ga)
    c.ptr(gb_ptr, 0, gb)
    c.buf('ga_nodes', 0x38 * 2)
    c.buf('ga_n0_a', 0x20)
    c.buf('ga_n0_c', 0x10)
    c.ptr('ga_nodes', 0x08, 'ga_n0_a')
    c.bytes('ga_nodes', 0x10, u32s(1, 2))
    c.bytes('ga_nodes', 0x24, u32s(0x80000001))
    c.ptr('ga_nodes', 0x28, 'ga_n0_c')
    c.bytes('ga_nodes', 0x30, u32s(1, 1))
    # node 1 has unowned array_a, owned array_b
    c.buf('ga_n1_b', 0x10)
    c.bytes('ga_nodes', 0x38 + 0x14, u32s(0x80000000))
    c.ptr('ga_nodes', 0x38 + 0x18, 'ga_n1_b')
    c.bytes('ga_nodes', 0x38 + 0x20, u32s(1, 1))
    c.bytes('ga_nodes', 0x38 + 0x34, u32s(0x80000000))
    c.ptr(ga, 0x40, 'ga_nodes')
    c.bytes(ga, 0x48, u32s(2, 4))
    c.buf('gb_nodes', 0x38)
    c.buf('gb_n0_b', 0x10)
    c.bytes('gb_nodes', 0x14, u32s(0x80000000))
    c.ptr('gb_nodes', 0x18, 'gb_n0_b')
    c.bytes('gb_nodes', 0x20, u32s(1, 1))
    c.bytes('gb_nodes', 0x34, u32s(0x80000000))
    c.ptr(gb, 0x68, 'gb_nodes')
    c.bytes(gb, 0x70, u32s(1, 2))
    # Reverse free order: node 1 array_b (0x10), node 0 array_c (0x8), node 0 array_a (0x20), then gb node 0 array_b (0x10)
    c.stub(SHAPE_FREE_SLOT, 3)
    c.stub(SHAPE_FREE_SLOT, 3)
    c.stub(SHAPE_FREE_SLOT, 3)
    c.stub(SHAPE_FREE_SLOT, 3)
    cases.append(c)

    # Substate 0: rebuild links from parts_container (matching + non-matching + empty counts + capacity doubling)
    c = Case('sub0_rebuild_links')
    set_block_fields(c, state=1, substate=0)
    setup_shape_heap(c)
    c.ptr(NVM_MANAGER, None, c.buf('nvm_mgr', 8))
    c.ptr(AI_MANAGER, None, c.buf('ai_mgr', 8))
    c.bytes(REBUILD_LINKS_FLAG, None, '00')
    parts_res = setup_res(c, 'parts_res', 4)
    nvm_res = setup_res(c, 'nvm_res', 4)
    c.ptr('block', 0x48, parts_res)
    c.ptr('block', 0x40, nvm_res)
    parts = c.buf('parts', 0xe0)
    c.ptr(parts_res, 0x88, parts)
    c.buf('entries', 0x90 * 2)
    c.bytes('entries', 0x30, u32s(100, 7))
    c.bytes('entries', 0x90 + 0x30, u32s(200, 999))
    c.ptr(parts, 0x78, 'entries')
    c.bytes(parts, 0xc0, u32s(2))
    c.buf('links_a', 0x20 * 3)
    # link 0: part 100 -> sec 7, data_a_count=2, data_b_count=2
    c.bytes('links_a', 0x00, u32s(100, 500, 2, 2))
    c.bytes('links_a', 0x10, u64s(1, 0))
    # link 1: missing part 999
    c.bytes('links_a', 0x20, u32s(999, 501, 1, 1))
    # link 2: part 200 -> missing section 999
    c.bytes('links_a', 0x40, u32s(200, 502, 0, 0))
    c.ptr(parts, 0x98, 'links_a')
    c.buf('data_a', 0x10 * 3, f32s(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12))
    c.ptr(parts, 0xa0, 'data_a')
    c.buf('data_b', 0x08 * 2, u32s(11, 22, 33, 44))
    c.ptr(parts, 0xa8, 'data_b')
    c.bytes(parts, 0xd0, u32s(3, 3, 2, 0))

    sec = c.buf('sec', 0xd0)
    c.bytes(sec, 0xc0, u32s(7))
    c.buf('secs', 8)
    c.ptr('secs', 0, sec)
    c.bytes(nvm_res, 0x88, u32s(1))
    c.ptr(nvm_res, 0x90, 'secs')
    ga_p = c.buf('ga_p', 8)
    gb_p = c.buf('gb_p', 8)
    c.ptr(sec, 0x88, ga_p)
    c.ptr(sec, 0x98, gb_p)
    ga = c.buf('ga', 0x50)
    gb = c.buf('gb', 0x80)
    c.ptr(ga_p, 0, ga)
    c.ptr(gb_p, 0, gb)
    c.bytes(ga, 0x48, u32s(0, 0x80000000))
    c.bytes(gb, 0x70, u32s(0, 0x80000000))
    c.buf('ga_nodes_buf', 0x38 * 2)
    c.buf('ga_a_buf', 0x10 * 2)
    c.buf('gb_nodes_buf', 0x38 * 2)
    c.buf('gb_b_buf', 0x10 * 2)
    c.stub(HK_ARRAY_RESERVE, 4, writes=[
        {'arg': 1, 'offset': 0x0, 'pointer': 'ga_nodes_buf+0x0'},
        {'arg': 1, 'offset': 0xc, 'bytes': u32s(2)},
    ])
    c.stub(HK_ARRAY_RESERVE, 4, writes=[
        {'arg': 1, 'offset': 0x0, 'pointer': 'ga_a_buf+0x0'},
        {'arg': 1, 'offset': 0xc, 'bytes': u32s(2)},
    ])
    c.stub(HK_ARRAY_RESERVE, 4, writes=[
        {'arg': 1, 'offset': 0x0, 'pointer': 'gb_nodes_buf+0x0'},
        {'arg': 1, 'offset': 0xc, 'bytes': u32s(2)},
    ])
    c.stub(HK_ARRAY_RESERVE, 4, writes=[
        {'arg': 1, 'offset': 0x0, 'pointer': 'gb_b_buf+0x0'},
        {'arg': 1, 'offset': 0xc, 'bytes': u32s(2)},
    ])
    cases.append(c)

    # Substate 0: link rebuild boundary where data_a_index == data_a_count (must leave src null).
    c = Case('sub0_rebuild_data_a_index_eq_count')
    set_block_fields(c, state=1, substate=0)
    setup_shape_heap(c)
    c.ptr(NVM_MANAGER, None, c.buf('nvm_mgr', 8))
    c.ptr(AI_MANAGER, None, c.buf('ai_mgr', 8))
    c.bytes(REBUILD_LINKS_FLAG, None, '00')
    parts_res = setup_res(c, 'parts_res', 4)
    nvm_res = setup_res(c, 'nvm_res', 4)
    c.ptr('block', 0x48, parts_res)
    c.ptr('block', 0x40, nvm_res)
    parts = c.buf('parts', 0xe0)
    c.ptr(parts_res, 0x88, parts)
    c.buf('entries', 0x90)
    c.bytes('entries', 0x30, u32s(100, 7))
    c.ptr(parts, 0x78, 'entries')
    c.bytes(parts, 0xc0, u32s(1))
    c.buf('links_a', 0x20)
    c.bytes('links_a', 0x00, u32s(100, 500, 1, 0))
    c.bytes('links_a', 0x10, u64s(3, 0))
    c.ptr(parts, 0x98, 'links_a')
    c.buf('data_a', 0x10 * 4, f32s(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 99, 98, 97, 96))
    c.ptr(parts, 0xa0, 'data_a')
    c.bytes(parts, 0xd0, u32s(1, 3, 0, 0))
    sec = c.buf('sec', 0xd0)
    c.bytes(sec, 0xc0, u32s(7))
    c.buf('secs', 8)
    c.ptr('secs', 0, sec)
    c.bytes(nvm_res, 0x88, u32s(1))
    c.ptr(nvm_res, 0x90, 'secs')
    ga_p = c.buf('ga_p', 8)
    c.ptr(sec, 0x88, ga_p)
    ga = c.buf('ga', 0x50)
    c.ptr(ga_p, 0, ga)
    c.bytes(ga, 0x48, u32s(0, 0x80000000))
    c.buf('ga_nodes_buf', 0x38)
    c.buf('ga_a_buf', 0x10, 'aa' * 0x10)
    c.stub(HK_ARRAY_RESERVE, 4, writes=[
        {'arg': 1, 'offset': 0x0, 'pointer': 'ga_nodes_buf+0x0'},
        {'arg': 1, 'offset': 0xc, 'bytes': u32s(1)},
    ])
    c.stub(HK_ARRAY_RESERVE, 4, writes=[
        {'arg': 1, 'offset': 0x0, 'pointer': 'ga_a_buf+0x0'},
        {'arg': 1, 'offset': 0xc, 'bytes': u32s(1)},
    ])
    cases.append(c)

    # Substate 0: node array full at capacity 2 growing to 4 via capacity doubling.
    c = Case('sub0_rebuild_node_cap_2_to_4')
    set_block_fields(c, state=1, substate=0)
    setup_shape_heap(c)
    c.ptr(NVM_MANAGER, None, c.buf('nvm_mgr', 8))
    c.ptr(AI_MANAGER, None, c.buf('ai_mgr', 8))
    c.bytes(REBUILD_LINKS_FLAG, None, '00')
    parts_res = setup_res(c, 'parts_res', 4)
    nvm_res = setup_res(c, 'nvm_res', 4)
    c.ptr('block', 0x48, parts_res)
    c.ptr('block', 0x40, nvm_res)
    parts = c.buf('parts', 0xe0)
    c.ptr(parts_res, 0x88, parts)
    c.buf('entries', 0x90)
    c.bytes('entries', 0x30, u32s(100, 7))
    c.ptr(parts, 0x78, 'entries')
    c.bytes(parts, 0xc0, u32s(1))
    c.buf('links_a', 0x20 * 3)
    for idx in range(3):
        c.bytes('links_a', idx * 0x20 + 0x00, u32s(100, 500 + idx, 0, 0))
    c.ptr(parts, 0x98, 'links_a')
    c.bytes(parts, 0xd0, u32s(3, 0, 0, 0))
    sec = c.buf('sec', 0xd0)
    c.bytes(sec, 0xc0, u32s(7))
    c.buf('secs', 8)
    c.ptr('secs', 0, sec)
    c.bytes(nvm_res, 0x88, u32s(1))
    c.ptr(nvm_res, 0x90, 'secs')
    ga_p = c.buf('ga_p', 8)
    c.ptr(sec, 0x88, ga_p)
    ga = c.buf('ga', 0x50)
    c.ptr(ga_p, 0, ga)
    c.buf('ga_nodes_init', 0x38 * 2)
    for n_idx in range(2):
        base = n_idx * 0x38
        c.bytes('ga_nodes_init', base + 0x14, u32s(0x80000000))
        c.bytes('ga_nodes_init', base + 0x24, u32s(0x80000000))
        c.bytes('ga_nodes_init', base + 0x34, u32s(0x80000000))
    c.ptr(ga, 0x40, 'ga_nodes_init')
    c.bytes(ga, 0x48, u32s(2, 0x80000002))
    c.buf('ga_nodes_4', 0x38 * 4)
    c.stub(HK_ARRAY_RESERVE, 4, writes=[
        {'arg': 1, 'offset': 0x0, 'pointer': 'ga_nodes_4+0x0'},
        {'arg': 1, 'offset': 0xc, 'bytes': u32s(4)},
    ])
    cases.append(c)

    # ---- State 1, Substate 0: connect parts (rebuild_links_on_step != 0) ----
    for tag, src_mode, n_entries, nan_test in (
        ('owner_src_3entries', 'owner', 3, False),
        ('owner_null_src_1entry', 'owner_null', 1, False),
        ('owner_null_src_2entries', 'owner_null', 2, False),
        ('area_mgr_2entries_nan', 'area_mgr', 2, True),
        ('area_mgr_absent_2entries', 'area_absent', 2, False),
        ('area_mgr_index_eq_count', 'area_eq_count', 2, False),
        ('area_mgr_oob', 'area_oob', 0, False),
    ):
        c = Case(f'sub0_connect_{tag}')
        set_block_fields(c, state=1, substate=0, block_index=0)
        c.ptr(NVM_MANAGER, None, c.buf('nvm_mgr', 8))
        c.ptr(AI_MANAGER, None, c.buf('ai_mgr', 8))
        c.bytes(REBUILD_LINKS_FLAG, None, '01')
        parts_res = setup_res(c, 'parts_res', 4)
        nvm_res = setup_res(c, 'nvm_res', 4)
        c.ptr('block', 0x48, parts_res)
        c.ptr('block', 0x40, nvm_res)
        if src_mode in ('owner', 'owner_null'):
            own = c.buf('t_owner', 0x90)
            c.ptr('block', 0x50, own)
            if src_mode == 'owner':
                src = c.buf('t_src', 0x450)
                c.bytes(src, 0x420, f32s(1, 0, 0, 10, 0, 1, 0, 20, 0, 0, 1, 30))
                c.ptr(own, 0x88, src)
        elif src_mode == 'area_mgr':
            am = c.buf('am', 0x18)
            tbl = c.buf('tbl', 0x28)
            ents = c.buf('am_ents', 0xa0)
            src = c.buf('t_src', 0x450)
            c.bytes(src, 0x420, u32s(NAN_A, 0, 0, NAN_B, 0, 0x3f800000, 0, NEG_ZERO, 0, 0, 0x3f800000, NAN_C))
            c.ptr(ents, 0x38, src)
            c.bytes(tbl, 0x18, u32s(1))
            c.ptr(tbl, 0x20, ents)
            c.ptr(am, 0x10, tbl)
            c.ptr(WORLD_AREA_MANAGER, None, am)
        elif src_mode == 'area_absent':
            c.bytes(WORLD_AREA_MANAGER, None, u64s(0))
        elif src_mode == 'area_eq_count':
            c.bytes('block', 0x34, u32s(1))
            am = c.buf('am', 0x18)
            tbl = c.buf('tbl', 0x28)
            ents = c.buf('am_ents', 0xa0 * 2)
            bad_src = c.buf('t_src_invalid_row', 0x450)
            c.bytes(bad_src, 0x420, f32s(2, 0, 0, 100, 0, 3, 0, 200, 0, 0, 4, 300))
            c.ptr(ents, 0xa0 + 0x38, bad_src)
            c.bytes(tbl, 0x18, u32s(1))
            c.ptr(tbl, 0x20, ents)
            c.ptr(am, 0x10, tbl)
            c.ptr(WORLD_AREA_MANAGER, None, am)
        elif src_mode == 'area_oob':
            c.bytes('block', 0x34, u32s(5))
            am = c.buf('am', 0x18)
            tbl = c.buf('tbl', 0x28)
            c.bytes(tbl, 0x18, u32s(1))
            c.ptr(am, 0x10, tbl)
            c.ptr(WORLD_AREA_MANAGER, None, am)

        parts = c.buf('parts', 0xe0)
        c.ptr(parts_res, 0x88, parts)
        c.bytes(parts, 0xc0, u32s(n_entries))
        if n_entries:
            ents = c.buf('p_ents', n_entries * 0x90)
            c.ptr(parts, 0x78, ents)
            secs = c.buf('secs', n_entries * 8)
            c.bytes(nvm_res, 0x88, u32s(n_entries))
            c.ptr(nvm_res, 0x90, secs)
            for i in range(n_entries):
                if nan_test and i == 0:
                    c.bytes(ents, i * 0x90 + 0x00, u32s(NAN_D, NEG_ZERO, 0x3f800000, 0))
                else:
                    c.bytes(ents, i * 0x90 + 0x00, f32s(float(i + 1), 2.0, -3.0, 1.0))
                c.bytes(ents, i * 0x90 + 0x10, f32s(0.1 * (i + 1), -0.2, 0.3, 0.0))
                c.bytes(ents, i * 0x90 + 0x30, u32s(50 + i, 10 + i))
                s = c.buf(f'sec_{i}', 0xd0)
                c.bytes(s, 0xc0, u32s(10 + i))
                gap = c.buf(f'gap_{i}', 8)
                gbp = c.buf(f'gbp_{i}', 8)
                ga = c.buf(f'ga_{i}', 8)
                gb = c.buf(f'gb_{i}', 8)
                c.ptr(gap, 0, ga)
                c.ptr(gbp, 0, gb)
                c.ptr(s, 0x88, gap)
                c.ptr(s, 0x98, gbp)
                c.ptr(secs, i * 8, s)
            for i in range(n_entries):
                for _j in range(i + 1, n_entries):
                    c.stub(CONNECT_PARTS, 8, argi=[0, 1, 2, 3, 4, 5, 6], argu32=[7],
                           argmem=[{'arg': 2, 'size': 0x40}, {'arg': 6, 'size': 0x40}])
        cases.append(c)

    # ---- State 1, Substate 1: allocate objects ----
    for tag, has_data, count, alloc_ok, src_mode in (
        ('null_data', False, 0, True, None),
        ('zero_entries', True, 0, True, None),
        ('three_entries', True, 3, True, None),
        ('alloc_fail', True, 3, False, None),
        ('owner_transform', True, 1, True, 'owner'),
        ('area_table_transform', True, 1, True, 'area_mgr'),
    ):
        c = Case(f'sub1_alloc_{tag}')
        set_block_fields(c, state=1, substate=1, block_index=1 if src_mode == 'area_mgr' else 0,
                         step_index=9, object_count=5)
        parts_res = setup_res(c, 'parts_res', 4)
        nvm_res = setup_res(c, 'nvm_res', 4)
        c.ptr('block', 0x48, parts_res)
        c.ptr('block', 0x40, nvm_res)
        if src_mode == 'owner':
            own = c.buf('t_owner', 0x90)
            src = c.buf('t_src', 0x450)
            c.bytes(src, 0x420, f32s(2.0, 0.5, -1.0, 10.0, 0.0, 3.0, 0.25, 20.0, -0.5, 0.0, 4.0, 30.0))
            c.ptr(own, 0x88, src)
            c.ptr('block', 0x50, own)
        elif src_mode == 'area_mgr':
            am = c.buf('am', 0x18)
            tbl = c.buf('tbl', 0x28)
            ents = c.buf('am_ents', 0xa0 * 2)
            src = c.buf('t_src', 0x450)
            c.bytes(src, 0x420, f32s(-1.5, 0.25, 0.5, 40.0, 0.0, 2.5, -0.75, 50.0, 1.0, 0.0, 3.5, 60.0))
            c.ptr(ents, 0xa0 + 0x38, src)
            c.bytes(tbl, 0x18, u32s(2))
            c.ptr(tbl, 0x20, ents)
            c.ptr(am, 0x10, tbl)
            c.ptr(WORLD_AREA_MANAGER, None, am)
        if has_data:
            parts = c.buf('parts', 0xe0)
            c.bytes(parts, 0xc0, u32s(count))
            c.ptr(parts_res, 0x88, parts)
        if count > 0:
            alloc = c.buf('balloc', 8)
            vt = c.buf('balloc_vt', 0x78)
            c.ptr(alloc, 0, vt)
            c.guest(vt, 0x58, BLOCK_ALLOC_SLOT)
            c.ptr(BLOCK_ALLOCATOR, None, alloc)
            if alloc_ok:
                c.buf('objs_raw', count * 8 + 0x10, 'ff' * (count * 8 + 0x10))
                c.stub(BLOCK_ALLOC_SLOT, 3, ret='buf:objs_raw+0x0')
            else:
                c.stub(BLOCK_ALLOC_SLOT, 3, ret=0)
        cases.append(c)

    # ---- State 1, Substate 2: setup objects ----
    for tag, is_loading, total, start_idx, cursor, active_slots, setup_rets, nan_mode in (
        ('loading_batch8', 1, 10, 0, 1020, (1021,), (True,) * 8, False),
        ('normal_finish', 0, 3, 1, 0, (), (True, False), False),
        ('pool_wrap_cursor_fallback', 0, 1, 0, 5, tuple(i for i in range(POOL_SLOTS) if i != 5), (True,), False),
        ('pool_exhausted', 0, 1, 0, 5, tuple(range(POOL_SLOTS)), (), False),
        ('nan_bounds_and_transform', 0, 1, 0, 0, (), (True,), True),
    ):
        c = Case(f'sub2_{tag}')
        set_block_fields(c, state=1, substate=2, step_index=start_idx, object_count=total)
        if nan_mode:
            c.bytes('block', 0x00, u32s(NAN_A, 0x3f800000, 0, NAN_B,
                                        0, NAN_C, 0x3f800000, NEG_ZERO,
                                        0, 0, 0x3f800000, NAN_D))
            c.bytes('block', 0x70, u32s(NAN_A, 0x3f800000, NEG_ZERO, 0x3f800000))
            c.bytes('block', 0x80, u32s(0x3f800000, NAN_B, NEG_ZERO, 0x3f800000))
        else:
            c.bytes('block', 0x00, f32s(1, 0, 0, 1, 0, 1, 0, 2, 0, 0, 1, 3))
            c.bytes('block', 0x70, f32s(10.0, 10.0, 10.0, 1.0))
            c.bytes('block', 0x80, f32s(-10.0, -10.0, -10.0, 1.0))

        parts_res = setup_res(c, 'parts_res', 4)
        nvm_res = setup_res(c, 'nvm_res', 4)
        c.ptr('block', 0x48, parts_res)
        c.ptr('block', 0x40, nvm_res)
        parts = c.buf('parts', 0xe0)
        c.ptr(parts_res, 0x88, parts)
        c.bytes(parts, 0xc0, u32s(total))
        if total > 0:
            c.buf('objs_raw', total * 8 + 0x10)
            c.ptr('block', 0x60, 'objs_raw+0x10')
            ents = c.buf('p_ents', total * 0x90)
            c.ptr(parts, 0x78, ents)
            secs = c.buf('secs', 8)
            s0 = c.buf('sec0', 0xd0)
            c.bytes(s0, 0xc0, u32s(77))
            c.ptr(secs, 0, s0)
            c.bytes(nvm_res, 0x88, u32s(1))
            c.ptr(nvm_res, 0x90, secs)
            for i in range(total):
                if nan_mode:
                    c.bytes(ents, i * 0x90 + 0x00, u32s(NAN_D, NEG_ZERO, 0x3f800000, 0))
                else:
                    c.bytes(ents, i * 0x90 + 0x00, f32s(float(i), 1.0, -1.0, 0.0))
                c.bytes(ents, i * 0x90 + 0x10, f32s(0.1, 0.2, -0.3, 0.0))
                c.bytes(ents, i * 0x90 + 0x34, u32s(77))

        lh = c.buf('loading_helper', 0x60)
        c.bytes(lh, 0x51, f'{is_loading:02x}')
        c.ptr(LOADING_HELPER, None, lh)
        setup_nvm_manager(c, active_slots=active_slots, cursor=cursor)
        for idx_ret, ok in enumerate(setup_rets):
            if ok:
                m = c.buf(f'obj_m_{idx_ret}', 0xa0)
                if nan_mode:
                    c.bytes(m, 0x70, u32s(NAN_C, 0xbf800000, 0, 0x3f800000))
                    c.bytes(m, 0x80, u32s(0x40000000, NAN_D, 0, 0x3f800000))
                else:
                    c.bytes(m, 0x70, f32s(-20.0, 5.0, 12.0, 99.0))
                    c.bytes(m, 0x80, f32s(20.0, -15.0, 5.0, 99.0))
                c.stub(OBJECT_SETUP, 6, ret=1, argmem=[{'arg': 5, 'size': 0x30}],
                       writes=[{'arg': 0, 'offset': 0x48, 'pointer': f'{m}+0x0'}])
            else:
                c.stub(OBJECT_SETUP, 6, ret=0, argmem=[{'arg': 5, 'size': 0x30}])
        cases.append(c)

    # ---- State 1, Substate 3 & 5: prepare & finalize links ----
    for sub, fn_addr, name in ((3, OBJECT_PREPARE_LINKS, 'sub3_prepare'),
                               (5, OBJECT_FINALIZE_LINKS, 'sub5_finalize')):
        c = Case(name)
        # Overlap o0 (0x00..0x750) and block (+0x200..0x2b0) so arg 0 + 0x258 writes block->object_count.
        # Avoid o0 fields used by is_object_active (+0x30, +0x134, +0x740).
        c.buf('block', 0x750)
        c.args = {'rdi': 'buf:block+0x200'}
        c.bytes('block', 0x200 + 0x58, u32s(5))
        c.bytes('block', 0x200 + 0x90, u32s(1, sub, 0))
        parts_res = setup_res(c, 'parts_res', 4)
        nvm_res = setup_res(c, 'nvm_res', 4)
        c.ptr('block', 0x200 + 0x48, parts_res)
        c.ptr('block', 0x200 + 0x40, nvm_res)
        c.buf('objs', 8 * 6)
        c.ptr('block', 0x200 + 0x60, 'objs')
        c.bytes('block', 0x30, u32s(1))
        c.bytes('block', 0x134, u32s(5))
        c.bytes('block', 0x740, '01')
        o1 = make_object(c, 'o1', kind=2, idx=1, flags=1)
        o2 = make_object(c, 'o2', kind=1, idx=-1, flags=1)
        o3 = make_object(c, 'o3', kind=1, idx=1, flags=0)
        o5 = make_object(c, 'o5', active=True)
        c.ptr('objs', 0x00, 'block+0x0')
        c.ptr('objs', 0x08, o1)
        c.ptr('objs', 0x10, o2)
        c.ptr('objs', 0x18, o3)
        c.ptr('objs', 0x28, o5)
        c.stub(fn_addr, 1, writes=[{'arg': 0, 'offset': 0x200 + 0x58, 'bytes': u32s(6)}])
        c.stub(fn_addr, 1)
        cases.append(c)

    # ---- State 1, Substate 4: attach links ----
    c = Case('sub4_attach_links')
    set_block_fields(c, state=1, substate=4, object_count=2)
    parts_res = setup_res(c, 'parts_res', 4)
    nvm_res = setup_res(c, 'nvm_res', 4)
    c.ptr('block', 0x48, parts_res)
    c.ptr('block', 0x40, nvm_res)
    parts = c.buf('parts', 0xe0)
    c.ptr(parts_res, 0x88, parts)
    c.buf('objs', 8 * 2)
    o0 = make_object(c, 'o0', model_id=55)
    c.ptr('objs', 0x00, o0)
    c.ptr('block', 0x60, 'objs')
    links_b = c.buf('links_b', 0x20 * 2)
    c.bytes(links_b, 0x10, u32s(55))
    c.bytes(links_b, 0x20 + 0x10, u32s(999))
    c.ptr(parts, 0xb0, links_b)
    c.bytes(parts, 0xdc, u32s(2))
    setup_nvm_manager(c, blocks=(1, 2, 2))
    found_obj = make_object(c, 'found_obj')
    # Link 0: block 0 skipped (state 1); block 1 matches and stops before block 2
    c.stub(FIND_LINK_OBJECT, 3, ret=f'buf:{found_obj}+0x0',
           writes=[{'arg': 2, 'offset': 0, 'bytes': f32s(1.5, 2.5, 3.5, 1.0)}])
    c.stub(OBJECT_ATTACH_LINK, 4, argmem=[{'arg': 3, 'size': 16}])
    # Link 1: block 1 returns null, block 2 returns found_obj, no matching local obj -> attach skipped
    c.stub(FIND_LINK_OBJECT, 3, ret=0)
    c.stub(FIND_LINK_OBJECT, 3, ret=f'buf:{found_obj}+0x0',
           writes=[{'arg': 2, 'offset': 0, 'bytes': f32s(4.0, 5.0, 6.0, 1.0)}])
    cases.append(c)

    c = Case('sub4_attach_null_data')
    set_block_fields(c, state=1, substate=4)
    c.ptr('block', 0x48, setup_res(c, 'parts_res', 4))
    c.ptr('block', 0x40, setup_res(c, 'nvm_res', 4))
    cases.append(c)

    # ---- State 1, Substate 6: register menu ----
    c = Case('sub6_area_not_ready')
    set_block_fields(c, state=1, substate=6)
    setup_area(c, area_state=1)
    c.ptr('block', 0x48, setup_res(c, 'parts_res', 4))
    c.ptr('block', 0x40, setup_res(c, 'nvm_res', 4))
    cases.append(c)

    for tag, parent_ok, block_menu_ok, add_item_ok, node_alloc_ok, heap_ok, label_heap, create_heap in (
        ('no_parent_with_block_menu', False, True, False, False, True, False, False),
        ('full_success_create_heap', True, True, True, True, True, False, True),
        ('full_success_heap_label_invalid_heap', True, True, True, True, False, True, False),
        ('add_item_fails_frees_node', True, True, False, True, True, False, False),
        ('add_item_fails_null_node', True, True, False, False, True, False, False),
        ('no_block_menu', True, False, False, False, True, False, False),
    ):
        c = Case(f'sub6_{tag}')
        set_block_fields(c, state=1, substate=6, map_id=0x1c070000, object_count=1)
        setup_area(c, area_state=2)
        c.ptr('block', 0x48, setup_res(c, 'parts_res', 4))
        c.ptr('block', 0x40, setup_res(c, 'nvm_res', 4))
        c.buf('objs', 8)
        o0 = make_object(c, 'o0', active=True)
        c.ptr('objs', 0, o0)
        c.ptr('block', 0x60, 'objs')

        menu = c.buf('menu', 0x10)
        menu_alloc = c.buf('menu_alloc', 8)
        menu_vt = c.buf('menu_vt', 0x80)
        c.ptr(menu_alloc, 0, menu_vt)
        c.guest(menu_vt, 0x58, MENU_ALLOC_SLOT)
        c.ptr(menu, 0x08, menu_alloc)
        c.ptr(MENU_MANAGER, None, menu)

        c.stub(NAME_CONSTRUCT, 2, writes=[{'arg': 0, 'bytes': u64s(0x111)}])
        c.stub(MENU_FIND, 2, ret='buf:parent+0x0' if parent_ok else 0)
        c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
        if parent_ok:
            c.buf('parent', 0x10)
            c.gs = f'buf:tls+{TLS_THREAD_OFF:#x}'
            c.buf('tls', TLS_THREAD_OFF + 8)
            c.bytes(THREAD_HEAP_OFFSET, None, u64s(TLS_HEAP_SLOT))
            c.bytes(THREAD_HEAP_SIZE_OFFSET, None, u64s(TLS_SIZE_SLOT))
            c.ptr('tls', TLS_THREAD_OFF, f'tls+{TLS_THREAD_OFF:#x}')
            theap = c.buf('theap', 0x38)
            theap_vt = c.buf('theap_vt', 0x80)
            c.ptr(theap, 0x28, theap_vt)
            c.guest(theap_vt, 0x20, HEAP_INFO_SLOT)
            c.guest(theap_vt, 0x70, HEAP_FREE_SLOT)
            if create_heap:
                c.bytes('tls', TLS_THREAD_OFF + TLS_HEAP_SLOT, u64s(0))
                c.bytes('tls', TLS_THREAD_OFF + TLS_SIZE_SLOT, u64s(0))
                c.ptr(QUIT_QUERY, None, c.buf('quit_q', 8))
                dalloc = c.buf('dalloc', 8)
                dvt = c.buf('dalloc_vt', 0x80)
                c.ptr(dalloc, 0, dvt)
                c.guest(dvt, 0x50, BLOCK_ALLOC_SLOT)
                c.ptr(DEFAULT_ALLOCATOR, None, dalloc)
                c.buf('theap_arena', 0x20000)
                c.stub(BLOCK_ALLOC_SLOT, 2, ret='buf:theap_arena+0x0')
                # 0x0247d250 initializes theap_arena's allocator vtable at +0x28 so WIDE_FORMAT sees flags & 0x20.
            else:
                c.ptr('tls', TLS_THREAD_OFF + TLS_HEAP_SLOT, theap)
                c.stub(HEAP_INFO_SLOT, 3, writes=[{'arg': 0, 'offset': 0, 'bytes': '20' if heap_ok else '00'}])
                if not heap_ok:
                    c.fatal(argc=3)
            if label_heap:
                c.buf('label_heap_buf', 0x40, u16_bytes('World Block Nvm[BlockNo:07]'))
                c.stub(WIDE_FORMAT, 3, writes=[
                    {'arg': 0, 'offset': 0x8, 'pointer': 'label_heap_buf+0x0'},
                    {'arg': 0, 'offset': 0x18, 'bytes': u64s(29, 32)},
                ])
            else:
                c.stub(WIDE_FORMAT, 3, writes=[
                    {'arg': 0, 'offset': 0x8, 'bytes': u16_bytes('Blk07')},
                    {'arg': 0, 'offset': 0x18, 'bytes': u64s(5, 7)},
                ])
            c.stub(NAME_CONSTRUCT, 2, writes=[{'arg': 0, 'bytes': u64s(0x222)}])
            c.stub(MENU_ADD, 4, ret='buf:bmenu+0x0' if block_menu_ok else 0,
                   argmem=[{'arg': 1, 'size': 10}, {'arg': 2, 'size': 10}, {'arg': 3, 'size': 16}])
            c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
            if block_menu_ok:
                c.buf('bmenu', 0x10)
                c.stub(NAME_CONSTRUCT, 2, writes=[{'arg': 0, 'bytes': u64s(0x333)}])
                c.stub(MENU_ADD, 4, ret='buf:bank_menu+0x0', argmem=[{'arg': 3, 'size': 16}])
                c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
                bank_menu = c.buf('bank_menu', 0x10)
                bank_vt = c.buf('bank_vt', 0x70)
                c.ptr(bank_menu, 0, bank_vt)
                c.guest(bank_vt, 0x68, BANK_ADD_ITEM_SLOT)
                if node_alloc_ok:
                    c.buf('node', 0x30, 'a5' * 0x30)
                    c.stub(MENU_ALLOC_SLOT, 3, ret='buf:node+0x0')
                    c.stub(MENU_ITEM_CONSTRUCT, 1)
                else:
                    c.stub(MENU_ALLOC_SLOT, 3, ret=0)
                c.stub(BANK_ADD_ITEM_SLOT, 3, ret=1 if add_item_ok else 0)
                if not add_item_ok and node_alloc_ok:
                    nalloc = c.buf('node_alloc', 8)
                    nvt = c.buf('node_alloc_vt', 0x78)
                    c.ptr(nalloc, 0, nvt)
                    c.guest(nvt, 0x70, NODE_FREE_SLOT)
                    c.stub(HEAP_OWNING, 1, ret='buf:node_alloc+0x0')
                if add_item_ok:
                    c.buf('prop4', 0x30, 'cc' * 0x30)
                    c.buf('prop5', 0x30, 'dd' * 0x30)
                    c.stub(MENU_ALLOC_SLOT, 3, ret='buf:prop4+0x0')
                    c.stub(MENU_BIND_PROPERTY, 3, argmem=[{'arg': 2, 'size': 0x2c}])
                    c.stub(MENU_ALLOC_SLOT, 3, ret='buf:prop5+0x0')
                    c.stub(MENU_BIND_PROPERTY, 3, argmem=[{'arg': 2, 'size': 0x2c}])
            if label_heap and not create_heap:
                c.stub(HEAP_FREE_SLOT, 2)

        c.stub(NAME_CONSTRUCT, 2, writes=[{'arg': 0, 'bytes': u64s(0x444)}])
        c.stub(MENU_FIND, 2, ret='buf:bmenu2+0x0' if block_menu_ok else 0)
        c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
        if block_menu_ok:
            c.buf('bmenu2', 0x10)
            c.stub(OBJECT_ADD_MENU, 2)
        if tag == 'add_item_fails_frees_node':
            # Original overwrites node->vtable with 0x056f1f80 after construction; slot 0 destroys at 0x02d865c0.
            c.stub('0x02d865c0', 1)
            c.stub(NODE_FREE_SLOT, 2)
        cases.append(c)

    # ---- State 2 & State 3 ----
    for ureq in (0, 1):
        c = Case(f'state2_unload_{ureq}')
        set_block_fields(c, state=2, unload_req=ureq)
        cases.append(c)

    c = Case('state3_begin_unload')
    set_block_fields(c, state=3, unload_substate=2)
    cases.append(c)

    # ---- State 4: unloading ----
    c = Case('state4_sub0_clear_menus')
    set_block_fields(c, state=4, unload_substate=0, object_count=2)
    c.bytes('block', 0x70, f32s(1, 2, 3, 4))
    c.bytes('block', 0x80, f32s(5, 6, 7, 8))
    c.buf('objs', 16)
    o0 = make_object(c, 'o0', active=True)
    o1 = make_object(c, 'o1', active=False)
    c.ptr('objs', 0, o0)
    c.ptr('objs', 8, o1)
    c.ptr('block', 0x60, 'objs')
    c.ptr(MENU_MANAGER, None, c.buf('menu', 0x10))
    c.stub(NAME_CONSTRUCT, 2, writes=[{'arg': 0, 'bytes': u64s(0x555)}])
    c.stub(MENU_REMOVE, 4, argmem=[{'arg': 1, 'size': 16}])
    c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
    c.stub(REMOVE_OBJECT_MENUS, 1)
    cases.append(c)

    for step_idx, expected_calls in ((10, 9), (8, 9), (5, 5), (0, 1)):
        c = Case(f'state4_sub1_step_{step_idx}')
        set_block_fields(c, state=4, unload_substate=1, step_index=step_idx, object_count=12)
        c.buf('objs', 12 * 8)
        c.ptr('block', 0x60, 'objs')
        for idx in range(12):
            if step_idx == 5 and idx == 2:
                continue
            obj = make_object(c, f'uobj_{idx}', active=True)
            c.ptr('objs', idx * 8, obj)
        for _ in range(expected_calls):
            c.stub(OBJECT_UNLOAD, 1)
        cases.append(c)

    c = Case('state4_sub1_step_int32_min')
    set_block_fields(c, state=4, unload_substate=1, step_index=0x80000000, object_count=0)
    cases.append(c)

    for tag, has_all in (('release_all', True), ('release_nulls', False)):
        c = Case(f'state4_sub2_{tag}')
        set_block_fields(c, state=4, unload_substate=2, object_count=2)
        c.ptr('block', 0x50, c.buf('t_own', 8))
        if has_all:
            c.buf('objs_raw', 0x20)
            c.ptr('objs_raw', 0, 'objs_raw')
            c.ptr('block', 0x60, 'objs_raw+0x10')
            oalloc = c.buf('oalloc', 8)
            ovt = c.buf('ovt', 0x78)
            c.ptr(oalloc, 0, ovt)
            c.guest(ovt, 0x70, BLOCK_FREE_SLOT)
            c.stub(HEAP_OWNING, 1, ret='buf:oalloc+0x0')
            c.stub(BLOCK_FREE_SLOT, 2)
            c.ptr('block', 0x40, setup_res(c, 'nvm_res', 4))
            c.ptr('block', 0x48, setup_res(c, 'parts_res', 4))
            sf = c.buf('sprj', 8)
            svt = c.buf('sprj_vt', 0x40)
            c.ptr(sf, 0, svt)
            c.guest(svt, 0x30, SPRJ_RELEASE_SLOT)
            c.ptr(SPRJ_FILE, None, sf)
            c.stub(SPRJ_RELEASE_SLOT, 2)
            c.stub(SPRJ_RELEASE_SLOT, 2)
        cases.append(c)

    # Also test missing FD4DebugMenuManager in state 4 substate 0 (fatal_error returns, stubbed remove runs).
    c = Case('state4_sub0_menu_missing')
    set_block_fields(c, state=4, unload_substate=0, object_count=1)
    c.buf('objs', 8)
    o0 = make_object(c, 'o0', active=True)
    c.ptr('objs', 0, o0)
    c.ptr('block', 0x60, 'objs')
    c.bytes(MENU_MANAGER, None, u64s(0))
    c.fatal()
    c.stub(NAME_CONSTRUCT, 2, writes=[{'arg': 0, 'bytes': u64s(0x555)}])
    c.stub(MENU_REMOVE, 4, argmem=[{'arg': 1, 'size': 16}])
    c.stub(NAME_DESTROY, 1, argmem=[{'arg': 0, 'size': 8}])
    c.stub(REMOVE_OBJECT_MENUS, 1)
    cases.append(c)

    c = Case('state4_sub3_done')
    set_block_fields(c, state=4, unload_substate=3)
    cases.append(c)

    # ---- State 5: idle ----
    for tag, reload_req, p_st, n_st in (
        ('no_reload', 0, None, None),
        ('reload_unloaded', 1, 3, 4),
        ('reload_null', 1, None, 4),
        ('reload_ready', 1, 4, 4),
    ):
        c = Case(f'state5_{tag}')
        set_block_fields(c, state=5, substate=7, reload_req=reload_req)
        if p_st is not None:
            c.ptr('block', 0x48, setup_res(c, 'parts_res', p_st))
        if n_st is not None:
            c.ptr('block', 0x40, setup_res(c, 'nvm_res', n_st))
        cases.append(c)

    return cases


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    cases = [c.json() for c in build_cases()]
    for c in cases:
        with open(os.path.join(out_dir, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(cases)} cases written to {out_dir}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
