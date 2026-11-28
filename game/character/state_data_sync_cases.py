#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for character_state_data_sync (0x01cf7880), for tools/verify.py run.

usage: state_data_sync_cases.py OUT_DIR

Every input sits in a named buffer; virtual methods on ChrIns (+0x1c0, +0x1c8, +0x2b8) point at
spare guest addresses that the cases stub, and direct callees (0x01cfcaf0, 0x01cfccb0, 0x01cfd110,
0x01979450, 0x01be1860, 0x01bec080) are stubbed directly. Covers:
- early exit when vtable+0x1c8 returns null (no writes, no further calls);
- signed clamping of three modules->points values (+0xf8, +0x128, +0x134) against prior state
  limits (+0x18, +0x24, +0x34) before updating those limits from (+0xfc, +0x12c, +0x138), including
  equal, negative, INT32_MIN, and INT32_MAX boundaries;
- EventGate bit 0 propagation into state+0x650 bit 4 (null gate, bit 0 clear with other bits set,
  bit 0 set, neighboring bits preserved) and pre-call write ordering before 0x01cfcaf0,
  vtable+0x2b8, 0x01979450, and 0x01be1860;
- byte-wide return ABI (`test al, al`) for vtable+0x1c0 early exit after 0x01cfcaf0 (0x01, 0x02,
  0x80 exit early; 0x00000100 and 0x00000001_00000000 continue), plus vtable reload after 0x01cfcaf0;
- post-call reloads of self->effects, self->modules, and self->vtable after 0x01cfd110, and
  self->effects reload after vtable+0x2b8;
- effect list mask gating, inactive-flag bits (each bit of 0x800c0003 skips; ~0x800c0003 stays
  active), null-param skipping, and first-active-match semantics for special types (0x88, 0x75,
  0x74 -> 999.0f/1000.0f vs -1.0f/-1.0f) and float-pair types (2, 5, 6, 0x104), across every mask
  bit of each category;
- ordered `> 0.0f` counter zeroing for all five active_counters (+0x61c..+0x62c): +0.0f, -0.0f,
  smallest positive/negative subnormals, +inf, -inf, qNaN (preserving -0.0f and NaN payload bit
  patterns in state), and opposite-sign value_0x00 vs value_0x04 separation, while raw_counters
  (+0x100..+0x110) keep un-zeroed values with slots 1 and 2 swapped and maxima (+0x654..+0x664)
  copy directly;
- CounterModule::flags_0x4c low 4 bits mirrored into state+0x650 bits 0..3 (preserving bits 4..7
  and ignoring bit 4 of flags_0x4c) followed by full 32-bit zeroing of flags_0x4c;
- type-0x9b bonus loop gated by mask 0x0e00, summing all active nodes with signed int16_t
  extension (+0x13a), FP accumulation order (summing bonus before adding to base_stat_0x2b8), and
  truncation via cvttss2si (positive/negative fractions, NaN, +inf, -inf -> INT32_MIN);
- query_effect_mode_01979450 return store (+0x66c) and cap_0x604 = 999999;
- associated object handling: null assoc_first (+0x670) skips both 0x01be1860 calls, both -1.0f
  resets, and the post-assoc self->effects reload; non-null assoc_first calls 0x01be1860 twice with
  reloaded assoc_second (+0x678), writes -1.0f after each call, and reloads self->effects afterward;
- unconditional 0x01bec080 iteration over all effect nodes (including inactive and null-param nodes),
  empty list handling, and post-call node->next reload.
"""
import json
import os
import struct
import sys

STEP_01CFCAF0 = 0x01cfcaf0
STEP_01CFCCB0 = 0x01cfccb0
STEP_01CFD110 = 0x01cfd110
QUERY_EFFECT_MODE = 0x01979450
SYNC_ASSOCIATED = 0x01be1860
PROCESS_EFFECT_NODE = 0x01bec080

# Spare guest code addresses used as distinct virtual-method stub targets.
VT_SKIP_SYNC = 0x00500000
VT_STATE_DATA = 0x00500100
VT_BASE_STAT = 0x00500200
VT2_SKIP_SYNC = 0x00500300
VT2_BASE_STAT = 0x00500400


def u16_hex(v):
    return struct.pack('<H', v & 0xffff).hex()


def i16_hex(v):
    return struct.pack('<h', v).hex()


def u32_hex(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def i32_hex(v):
    return struct.pack('<i', v).hex()


def f32_hex(v):
    return struct.pack('<f', v).hex()


def f32_u32(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def u32_bits_hex(bits):
    return struct.pack('<I', bits & 0xffffffff).hex()


def build_effect_list(bufs, mem, prefix, mask, nodes):
    """Populate an EffectList and its linked nodes/params in bufs and mem."""
    list_name = f'{prefix}_list'
    bufs[list_name] = {'size': 0x40}
    mem.append({'addr': f'buf:{list_name}+0x20', 'bytes': u32_hex(mask)})

    prev_node = None
    for idx, spec in enumerate(nodes):
        nname = f'{prefix}_n{idx}'
        bufs[nname] = {'size': 0x68}
        val_a = spec.get('val_a_bits')
        if val_a is None:
            val_a = f32_u32(spec.get('val_a', 0.0))
        val_b = spec.get('val_b_bits')
        if val_b is None:
            val_b = f32_u32(spec.get('val_b', 0.0))
        mem.append({'addr': f'buf:{nname}+0x00', 'bytes': u32_bits_hex(val_a)})
        mem.append({'addr': f'buf:{nname}+0x04', 'bytes': u32_bits_hex(val_b)})
        mem.append({'addr': f'buf:{nname}+0x1c', 'bytes': u32_hex(spec.get('flags', 0))})

        if spec.get('has_param', True):
            pname = f'{prefix}_p{idx}'
            bufs[pname] = {'size': 0x160}
            mem.append({'addr': f'buf:{pname}+0x13a', 'bytes': i16_hex(spec.get('bonus', 0))})
            mem.append({'addr': f'buf:{pname}+0x156', 'bytes': u16_hex(spec.get('type', 0))})
            mem.append({'addr': f'buf:{nname}+0x48', 'pointer': pname})

        if prev_node is None:
            mem.append({'addr': f'buf:{list_name}+0x08', 'pointer': nname})
        else:
            mem.append({'addr': f'buf:{prev_node}+0x58', 'pointer': nname})
        prev_node = nname
    return list_name


def case(
    cid,
    *,
    state_exists=True,
    points_vals=(100, 50, 80),
    points_limits=(200, 100, 150),
    state_init_limits=(120, 60, 90),
    gate_exists=True,
    gate_flags=0x00,
    init_state_flags=0x00,
    skip_sync_ret=0,
    counters=(11, 22, 33, 44, 55),
    maxima=(111, 222, 333, 444, 555),
    counter_flags=0x00000000,
    effects_mask=0x00000000,
    effect_nodes=(),
    base_stat_bits=None,
    base_stat=0.0,
    effect_mode_ret=3,
    assoc_first_exists=False,
    assoc_second_exists=True,
    init_assoc_reset=(12.5, 25.0),
    # Reload / callback observation hooks:
    mutate_state_in_caf0=False,
    switch_vtable_in_caf0=False,
    vt2_skip_sync_ret=0,
    switch_modules_and_effects_in_d110=False,
    switch_vtable_in_d110=False,
    vt2_base_stat=42.25,
    alt_counters=(91, 92, 93, 94, 95),
    alt_maxima=(901, 902, 903, 904, 905),
    alt_counter_flags=0x0000000a,
    alt_effects_mask=0x00000e00,
    alt_effect_nodes=(),
    mutate_pre_2b8_writes=False,
    switch_effects_in_base_stat=False,
    post_2b8_effects_mask=0x00000e00,
    post_2b8_effect_nodes=(),
    mutate_stat668_in_query_mode=False,
    switch_effects_in_query_mode=False,
    query_mode_effects_mask=0x00000000,
    query_mode_effect_nodes=(),
    mutate_in_first_assoc=False,
    switch_effects_in_first_assoc=False,
    post_assoc_effects_mask=0x00000000,
    post_assoc_effect_nodes=(),
    mutate_next_in_first_node=False,
    unused_regs=False,
):
    """Build one synthetic schema-1 differential test case for 0x01cf7880."""
    bufs = {
        'self': {'size': 0x400},
        'vtable': {'size': 0x300},
        'state': {'size': 0x690},
        'modules': {'size': 0x40},
        'points': {'size': 0x150},
        'counters': {'size': 0x60},
        'gate_holder': {'size': 0x40},
        'assoc1': {'size': 0x40},
        'assoc2': {'size': 0x40},
    }
    mem = [
        {'addr': 'buf:self+0x00', 'pointer': 'vtable'},
        {'addr': 'buf:self+0x18', 'pointer': 'gate_holder'},
        {'addr': 'buf:self+0x3b0', 'pointer': 'modules'},
        {'addr': 'buf:modules+0x20', 'pointer': 'points'},
        {'addr': 'buf:modules+0x28', 'pointer': 'counters'},
        {'addr': 'buf:vtable+0x1c0', 'guest': f'0x{VT_SKIP_SYNC:08x}'},
        {'addr': 'buf:vtable+0x1c8', 'guest': f'0x{VT_STATE_DATA:08x}'},
        {'addr': 'buf:vtable+0x2b8', 'guest': f'0x{VT_BASE_STAT:08x}'},
    ]

    if gate_exists:
        bufs['gate'] = {'size': 0x20}
        mem.append({'addr': 'buf:gate_holder+0x30', 'pointer': 'gate'})
        mem.append({'addr': 'buf:gate+0x18', 'bytes': f'{gate_flags & 0xff:02x}'})

    # Points values and limits
    mem.append({'addr': 'buf:points+0xf8', 'bytes': i32_hex(points_vals[0])})
    mem.append({'addr': 'buf:points+0xfc', 'bytes': i32_hex(points_limits[0])})
    mem.append({'addr': 'buf:points+0x128', 'bytes': i32_hex(points_vals[1])})
    mem.append({'addr': 'buf:points+0x12c', 'bytes': i32_hex(points_limits[1])})
    mem.append({'addr': 'buf:points+0x134', 'bytes': i32_hex(points_vals[2])})
    mem.append({'addr': 'buf:points+0x138', 'bytes': i32_hex(points_limits[2])})

    # Initial state limits, flags, and associated pointers
    mem.append({'addr': 'buf:state+0x18', 'bytes': i32_hex(state_init_limits[0])})
    mem.append({'addr': 'buf:state+0x24', 'bytes': i32_hex(state_init_limits[1])})
    mem.append({'addr': 'buf:state+0x34', 'bytes': i32_hex(state_init_limits[2])})
    mem.append({'addr': 'buf:state+0x650', 'bytes': f'{init_state_flags & 0xff:02x}'})

    if assoc_first_exists:
        mem.append({'addr': 'buf:state+0x670', 'pointer': 'assoc1'})
    if assoc_second_exists:
        mem.append({'addr': 'buf:state+0x678', 'pointer': 'assoc2'})
    mem.append({'addr': 'buf:assoc1+0x28', 'bytes': f32_hex(init_assoc_reset[0])})
    mem.append({'addr': 'buf:assoc2+0x28', 'bytes': f32_hex(init_assoc_reset[1])})

    # Counters, maxima, and flags_0x4c
    for i in range(5):
        mem.append({'addr': f'buf:counters+0x{0x10 + i * 4:02x}', 'bytes': i32_hex(counters[i])})
        mem.append({'addr': f'buf:counters+0x{0x24 + i * 4:02x}', 'bytes': i32_hex(maxima[i])})
    mem.append({'addr': 'buf:counters+0x4c', 'bytes': u32_hex(counter_flags)})

    # Primary effect list
    eff_name = build_effect_list(bufs, mem, 'eff', effects_mask, effect_nodes)
    mem.append({'addr': 'buf:self+0x1c8', 'pointer': eff_name})

    # Optional alternate vtable / modules / effect lists for reload tests
    if switch_vtable_in_caf0 or switch_vtable_in_d110:
        bufs['vtable2'] = {'size': 0x300}
        mem.append({'addr': 'buf:vtable2+0x1c0', 'guest': f'0x{VT2_SKIP_SYNC:08x}'})
        mem.append({'addr': 'buf:vtable2+0x1c8', 'guest': f'0x{VT_STATE_DATA:08x}'})
        mem.append({'addr': 'buf:vtable2+0x2b8', 'guest': f'0x{VT2_BASE_STAT:08x}'})

    alt_eff_name = None
    if switch_modules_and_effects_in_d110:
        bufs['modules2'] = {'size': 0x40}
        bufs['counters2'] = {'size': 0x60}
        mem.append({'addr': 'buf:modules2+0x20', 'pointer': 'points'})
        mem.append({'addr': 'buf:modules2+0x28', 'pointer': 'counters2'})
        for i in range(5):
            mem.append({'addr': f'buf:counters2+0x{0x10 + i * 4:02x}', 'bytes': i32_hex(alt_counters[i])})
            mem.append({'addr': f'buf:counters2+0x{0x24 + i * 4:02x}', 'bytes': i32_hex(alt_maxima[i])})
        mem.append({'addr': 'buf:counters2+0x4c', 'bytes': u32_hex(alt_counter_flags)})
        alt_eff_name = build_effect_list(bufs, mem, 'eff_alt', alt_effects_mask, alt_effect_nodes)

    post_2b8_eff_name = None
    if switch_effects_in_base_stat:
        post_2b8_eff_name = build_effect_list(bufs, mem, 'eff_2b8', post_2b8_effects_mask, post_2b8_effect_nodes)

    qm_eff_name = None
    if switch_effects_in_query_mode:
        qm_eff_name = build_effect_list(bufs, mem, 'eff_qm', query_mode_effects_mask, query_mode_effect_nodes)

    post_assoc_eff_name = None
    if switch_effects_in_first_assoc:
        post_assoc_eff_name = build_effect_list(bufs, mem, 'eff_pa', post_assoc_effects_mask, post_assoc_effect_nodes)

    if mutate_in_first_assoc:
        bufs['assoc2_alt'] = {'size': 0x40}
        mem.append({'addr': 'buf:assoc2_alt+0x28', 'bytes': f32_hex(77.5)})

    # Build scripted stubs for the executed path, plus safe fallback stubs on early-exit / alternate-vtable
    # targets so erroneous replacements produce clean behavioral diffs instead of crashing into unstubbed code.
    stubs = []
    stubs.append({
        'address': f'0x{VT_STATE_DATA:08x}',
        'argc': 1,
        'ret': 'buf:state' if state_exists else 0,
    })

    caf0_stub = {'address': f'0x{STEP_01CFCAF0:08x}', 'argc': 1, 'ret': 0}
    caf0_writes = []
    if mutate_state_in_caf0:
        # Mutate state fields written before 0x01cfcaf0: original writes already happened, so
        # these mutations survive (a replacement delaying writes after 0x01cfcaf0 overwrites them).
        caf0_writes.extend([
            {'addr': 'buf:state+0x14', 'bytes': i32_hex(777)},
            {'addr': 'buf:state+0x18', 'bytes': i32_hex(888)},
            {'addr': 'buf:state+0x5f4', 'bytes': i32_hex(12345)},
            {'addr': 'buf:state+0x650', 'bytes': '55'},
            {'addr': 'buf:points+0xf8', 'bytes': i32_hex(9999)},
            {'addr': 'buf:points+0xfc', 'bytes': i32_hex(9999)},
        ])
    if switch_vtable_in_caf0:
        caf0_writes.append({'addr': 'buf:self+0x00', 'pointer': 'vtable2'})
    if caf0_writes:
        caf0_stub['writes'] = caf0_writes
    stubs.append(caf0_stub)

    stubs.append({'address': f'0x{VT_SKIP_SYNC:08x}', 'argc': 1, 'ret': skip_sync_ret})
    if switch_vtable_in_caf0 or switch_vtable_in_d110:
        stubs.append({'address': f'0x{VT2_SKIP_SYNC:08x}', 'argc': 1, 'ret': vt2_skip_sync_ret})

    stubs.append({'address': f'0x{STEP_01CFCCB0:08x}', 'argc': 1, 'ret': 0})

    d110_stub = {'address': f'0x{STEP_01CFD110:08x}', 'argc': 1, 'ret': 0}
    d110_writes = []
    if switch_modules_and_effects_in_d110:
        d110_writes.append({'addr': 'buf:self+0x3b0', 'pointer': 'modules2'})
        d110_writes.append({'addr': 'buf:self+0x1c8', 'pointer': alt_eff_name})
    if switch_vtable_in_d110:
        d110_writes.append({'addr': 'buf:self+0x00', 'pointer': 'vtable2'})
    if d110_writes:
        d110_stub['writes'] = d110_writes
    stubs.append(d110_stub)

    if base_stat_bits is None:
        vt1_ret_bits = f32_u32(base_stat)
        vt2_ret_bits = f32_u32(vt2_base_stat)
    else:
        vt1_ret_bits = base_stat_bits
        vt2_ret_bits = base_stat_bits

    bs1_stub = {'address': f'0x{VT_BASE_STAT:08x}', 'argc': 1, 'ret': f'0x{vt1_ret_bits:08x}'}
    bs2_stub = {'address': f'0x{VT2_BASE_STAT:08x}', 'argc': 1, 'ret': f'0x{vt2_ret_bits:08x}'}
    bs_writes = []
    if mutate_pre_2b8_writes:
        # Overwrite state/counter fields written before vtable+0x2b8 to verify pre-call ordering.
        bs_writes.extend([
            {'addr': 'buf:counters+0x4c', 'bytes': u32_hex(0x00000099)},
            {'addr': 'buf:state+0x650', 'bytes': 'c3'},
            {'addr': 'buf:state+0x61c', 'bytes': i32_hex(4444)},
            {'addr': 'buf:state+0x100', 'bytes': i32_hex(5555)},
            {'addr': 'buf:state+0x630', 'bytes': f32_hex(123.5)},
        ])
    if switch_effects_in_base_stat:
        bs_writes.append({'addr': 'buf:self+0x1c8', 'pointer': post_2b8_eff_name})
    if bs_writes:
        if switch_vtable_in_d110 or switch_vtable_in_caf0:
            bs2_stub['writes'] = bs_writes
        else:
            bs1_stub['writes'] = bs_writes
    stubs.append(bs1_stub)
    if switch_vtable_in_caf0 or switch_vtable_in_d110:
        stubs.append(bs2_stub)

    qm_stub = {'address': f'0x{QUERY_EFFECT_MODE:08x}', 'argc': 1, 'ret': effect_mode_ret & 0xffffffff}
    qm_writes = []
    if mutate_stat668_in_query_mode:
        qm_writes.append({'addr': 'buf:state+0x668', 'bytes': i32_hex(7777)})
    if switch_effects_in_query_mode:
        qm_writes.append({'addr': 'buf:self+0x1c8', 'pointer': qm_eff_name})
    if qm_writes:
        qm_stub['writes'] = qm_writes
    stubs.append(qm_stub)

    if assoc_first_exists:
        a1_stub = {'address': f'0x{SYNC_ASSOCIATED:08x}', 'argc': 2, 'ret': 0}
        a1_writes = []
        if mutate_in_first_assoc:
            # Overwrite assoc1+0x28 (must be overwritten by -1.0f afterward), cap_0x604/effect_mode_0x66c
            # (must survive because written before 0x01be1860), and swap assoc_second.
            a1_writes.append({'addr': 'buf:assoc1+0x28', 'bytes': f32_hex(99.0)})
            a1_writes.append({'addr': 'buf:state+0x604', 'bytes': i32_hex(8888)})
            a1_writes.append({'addr': 'buf:state+0x66c', 'bytes': i32_hex(9999)})
            a1_writes.append({'addr': 'buf:state+0x678', 'pointer': 'assoc2_alt'})
        if switch_effects_in_first_assoc:
            a1_writes.append({'addr': 'buf:self+0x1c8', 'pointer': post_assoc_eff_name})
        if a1_writes:
            a1_stub['writes'] = a1_writes
        stubs.append(a1_stub)

        a2_stub = {'address': f'0x{SYNC_ASSOCIATED:08x}', 'argc': 2, 'ret': 0}
        if mutate_in_first_assoc:
            a2_stub['writes'] = [{'addr': 'buf:assoc2_alt+0x28', 'bytes': f32_hex(88.0)}]
        stubs.append(a2_stub)

    # Determine which effect list is walked by 0x01bec080
    if assoc_first_exists and switch_effects_in_first_assoc:
        final_nodes = post_assoc_effect_nodes
    elif assoc_first_exists and switch_effects_in_query_mode:
        final_nodes = query_mode_effect_nodes
    elif switch_effects_in_base_stat:
        final_nodes = post_2b8_effect_nodes
    elif switch_modules_and_effects_in_d110:
        final_nodes = alt_effect_nodes
    else:
        final_nodes = effect_nodes

    if mutate_next_in_first_node:
        # 3 nodes: node0 -> node1 -> node2, stub on node0 rewires node0->next to node2 (skips node1!).
        # Provide a 3rd spare stub entry so a prefetching mutant completes cleanly with 3 calls instead of erroring.
        assert len(final_nodes) == 3
        stubs.append({
            'address': f'0x{PROCESS_EFFECT_NODE:08x}',
            'argc': 2,
            'ret': 0,
            'writes': [{'addr': 'buf:eff_n0+0x58', 'pointer': 'eff_n2'}],
        })
        stubs.append({'address': f'0x{PROCESS_EFFECT_NODE:08x}', 'argc': 2, 'ret': 0})
        stubs.append({'address': f'0x{PROCESS_EFFECT_NODE:08x}', 'argc': 2, 'ret': 0})
    else:
        for _ in final_nodes:
            stubs.append({'address': f'0x{PROCESS_EFFECT_NODE:08x}', 'argc': 2, 'ret': 0})

    args = {'rdi': 'buf:self'}
    if unused_regs:
        args['rsi'] = 0x1122334455667788
        args['rdx'] = 0x99aabbccddeeff00
        args['xmm0'] = '2301c07f'

    return {
        'schema': 1,
        'address': '0x01cf7880',
        'id': cid,
        'returns': 'void',
        'args': args,
        'buffers': bufs,
        'memory': mem,
        'stubs': stubs,
    }


CASES = [
    # --- Early exit 1: vtable+0x1c8 returns null ---
    case('state_data_null_early_return', state_exists=False, points_vals=(999, 999, 999),
         gate_flags=0x01, init_state_flags=0xaa),

    # --- Points value clamping against OLD state limits & limit copy order ---
    case('points_clamped_by_old_state_limits_not_new_points_limits',
         points_vals=(500, 400, 300), points_limits=(1000, 900, 800), state_init_limits=(120, 60, 90)),
    case('points_all_below_old_limits_unclamped',
         points_vals=(10, 20, 30), points_limits=(5, 6, 7), state_init_limits=(100, 200, 300)),
    case('points_equal_old_limits_boundary',
         points_vals=(120, 60, 90), points_limits=(121, 61, 91), state_init_limits=(120, 60, 90)),
    case('points_only_pair0_clamped',
         points_vals=(121, 59, 89), points_limits=(500, 500, 500), state_init_limits=(120, 60, 90)),
    case('points_only_pair1_clamped',
         points_vals=(119, 61, 89), points_limits=(500, 500, 500), state_init_limits=(120, 60, 90)),
    case('points_only_pair2_clamped',
         points_vals=(119, 59, 91), points_limits=(500, 500, 500), state_init_limits=(120, 60, 90)),
    case('points_signed_negative_old_limit_vs_positive_val',
         points_vals=(5, 10, 15), points_limits=(100, 200, 300), state_init_limits=(-10, -20, -30)),
    case('points_signed_positive_old_limit_vs_negative_val',
         points_vals=(-10, -20, -30), points_limits=(100, 200, 300), state_init_limits=(5, 10, 15)),
    case('points_signed_int32_min_max_boundaries',
         points_vals=(-0x80000000, 0x7fffffff, -1),
         points_limits=(0x7fffffff, -0x80000000, 0),
         state_init_limits=(0x7fffffff, -0x80000000, -2)),

    # --- EventGate bit 0 -> state+0x650 bit 4 ---
    case('gate_null_clears_bit4_preserves_high_bits',
         gate_exists=False, init_state_flags=0xf5, counter_flags=0x05),
    case('gate_bit0_clear_other_bits_set_clears_bit4',
         gate_exists=True, gate_flags=0xfe, init_state_flags=0xfa, counter_flags=0x0a),
    case('gate_bit0_set_sets_bit4_preserves_high_bits',
         gate_exists=True, gate_flags=0x01, init_state_flags=0xa0, counter_flags=0x00),
    case('gate_bit0_and_high_bits_set',
         gate_exists=True, gate_flags=0x81, init_state_flags=0x60, counter_flags=0x00),

    # --- Early exit 2: vtable+0x1c0 byte-wide ABI & vtable reload ---
    case('skip_sync_al_01_returns_after_caf0',
         skip_sync_ret=0x01, gate_flags=0x01, init_state_flags=0xaa,
         points_vals=(500, 400, 300), state_init_limits=(50, 40, 30), points_limits=(600, 500, 400),
         counter_flags=0x0f),
    case('pre_caf0_writes_survive_caf0_mutations',
         skip_sync_ret=0x01, gate_flags=0x01, init_state_flags=0xaa,
         points_vals=(500, 400, 300), state_init_limits=(50, 40, 30), points_limits=(600, 500, 400),
         mutate_state_in_caf0=True),
    case('skip_sync_al_02_bit0_clear_still_exits',
         skip_sync_ret=0x02, counter_flags=0x0f),
    case('skip_sync_al_80_sign_bit_exits',
         skip_sync_ret=0x80, counter_flags=0x0f),
    case('skip_sync_0x0100_low_byte_zero_continues',
         skip_sync_ret=0x00000100, counter_flags=0x03),
    case('skip_sync_upper32_only_continues',
         skip_sync_ret=0x0000000100000000, counter_flags=0x0c),
    case('caf0_switches_vtable_to_skip_sync',
         switch_vtable_in_caf0=True, skip_sync_ret=0x00, vt2_skip_sync_ret=0x01, counter_flags=0x0f),
    case('caf0_switches_vtable_to_continue_sync',
         switch_vtable_in_caf0=True, skip_sync_ret=0x01, vt2_skip_sync_ret=0x00,
         vt2_base_stat=7.5, counter_flags=0x05),

    # --- Post-call reloads after 0x01cfd110 and vtable+0x2b8 ---
    case('d110_switches_modules_and_effects_reloaded',
         counters=(1, 2, 3, 4, 5), maxima=(10, 20, 30, 40, 50), counter_flags=0x01,
         effects_mask=0x00000000,
         switch_modules_and_effects_in_d110=True,
         alt_counters=(101, 202, 303, 404, 505), alt_maxima=(601, 602, 603, 604, 605),
         alt_counter_flags=0x0a, alt_effects_mask=0x00000e00,
         alt_effect_nodes=({'type': 2, 'val_a': 5.0, 'val_b': 12.0}, {'type': 0x9b, 'bonus': 7})),
    case('d110_switches_vtable_for_base_stat_2b8',
         switch_vtable_in_d110=True, base_stat=1.0, vt2_base_stat=99.75),
    case('base_stat_2b8_switches_effects_for_9b_query_and_loop',
         effects_mask=0x00000e00,
         effect_nodes=({'type': 2, 'val_a': 3.5, 'val_b': 8.5}, {'type': 0x9b, 'bonus': 100}),
         base_stat=10.0, switch_effects_in_base_stat=True,
         post_2b8_effects_mask=0x00000e00,
         post_2b8_effect_nodes=({'type': 0x9b, 'bonus': -4}, {'type': 0x9b, 'bonus': 9})),

    # --- Effect list: empty list, zero mask, mask bit coverage, inactive flags, null param ---
    case('effects_all_masks_empty_list_defaults_neg1',
         effects_mask=0xffffffff, effect_nodes=(), counters=(11, 22, 33, 44, 55)),
    case('effects_zero_mask_skips_all_scans_keeps_counters',
         effects_mask=0x00000000,
         effect_nodes=(
             {'type': 0x88, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 0x75, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 0x74, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 2, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 5, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 6, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 0x104, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 0x9b, 'bonus': 50},
         ),
         counters=(11, 22, 33, 44, 55)),

    # Special effect group (0x88 / 0x75 / 0x74 -> 999.0f, 1000.0f, zeroes c3)
    case('special_0x88_mask_0x0040', effects_mask=0x00000040, effect_nodes=({'type': 0x88},)),
    case('special_0x88_mask_0x0080', effects_mask=0x00000080, effect_nodes=({'type': 0x88},)),
    case('special_0x88_mask_0x0100', effects_mask=0x00000100, effect_nodes=({'type': 0x88},)),
    case('special_0x88_wrong_mask_not_matched', effects_mask=0xfffffe3f, effect_nodes=({'type': 0x88},)),
    case('special_0x75_mask_0x0008', effects_mask=0x00000008, effect_nodes=({'type': 0x75},)),
    case('special_0x75_mask_0x0010', effects_mask=0x00000010, effect_nodes=({'type': 0x75},)),
    case('special_0x75_mask_0x0020', effects_mask=0x00000020, effect_nodes=({'type': 0x75},)),
    case('special_0x75_wrong_mask_not_matched', effects_mask=0xffffffc7, effect_nodes=({'type': 0x75},)),
    case('special_0x74_mask_0x08000000', effects_mask=0x08000000, effect_nodes=({'type': 0x74},)),
    case('special_0x74_mask_0x10000000', effects_mask=0x10000000, effect_nodes=({'type': 0x74},)),
    case('special_0x74_mask_0x20000000', effects_mask=0x20000000, effect_nodes=({'type': 0x74},)),
    case('special_0x74_wrong_mask_not_matched', effects_mask=0xc7ffffff, effect_nodes=({'type': 0x74},)),
    case('special_fallthrough_0x88_inactive_to_0x74',
         effects_mask=0x380001f8,
         effect_nodes=({'type': 0x88, 'flags': 0x00000001}, {'has_param': False}, {'type': 0x74})),

    # Type 2 / 5 / 6 / 0x104 mask bits and individual counter zeroing
    case('type2_mask_0x0200_zeroes_c0', effects_mask=0x00000200,
         effect_nodes=({'type': 2, 'val_a': 4.25, 'val_b': 10.5},)),
    case('type2_mask_0x0400_zeroes_c0', effects_mask=0x00000400,
         effect_nodes=({'type': 2, 'val_a': 1.5, 'val_b': 9.0},)),
    case('type2_mask_0x0800_zeroes_c0', effects_mask=0x00000800,
         effect_nodes=({'type': 2, 'val_a': 0.25, 'val_b': 3.0},)),
    case('type2_wrong_mask_ignored', effects_mask=0xfffff1ff,
         effect_nodes=({'type': 2, 'val_a': 4.25, 'val_b': 10.5},)),

    case('type5_mask_0x00040000_zeroes_c1', effects_mask=0x00040000,
         effect_nodes=({'type': 5, 'val_a': 2.5, 'val_b': 15.0},)),
    case('type5_mask_0x00080000_zeroes_c1', effects_mask=0x00080000,
         effect_nodes=({'type': 5, 'val_a': 3.5, 'val_b': 16.0},)),
    case('type5_mask_0x00100000_zeroes_c1', effects_mask=0x00100000,
         effect_nodes=({'type': 5, 'val_a': 4.5, 'val_b': 17.0},)),
    case('type5_wrong_mask_ignored', effects_mask=0xffe3ffff,
         effect_nodes=({'type': 5, 'val_a': 2.5, 'val_b': 15.0},)),

    case('type6_mask_0x00200000_zeroes_c2', effects_mask=0x00200000,
         effect_nodes=({'type': 6, 'val_a': 6.25, 'val_b': 20.0},)),
    case('type6_mask_0x00400000_zeroes_c2', effects_mask=0x00400000,
         effect_nodes=({'type': 6, 'val_a': 7.25, 'val_b': 21.0},)),
    case('type6_mask_0x00800000_zeroes_c2', effects_mask=0x00800000,
         effect_nodes=({'type': 6, 'val_a': 8.25, 'val_b': 22.0},)),
    case('type6_wrong_mask_ignored', effects_mask=0xff1fffff,
         effect_nodes=({'type': 6, 'val_a': 6.25, 'val_b': 20.0},)),

    case('type104_mask_0x08000000_zeroes_c4_only', effects_mask=0x08000000,
         effect_nodes=({'type': 0x104, 'val_a': 1.0, 'val_b': 50.0},)),
    case('type104_mask_0x10000000_zeroes_c4_only', effects_mask=0x10000000,
         effect_nodes=({'type': 0x104, 'val_a': 0.5, 'val_b': 50.0},)),
    case('type104_mask_0x20000000_zeroes_c4_only', effects_mask=0x20000000,
         effect_nodes=({'type': 0x104, 'val_a': 99.0, 'val_b': 50.0},)),
    case('type104_wrong_mask_ignored', effects_mask=0xc7ffffff,
         effect_nodes=({'type': 0x104, 'val_a': 99.0, 'val_b': 50.0},)),

    # Inactive flag bits: every bit of 0x800c0003 skips, complement 0x7ff3fffc stays active
    case('inactive_flags_each_bit_skips_and_complement_matches',
         effects_mask=0xffffffff,
         effect_nodes=(
             {'type': 2, 'flags': 0x00000001, 'val_a': 10.0, 'val_b': 10.0},
             {'type': 2, 'flags': 0x00000002, 'val_a': 11.0, 'val_b': 11.0},
             {'type': 2, 'flags': 0x00040000, 'val_a': 12.0, 'val_b': 12.0},
             {'type': 2, 'flags': 0x00080000, 'val_a': 13.0, 'val_b': 13.0},
             {'type': 2, 'flags': 0x80000000, 'val_a': 14.0, 'val_b': 14.0},
             {'has_param': False, 'flags': 0x00000000, 'val_a': 15.0, 'val_b': 15.0},
             {'type': 2, 'flags': 0x7ff3fffc, 'val_a': 2.25, 'val_b': 3.75},
             {'type': 2, 'flags': 0x00000000, 'val_a': 99.0, 'val_b': 99.0},
         )),
    case('inactive_flags_all_other_types_first_active_wins',
         effects_mask=0xffffffff,
         effect_nodes=(
             {'type': 5, 'flags': 0x00040000, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 5, 'flags': 0x7ff3fffc, 'val_a': 5.5, 'val_b': 6.5},
             {'type': 5, 'flags': 0x00000000, 'val_a': 9.5, 'val_b': 10.5},
             {'type': 6, 'flags': 0x80000000, 'val_a': 1.0, 'val_b': 2.0},
             {'type': 6, 'flags': 0x7ff3fffc, 'val_a': 7.5, 'val_b': 8.5},
             {'type': 6, 'flags': 0x00000000, 'val_a': 11.5, 'val_b': 12.5},
             {'type': 0x104, 'flags': 0x00080000, 'val_a': 5.0},
             {'type': 0x104, 'flags': 0x7ff3fffc, 'val_a': -0.5},
             {'type': 0x104, 'flags': 0x00000000, 'val_a': 5.0},
         )),

    # --- Float zeroing boundaries (`vucomiss val, 0.0f; cmova`) and bit preservation ---
    case('float_zero_pos_and_neg_zero_do_not_zero_counters',
         effects_mask=0xffffffff,
         effect_nodes=(
             {'type': 2, 'val_a_bits': 0x00000000, 'val_b_bits': 0x80000000},
             {'type': 5, 'val_a_bits': 0x80000000, 'val_b_bits': 0x00000000},
             {'type': 6, 'val_a_bits': 0x80000001, 'val_b_bits': 0x7fc0abcd},
             {'type': 0x104, 'val_a_bits': 0x80000000},
         )),
    case('float_positive_subnormal_and_inf_zero_counters',
         effects_mask=0xffffffff,
         effect_nodes=(
             {'type': 2, 'val_a_bits': 0x00000001, 'val_b_bits': 0x7f800000},
             {'type': 5, 'val_a_bits': 0x7f800000, 'val_b_bits': 0xff800000},
             {'type': 6, 'val_a_bits': 0x00000001, 'val_b_bits': 0x00000001},
             {'type': 0x104, 'val_a_bits': 0x00000001},
             {'type': 0x88},
         )),
    case('float_nan_and_neg_inf_do_not_zero_and_preserve_payload',
         effects_mask=0xffffffff,
         effect_nodes=(
             {'type': 2, 'val_a_bits': 0x7fc01234, 'val_b_bits': 0xffc05678},
             {'type': 5, 'val_a_bits': 0xff800000, 'val_b_bits': 0x7fc0dead},
             {'type': 6, 'val_a_bits': 0x7fc0beef, 'val_b_bits': 0x80000000},
             {'type': 0x104, 'val_a_bits': 0x7fc04321},
         )),
    case('float_val_a_neg_val_b_pos_does_not_zero_counters',
         effects_mask=0xffffffff,
         effect_nodes=(
             {'type': 2, 'val_a': -0.5, 'val_b': 10.0},
             {'type': 5, 'val_a': -0.25, 'val_b': 20.0},
             {'type': 6, 'val_a': -1.0, 'val_b': 30.0},
             {'type': 0x104, 'val_a': -0.5, 'val_b': 40.0},
         )),
    case('float_val_a_pos_val_b_neg_zeroes_counters',
         effects_mask=0xffffffff,
         effect_nodes=(
             {'type': 2, 'val_a': 0.5, 'val_b': -10.0},
             {'type': 5, 'val_a': 0.25, 'val_b': -20.0},
             {'type': 6, 'val_a': 1.0, 'val_b': -30.0},
             {'type': 0x104, 'val_a': 0.5, 'val_b': -40.0},
         )),
    case('pre_2b8_and_pre_query_mode_writes_ordering',
         effects_mask=0x00000200,
         effect_nodes=({'type': 2, 'val_a': 2.0, 'val_b': 3.0}, {'type': 0x9b, 'bonus': 5}),
         base_stat=10.0, counter_flags=0x0f,
         mutate_pre_2b8_writes=True, mutate_stat668_in_query_mode=True),

    # --- CounterModule flags_0x4c, counter swap (+0x104/+0x108), maxima ---
    case('counter_flags_upper_bits_cleared_low_nibble_copied',
         init_state_flags=0xe0, gate_flags=0x00,
         counters=(101, 202, 303, 404, 505), maxima=(601, 602, 603, 604, 605),
         counter_flags=0xdeadbeef),
    case('counter_flags_bit0_only_preserves_gate_and_high_bits',
         init_state_flags=0xaf, gate_flags=0x01, counter_flags=0x00000001),
    case('counter_flags_bit1_only',
         init_state_flags=0xaf, gate_flags=0x00, counter_flags=0x00000002),
    case('counter_flags_bit2_only',
         init_state_flags=0xaf, gate_flags=0x01, counter_flags=0x00000004),
    case('counter_flags_bit3_only',
         init_state_flags=0xaf, gate_flags=0x00, counter_flags=0x00000008),
    case('counter_flags_bit4_does_not_leak_to_gate_bit',
         init_state_flags=0xa0, gate_flags=0x00, counter_flags=0x000000f0),

    # --- Type 0x9b bonus accumulation, signed int16, FP order, truncation, 0x01979450 ---
    case('bonus_9b_sums_multiple_signed_skips_inactive_and_null_param',
         effects_mask=0x00000200, base_stat=100.5,
         effect_nodes=(
             {'type': 0x9b, 'bonus': -5},
             {'type': 0x9b, 'flags': 0x00000001, 'bonus': 500},
             {'type': 0x9b, 'flags': 0x00000002, 'bonus': 510},
             {'type': 0x9b, 'flags': 0x00040000, 'bonus': 520},
             {'type': 0x9b, 'flags': 0x00080000, 'bonus': 600},
             {'type': 0x9b, 'flags': 0x80000000, 'bonus': 610},
             {'has_param': False},
             {'type': 0x9a, 'bonus': 700},
             {'type': 0x9b, 'bonus': 32767},
             {'type': 0x9b, 'bonus': -32768},
             {'type': 0x9b, 'flags': 0x7ff3fffc, 'bonus': 12},
         ),
         effect_mode_ret=6),
    case('bonus_9b_mask_0x0400_and_0x0800',
         effects_mask=0x00000800, base_stat=-10.25,
         effect_nodes=({'type': 0x9b, 'bonus': -3}, {'type': 0x9b, 'bonus': 1}),
         effect_mode_ret=2),
    case('bonus_9b_wrong_mask_skips_sum',
         effects_mask=0xfffff1ff, base_stat=12.75,
         effect_nodes=({'type': 0x9b, 'bonus': 50},)),
    case('bonus_9b_fp_accumulation_order_2pow24_plus_two_ones',
         effects_mask=0x00000400, base_stat=16777216.0,
         effect_nodes=({'type': 0x9b, 'bonus': 1}, {'type': 0x9b, 'bonus': 1})),
    case('bonus_9b_truncation_negative_fraction_toward_zero',
         effects_mask=0x00000000, base_stat=-3.9),
    case('bonus_9b_truncation_nan_gives_int32_min',
         effects_mask=0x00000200, base_stat_bits=0x7fc01234,
         effect_nodes=({'type': 0x9b, 'bonus': 5},)),
    case('bonus_9b_truncation_pos_inf_gives_int32_min',
         effects_mask=0x00000000, base_stat_bits=0x7f800000),
    case('bonus_9b_truncation_neg_inf_gives_int32_min',
         effects_mask=0x00000000, base_stat_bits=0xff800000),
    case('query_effect_mode_negative_32bit_return',
         effect_mode_ret=-7),

    # --- Associated objects (+0x670/+0x678) & effect node loop (0x01bec080) ---
    case('assoc_first_null_skips_both_calls_and_skips_effects_reload',
         assoc_first_exists=False, assoc_second_exists=True,
         effects_mask=0x00000000,
         effect_nodes=({'type': 2, 'flags': 0x800c0003}, {'has_param': False}),
         switch_effects_in_query_mode=True,
         query_mode_effects_mask=0x00000000,
         query_mode_effect_nodes=({'type': 5},)),
    case('assoc_both_present_reloads_second_and_reloads_effects_after',
         assoc_first_exists=True, assoc_second_exists=True,
         effects_mask=0x00000000,
         effect_nodes=({'type': 2},),
         mutate_in_first_assoc=True,
         switch_effects_in_first_assoc=True,
         post_assoc_effects_mask=0x00000000,
         post_assoc_effect_nodes=({'type': 5}, {'type': 6})),
    case('assoc_both_present_reloads_effects_mutated_in_query_mode',
         assoc_first_exists=True, assoc_second_exists=True,
         effects_mask=0x00000000,
         effect_nodes=({'type': 2},),
         switch_effects_in_query_mode=True,
         query_mode_effects_mask=0x00000000,
         query_mode_effect_nodes=({'type': 5}, {'type': 6})),
    case('node_loop_processes_all_nodes_and_reloads_next_after_call',
         effects_mask=0x00000000,
         effect_nodes=(
             {'type': 2, 'flags': 0x800c0003},
             {'has_param': False},
             {'type': 5, 'flags': 0x00000000},
         ),
         mutate_next_in_first_node=True),

    # --- Unused argument register independence ---
    case('unused_rsi_rdx_xmm0_ignored',
         unused_regs=True, points_vals=(150, 75, 110), state_init_limits=(100, 100, 100),
         effects_mask=0x00000e00, effect_nodes=({'type': 2, 'val_a': 1.0, 'val_b': 2.0},)),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
