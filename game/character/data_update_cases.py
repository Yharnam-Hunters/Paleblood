#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for character_data_update_01913c10 (0x01913c10), for tools/verify.py run.

usage: data_update_cases.py OUT_DIR

Every input sits in a named buffer; virtual methods on ChrData, ChrIns, DrawTarget,
fallback DrawTarget, and MotionReceiver point at spare guest addresses that the cases stub.
Covers:
- anim_events null / unready at entry and mutated to unready / null / ready during target update;
- primary target vs fallback target selection, null target before apply_step (+0xb8) and before
  target update (+0x48 / +0x40), and mode_flags & 7 == 7 vs visible() target update dispatch;
- hidden target with hp <= 0 (0, -1, INT32_MIN) skipping step prep vs hp == 1 entering it;
- visible yaw quantization across all three degree thresholds (30, 60, 100), exact 100.0 and
  1-ULP boundaries around 30/60/100, positive/negative yaw, negative turn_rate_scale, NaN, and
  0 * inf, while preserving x/z/w bits;
- motion_a_result (+0x1d9) and motion_b_result (+0x28a) gating: flags_0x1bc bit 15 vs other bits,
  motion_b_enabled bit 0 only (0x02 vs 0x03), up-to-three linked() calls (null linked, null source,
  state 0, state 9, state 10, third-call null source), gate_module + allow_motion_b (+0xc0),
  motion_blocked (+0x111), params->motion_mode (0xff vs 0xfe), mode_flags (& 7), visible override,
  and throw_state (null, active, inactive);
- freeze path via points->flags & 0x40 and WorldChrManDbg (missing singleton report, debug flag
  with exempt_a / exempt_b / triggered), keeping quantized_rotation intact and checking owner
  flags_0x359 & 0x20 for orientation reset;
- extra_rotation (+0x80) NaN-payload operand ordering in add_in_order(extra, rotation_delta);
- 3-row basis transform (mulps + double haddps) with asymmetric non-unit values, w preservation,
  distinct NaN-payload operand ordering, and receiver apply_motion (+0x30) in-place mutation of
  world_motion stored to self->motion_delta (+0x100);
- horizontal XZ projection (owner +0x4d8): normal 3D->XZ magnitude scaling, exact FLT_EPSILON and
  nextafter boundaries, NaN rx taking vector_zero (0x05940550), infinite XZ length taking
  vector_nan (0x05940540) with distinct payloads and ry-NaN ordering, and finite XZ with infinite
  or NaN ry verifying w = 0.0f * xyz_len;
- store ordering of motion_a_result (+0x1d9) and motion_b_result (+0x28a) before apply_step via
  argmem snapshots on self, and final active_target(self) re-read at params_post_update after a
  primary-to-fallback switch inside motion_receiver (+0x48).

Explicit coverage limits:
- The original's retained xz_len == 0.0f normalization fallback is unreachable in IEEE-754 under
  the stock positive horizontal_length_epsilon (0x34000000).
- tools/harness.py loads stub vector returns via `movq xmm0, rax`, constraining scripted
  extra_rotation (+0x80) upper lanes (z, w) to 0.0f; rotation_delta tests all 4 lanes.
"""
import json
import os
import struct
import sys

GUARD_GOT = '0x057e3be0'
WORLD_CHR_MAN_DBG = '0x0593e880'
DEBUG_FREEZE_MOTION = '0x0593e894'
VECTOR_NAN = '0x05940540'
VECTOR_ZERO = '0x05940550'

FATAL_ERROR = 0x024b55b0
PARAMS_POST_UPDATE = 0x0190a5f0

# Spare guest code addresses used as distinct virtual-method stub targets.
TARGET_UPDATE_INACTIVE = 0x00500000
TARGET_UPDATE_ACTIVE = 0x00500100
TARGET_ALLOW_B = 0x00500200
TARGET_VISIBLE = 0x00500300

DATA_RECEIVER = 0x00500400
DATA_EXTRA_ROT = 0x00500500
DATA_APPLY_STEP = 0x00500600
DATA_TURN_SCALE = 0x00500700

OWNER_LINKED = 0x00500800
OWNER_EXEMPT_B = 0x00500900
OWNER_EXEMPT_A = 0x00500a00
OWNER_PROJECT_XZ = 0x00500b00

RECV_APPLY_MOTION = 0x00500c00

FALLBACK_UPDATE_INACTIVE = 0x00500d00
FALLBACK_UPDATE_ACTIVE = 0x00500e00
FALLBACK_ALLOW_B = 0x00500f00
FALLBACK_VISIBLE = 0x00501000

# Offsets inside the combined `self` buffer so target->update_active(target, self, dt)
# can mutate anim_events or modules->anim_events through arg 1 when testing re-reads.
SELF_ANIM_OFF = 0x300
SELF_MODULES_OFF = 0x700

DEFAULT_MOTION = (1.25, -2.5, 3.75, -4.5)
DEFAULT_ROT = (0.125, 0.25, -0.375, 0.5)
DEFAULT_ORIENT = (0.1, 0.2, 0.3, 1.0)
DEFAULT_STATE_ORIENT = (-0.5, 0.625, -0.75, 0.875)
DEFAULT_LOCAL_MOTION = (1.5, -2.25, 3.5, 0.75)
DEFAULT_ROW_X = (1.0, 0.0, 0.0, 0.0)
DEFAULT_ROW_Y = (0.0, 1.0, 0.0, 0.0)
DEFAULT_ROW_Z = (0.0, 0.0, 1.0, 0.0)
DEFAULT_EXTRA_ROT = (0.5, -0.25)

# Non-zero bit patterns for vector_zero / vector_nan globals so fixtures prove the
# replacement loads from 0x05940550 / 0x05940540 rather than hardcoding literals.
SENTINEL_VEC_ZERO = (0x3e800000, 0xbf000000, 0x3f400000, 0xc0000000)
SENTINEL_VEC_NAN = (0x7fc00101, 0x7fc00202, 0x7fc00303, 0x7fc00404)


def f32_hex(v):
    if isinstance(v, int):
        return struct.pack('<I', v & 0xffffffff).hex()
    return struct.pack('<f', v).hex()


def vec4_hex(vals):
    return ''.join(f32_hex(v) for v in vals)


def u32_hex(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def i32_hex(v):
    return struct.pack('<i', v).hex()


def u64_hex(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def pair_f32_ret(x, y):
    lo = x if isinstance(x, int) else struct.unpack('<I', struct.pack('<f', x))[0]
    hi = y if isinstance(y, int) else struct.unpack('<I', struct.pack('<f', y))[0]
    return f'0x{((hi & 0xffffffff) << 32) | (lo & 0xffffffff):016x}'


def case(
    cid,
    *,
    dt=1.0 / 30.0,
    anim_exists=True,
    anim_ready=1,
    has_primary=True,
    has_fallback=True,
    visible1=1,
    visible2=1,
    visible3=1,
    hp=100,
    turn_scale=10,
    rot_delta=DEFAULT_ROT,
    motion_delta=DEFAULT_MOTION,
    target_orient=DEFAULT_ORIENT,
    state_orient=DEFAULT_STATE_ORIENT,
    flags_1bc=0,
    motion_b_enabled=1,
    linked_seq=(),
    gate_active=0,
    allow_motion_b_ret=1,
    motion_blocked=0,
    params_mode=0xff,
    mode_flags=0x07,
    throw_state_exists=True,
    throw_active=0,
    points_flags=0,
    world_dbg_present=True,
    debug_freeze=0,
    exempt_a=0,
    exempt_b=0,
    owner_flags_359=0,
    extra_rot=DEFAULT_EXTRA_ROT,
    clear_targets_in_extra=False,
    clear_anim_ready_in_apply_step=False,
    snapshot_self_flags_in_apply_step=False,
    mutate_anim_in_target_update=None,
    clear_primary_in_target_update=False,
    clear_primary_in_motion_receiver=False,
    local_motion=DEFAULT_LOCAL_MOTION,
    row_x=DEFAULT_ROW_X,
    row_y=DEFAULT_ROW_Y,
    row_z=DEFAULT_ROW_Z,
    project_xz=0,
    receiver_writes_vec=None,
    vec_zero_val=SENTINEL_VEC_ZERO,
    vec_nan_val=SENTINEL_VEC_NAN,
    init_flag_1d9=0xaa,
    init_flag_28a=0x55,
):
    """Build one synthetic schema-1 differential test case for 0x01913c10.

    linked_seq: sequence of linked() stub outcomes. Empty means first call returns null.
    Each element is either None (returns null) or ('linked_null_src',) or ('state', byte_val).
    mutate_anim_in_target_update: None, 'unready', 'null', or 'ready'.
    """
    b = {
        'guard': 16,
        'self': 0x800,
        'self_vt': 0xe8,
        'owner': 0x3c0,
        'owner_vt': 0x4e0,
        'params': 0x70,
        'flags_mod': 0x1c0,
        'points': 0x208,
        'gate_mod': 0x20,
        'state_mod': 0x1e0,
        'throw_req': 0x30,
        'recv': 0x10,
        'recv_vt': 0x38,
    }
    m = [
        (GUARD_GOT, None, 'p', 'guard'),
        ('guard', 0, u64_hex(0xdeadbeefcafebabe)),
        (WORLD_CHR_MAN_DBG, None, u64_hex(0x1122334455667788 if world_dbg_present else 0)),
        (DEBUG_FREEZE_MOTION, None, f'{debug_freeze:02x}'),
        (VECTOR_ZERO, None, vec4_hex(vec_zero_val)),
        (VECTOR_NAN, None, vec4_hex(vec_nan_val)),
        ('self', 0x000, 'p', 'self_vt'),
        ('self', 0x008, 'p', 'owner'),
        ('self', 0x038, 'p', 'params'),
        ('self', 0x0c0, vec4_hex(row_x)),
        ('self', 0x0d0, vec4_hex(row_y)),
        ('self', 0x0e0, vec4_hex(row_z)),
        ('self', 0x100, vec4_hex(motion_delta)),
        ('self', 0x110, f'{mode_flags:02x}'),
        ('self', 0x111, f'{motion_blocked:02x}'),
        ('self', 0x1d8, f'{motion_b_enabled:02x}'),
        ('self', 0x1d9, f'{init_flag_1d9:02x}'),
        ('self', 0x28a, f'{init_flag_28a:02x}'),
        ('self_vt', 0x48, 'g', DATA_RECEIVER),
        ('self_vt', 0x80, 'g', DATA_EXTRA_ROT),
        ('self_vt', 0xb8, 'g', DATA_APPLY_STEP),
        ('self_vt', 0xe0, 'g', DATA_TURN_SCALE),
        ('owner', 0x000, 'p', 'owner_vt'),
        ('owner', 0x359, f'{owner_flags_359:02x}'),
        ('owner', 0x3b0, 'p', f'self+{SELF_MODULES_OFF}'),
        ('owner_vt', 0x168, 'g', OWNER_LINKED),
        ('owner_vt', 0x1b0, 'g', OWNER_EXEMPT_B),
        ('owner_vt', 0x1b8, 'g', OWNER_EXEMPT_A),
        ('owner_vt', 0x4d8, 'g', OWNER_PROJECT_XZ),
        ('self', SELF_MODULES_OFF + 0x08, 'p', 'flags_mod'),
        ('self', SELF_MODULES_OFF + 0x20, 'p', 'points'),
        ('self', SELF_MODULES_OFF + 0x50, 'p', 'gate_mod'),
        ('self', SELF_MODULES_OFF + 0x68, 'p', 'state_mod'),
        ('self', SELF_MODULES_OFF + 0x88, 'p', 'throw_req'),
        ('flags_mod', 0x1bc, u32_hex(flags_1bc)),
        ('points', 0xf8, i32_hex(hp)),
        ('points', 0x200, f'{points_flags:02x}'),
        ('gate_mod', 0x10, f'{gate_active:02x}'),
        ('state_mod', 0x1d0, vec4_hex(state_orient)),
        ('params', 0x60, f'{params_mode:02x}'),
        ('recv', 0x00, 'p', 'recv_vt'),
        ('recv_vt', 0x30, 'g', RECV_APPLY_MOTION),
    ]

    if anim_exists:
        m.append(('self', SELF_MODULES_OFF + 0x30, 'p', f'self+{SELF_ANIM_OFF}'))
        m.append(('self', SELF_ANIM_OFF + 0x384, f'{anim_ready:02x}'))

    if throw_state_exists:
        b['throw_state'] = 0x28
        m.append(('throw_req', 0x28, 'p', 'throw_state'))
        m.append(('throw_state', 0x20, f'{throw_active:02x}'))

    if has_primary:
        b['target'] = 0x40
        b['target_vt'] = 0xd0
        m += [
            ('self', 0x280, 'p', 'target'),
            ('target', 0x00, 'p', 'target_vt'),
            ('target', 0x10, vec4_hex(local_motion)),
            ('target', 0x20, vec4_hex(rot_delta)),
            ('target', 0x30, vec4_hex(target_orient)),
            ('target_vt', 0x40, 'g', TARGET_UPDATE_INACTIVE),
            ('target_vt', 0x48, 'g', TARGET_UPDATE_ACTIVE),
            ('target_vt', 0xc0, 'g', TARGET_ALLOW_B),
            ('target_vt', 0xc8, 'g', TARGET_VISIBLE),
        ]

    if has_fallback:
        b['fallback'] = 0x40
        b['fallback_vt'] = 0xd0
        # Distinct values on fallback when primary also exists, so choosing the wrong target fails.
        fb_local = (9.0, -8.0, 7.0, -6.0) if has_primary else local_motion
        fb_rot = (4.0, 5.0, 6.0, 7.0) if has_primary else rot_delta
        fb_orient = (8.0, 9.0, 10.0, 11.0) if has_primary else target_orient
        m += [
            ('self', 0x80, 'p', 'fallback'),
            ('fallback', 0x00, 'p', 'fallback_vt'),
            ('fallback', 0x10, vec4_hex(fb_local)),
            ('fallback', 0x20, vec4_hex(fb_rot)),
            ('fallback', 0x30, vec4_hex(fb_orient)),
            ('fallback_vt', 0x40, 'g', FALLBACK_UPDATE_INACTIVE),
            ('fallback_vt', 0x48, 'g', FALLBACK_UPDATE_ACTIVE),
            ('fallback_vt', 0xc0, 'g', FALLBACK_ALLOW_B),
            ('fallback_vt', 0xc8, 'g', FALLBACK_VISIBLE),
        ]

    use_primary = has_primary
    vis_addr = TARGET_VISIBLE if use_primary else FALLBACK_VISIBLE
    allow_b_addr = TARGET_ALLOW_B if use_primary else FALLBACK_ALLOW_B
    upd_active_addr = TARGET_UPDATE_ACTIVE if use_primary else FALLBACK_UPDATE_ACTIVE
    upd_inactive_addr = TARGET_UPDATE_INACTIVE if use_primary else FALLBACK_UPDATE_INACTIVE

    stubs = []
    entry_ready = anim_exists and anim_ready != 0
    post_anim_exists = anim_exists
    post_anim_ready = anim_ready
    targets_present = has_primary or has_fallback

    if entry_ready:
        stubs.append({'address': f'0x{vis_addr:08x}', 'argc': 1, 'ret': f'0x{visible1:x}'})
        if visible1 != 0 or hp > 0:
            if visible1 != 0:
                stubs.append({'address': f'0x{DATA_TURN_SCALE:08x}', 'argc': 1,
                              'ret': f'0x{turn_scale & 0xffffffff:x}'})
            if not linked_seq:
                stubs.append({'address': f'0x{OWNER_LINKED:08x}', 'argc': 1, 'ret': '0x0'})
            else:
                for idx, item in enumerate(linked_seq):
                    if item is None:
                        stubs.append({'address': f'0x{OWNER_LINKED:08x}', 'argc': 1, 'ret': '0x0'})
                    elif item[0] == 'linked_null_src':
                        lname = f'linked_{idx}'
                        b[lname] = 0x10
                        stubs.append({'address': f'0x{OWNER_LINKED:08x}', 'argc': 1, 'ret': f'buf:{lname}'})
                    else:
                        lname = f'linked_{idx}'
                        sname = f'lsrc_{idx}'
                        b[lname] = 0x10
                        b[sname] = 0x138
                        m.append((lname, 0x08, 'p', sname))
                        m.append((sname, 0x130, f'{item[1]:02x}'))
                        stubs.append({'address': f'0x{OWNER_LINKED:08x}', 'argc': 1, 'ret': f'buf:{lname}'})
            if gate_active != 0:
                stubs.append({'address': f'0x{allow_b_addr:08x}', 'argc': 1,
                              'ret': f'0x{allow_motion_b_ret:x}'})
            if not (points_flags & 0x40):
                if not world_dbg_present:
                    stubs.append({'address': f'0x{FATAL_ERROR:08x}', 'argc': 4})
                if debug_freeze != 0:
                    stubs.append({'address': f'0x{OWNER_EXEMPT_A:08x}', 'argc': 1,
                                  'ret': f'0x{exempt_a:x}'})
                    if exempt_a == 0:
                        stubs.append({'address': f'0x{OWNER_EXEMPT_B:08x}', 'argc': 1,
                                      'ret': f'0x{exempt_b:x}'})
            extra_stub = {
                'address': f'0x{DATA_EXTRA_ROT:08x}',
                'argi': [0],
                'argu8': [1],
                'argf32': [0],
                'ret': pair_f32_ret(extra_rot[0], extra_rot[1]),
            }
            if clear_targets_in_extra:
                extra_stub['writes'] = [
                    {'arg': 0, 'offset': 0x280, 'bytes': '00' * 8},
                    {'arg': 0, 'offset': 0x80, 'bytes': '00' * 8},
                ]
                targets_present = False
            stubs.append(extra_stub)

        if targets_present:
            stubs.append({'address': f'0x{vis_addr:08x}', 'argc': 1, 'ret': f'0x{visible2:x}'})

        apply_step_argmem = [
            {'arg': 1, 'size': 16},
            {'arg': 2, 'size': 16},
            {'arg': 3, 'size': 16},
            {'arg': 4, 'size': 16},
        ]
        if snapshot_self_flags_in_apply_step:
            apply_step_argmem += [
                {'arg': 0, 'offset': 0x1d9, 'size': 1},
                {'arg': 0, 'offset': 0x28a, 'size': 1},
            ]
        apply_step_stub = {
            'address': f'0x{DATA_APPLY_STEP:08x}',
            'argi': [0, 1, 2, 3, 4, 5],
            # Original writes 32-bit zero-extended dwords at [rsp] and [rsp+8] leaving upper
            # 32 bits unwritten; real callees 0x0191e6a0 / 0x0192b380 read byte [rbp+0x10/+0x18].
            'argu32': [6, 7],
            'argf32': [0],
            'argmem': apply_step_argmem,
        }
        if clear_anim_ready_in_apply_step:
            apply_step_stub['writes'] = [
                {'arg': 0, 'offset': SELF_ANIM_OFF + 0x384, 'bytes': '00'},
            ]
            post_anim_ready = 0
        stubs.append(apply_step_stub)

    if targets_present:
        if (mode_flags & 7) == 7:
            active_update = True
        else:
            stubs.append({'address': f'0x{vis_addr:08x}', 'argc': 1, 'ret': f'0x{visible3:x}'})
            active_update = visible3 != 0

        if active_update:
            upd_stub = {'address': f'0x{upd_active_addr:08x}', 'argc': 2, 'argf32': [0]}
            writes = []
            if mutate_anim_in_target_update == 'unready':
                writes.append({'arg': 1, 'offset': SELF_ANIM_OFF + 0x384, 'bytes': '00'})
                post_anim_ready = 0
            elif mutate_anim_in_target_update == 'null':
                writes.append({'arg': 1, 'offset': SELF_MODULES_OFF + 0x30, 'bytes': '00' * 8})
                post_anim_exists = False
            elif mutate_anim_in_target_update == 'ready':
                writes.append({'arg': 1, 'offset': SELF_ANIM_OFF + 0x384, 'bytes': '01'})
                post_anim_ready = 1
            if clear_primary_in_target_update:
                writes.append({'arg': 1, 'offset': 0x280, 'bytes': '00' * 8})
            if writes:
                upd_stub['writes'] = writes
            stubs.append(upd_stub)
        else:
            stubs.append({'address': f'0x{upd_inactive_addr:08x}', 'argc': 1})

    if post_anim_exists and post_anim_ready != 0:
        stubs.append({'address': f'0x{OWNER_PROJECT_XZ:08x}', 'argc': 1, 'ret': f'0x{project_xz:x}'})
        data_recv_stub = {'address': f'0x{DATA_RECEIVER:08x}', 'argc': 1, 'ret': 'buf:recv'}
        if clear_primary_in_motion_receiver:
            # Clears self->target after motion_target has already sampled local_motion, isolating
            # the final active_target(self) re-read at params_post_update (0x0190a5f0).
            data_recv_stub['writes'] = [{'arg': 0, 'offset': 0x280, 'bytes': '00' * 8}]
        stubs.append(data_recv_stub)
        recv_stub = {
            'address': f'0x{RECV_APPLY_MOTION:08x}',
            'argc': 2,
            'argmem': [{'arg': 1, 'size': 16}],
        }
        if receiver_writes_vec is not None:
            recv_stub['writes'] = [{'arg': 1, 'offset': 0, 'bytes': vec4_hex(receiver_writes_vec)}]
        stubs.append(recv_stub)
        stubs.append({'address': f'0x{PARAMS_POST_UPDATE:08x}', 'argc': 2, 'argf32': [0]})

    memory = []
    for entry in m:
        where = entry[0] if entry[1] is None else f'buf:{entry[0]}+{entry[1]}'
        if len(entry) == 3:
            memory.append({'addr': where, 'bytes': entry[2]})
        elif entry[2] == 'p':
            memory.append({'addr': where, 'pointer': entry[3]})
        else:
            memory.append({'addr': where, 'guest': f'0x{entry[3]:08x}'})

    return {
        'schema': 1,
        'address': '0x01913c10',
        'id': cid,
        'returns': 'void',
        'args': {'rdi': 'buf:self', 'xmm0': f32_hex(dt)},
        'buffers': {k: {'size': v} for k, v in b.items()},
        'memory': memory,
        'stubs': stubs,
        'imports': [],
    }


CASES = [
    # --- AnimEvents gating (entry and post-target-update re-check) ---
    case('anim_null_target_null', anim_exists=False, has_primary=False, has_fallback=False),
    case('anim_null_target_active_mode7', anim_exists=False, mode_flags=0x27, dt=0.05),
    case('anim_not_ready_target_visible', anim_ready=0, mode_flags=0x25, visible3=1, dt=0.025),
    case('anim_not_ready_target_hidden_fallback', anim_ready=0, has_primary=False, mode_flags=0x00, visible3=0),
    case('anim_becomes_unready_in_target_update', mutate_anim_in_target_update='unready'),
    case('anim_becomes_null_in_target_update', mutate_anim_in_target_update='null'),
    case('anim_becomes_ready_in_target_update', anim_ready=0, mutate_anim_in_target_update='ready',
         local_motion=(2.0, 3.0, 4.0, 5.0)),

    # --- Primary vs fallback target selection & null target before apply_step ---
    case('primary_selected_over_fallback', has_primary=True, has_fallback=True, visible1=1, visible2=1),
    case('fallback_selected_when_primary_null', has_primary=False, has_fallback=True, visible1=1, visible2=1),
    case('target_cleared_before_apply_step', clear_targets_in_extra=True, clear_anim_ready_in_apply_step=True),
    case('target_update_mode_not_7_inactive', mode_flags=0x06, visible1=1, visible2=0, visible3=0),

    # --- Visibility vs HP (+0xf8) gate ---
    case('hidden_hp_zero', visible1=0, hp=0, rot_delta=(1.0, 2.0, 3.0, 4.0)),
    case('hidden_hp_neg_one', visible1=0, hp=-1),
    case('hidden_hp_int32_min', visible1=0, hp=-0x80000000),
    case('hidden_hp_one_skips_yaw_quant', visible1=0, hp=1, rot_delta=(0.2, 5.0, -0.3, 0.4)),

    # --- Yaw quantization (thresholds 30 / 60 / 100 deg, signs, negative scale, NaN, 0*inf) ---
    case('yaw_below_30', turn_scale=1, rot_delta=(0x7fc00011, 0x3f060a91, 0x80000000, 0x7fc00022)),
    case('yaw_just_above_30_pos', turn_scale=1, rot_delta=(0x7fc00011, 0x3f060a92, 0x80000000, 0x7fc00022)),
    case('yaw_just_above_30_neg', turn_scale=1, rot_delta=(0x7fc00011, 0xbf060a92, 0x80000000, 0x7fc00022)),
    case('yaw_just_below_60_pos', turn_scale=1, rot_delta=(0.5, 0x3f860a91, -0.5, 1.0)),
    case('yaw_just_above_60_pos', turn_scale=1, rot_delta=(0.5, 0x3f860a92, -0.5, 1.0)),
    case('yaw_just_above_60_neg', turn_scale=1, rot_delta=(0.5, 0xbf860a92, -0.5, 1.0)),
    case('yaw_exact_100_pos', turn_scale=1, rot_delta=(0.5, 0x3fdf66f3, -0.5, 1.0)),
    case('yaw_just_above_100_pos', turn_scale=1, rot_delta=(0.5, 0x3fdf66f4, -0.5, 1.0)),
    case('yaw_just_above_100_neg', turn_scale=1, rot_delta=(0.5, 0xbfdf66f4, -0.5, 1.0)),
    case('yaw_neg_scale_pos_y', turn_scale=-10, rot_delta=(0.25, 0.5, -0.25, 1.0)),
    case('yaw_neg_scale_neg_y', turn_scale=-10, rot_delta=(0.25, -0.5, -0.25, 1.0)),
    case('yaw_nan_keeps_rot', turn_scale=30, rot_delta=(0.25, 0x7fc00077, -0.25, 1.0)),
    case('yaw_zero_scale_inf_y', turn_scale=0, rot_delta=(0.25, 0x7f800000, -0.25, 1.0)),
    case('yaw_inf_pos_y', turn_scale=1, rot_delta=(0.25, 0x7f800000, -0.25, 1.0)),
    case('yaw_inf_neg_y', turn_scale=1, rot_delta=(0.25, 0xff800000, -0.25, 1.0)),

    # --- Motion enable flags (tested with visible1=0, hp>0 so visible doesn't override) ---
    case('flags_both_one_non_canonical_b', visible1=0, hp=5, flags_1bc=0xffff7fff, motion_b_enabled=0x03,
         mode_flags=0xf7, params_mode=0xff, motion_blocked=0),
    case('flags_b_enabled_even_bit0_clear', visible1=0, hp=5, motion_b_enabled=0x02),
    case('flags_1bc_bit15_set_clears_a_only', visible1=0, hp=5, flags_1bc=0x8000, motion_b_enabled=0x01),
    case('flags_linked_null_source', visible1=0, hp=5,
         linked_seq=[('state', 1), ('linked_null_src',)]),
    case('flags_linked_state_zero', visible1=0, hp=5,
         linked_seq=[('state', 1), ('state', 0)]),
    case('flags_linked_state_ten_blocks', visible1=0, hp=5,
         linked_seq=[('state', 1), ('state', 10), ('state', 10)]),
    case('flags_linked_state_nine_allows', visible1=0, hp=5,
         linked_seq=[('state', 1), ('state', 9), ('state', 9)]),
    case('flags_linked_state_eleven_allows', visible1=0, hp=5,
         linked_seq=[('state', 1), ('state', 11), ('state', 11)]),
    case('flags_linked_third_call_null_source_allows', visible1=0, hp=5,
         linked_seq=[('state', 1), ('state', 5), ('linked_null_src',)]),
    case('flags_gate_active_c0_false_clears_b_only', visible1=0, hp=5, gate_active=1, allow_motion_b_ret=0),
    case('flags_gate_active_c0_true_keeps_both', visible1=0, hp=5, gate_active=1, allow_motion_b_ret=1),
    case('flags_motion_blocked_clears_both', visible1=0, hp=5, motion_blocked=1, params_mode=0xff, mode_flags=0x07),
    case('flags_params_mode_not_ff_clears_a_only', visible1=0, hp=5, params_mode=0xfe, mode_flags=0x07),
    case('flags_mode_flags_not_7_clears_both', visible1=0, hp=5, params_mode=0xff, mode_flags=0x06),
    case('flags_visible_overrides_cleared', visible1=1, flags_1bc=0x8000, motion_b_enabled=0, motion_blocked=1,
         params_mode=0x00, mode_flags=0x00, throw_active=0),
    case('flags_throw_state_null_clears_both', visible1=1, throw_state_exists=False),
    case('flags_throw_state_active_clears_both', visible1=1, throw_active=1),

    # --- Freeze paths (points->flags & 0x40 and WorldChrManDbg / 0x0593e894) ---
    case('freeze_points_0x40_resets_orient', visible1=1, points_flags=0x40, world_dbg_present=False,
         owner_flags_359=0x00, rot_delta=(0.1, 2.0, 0.3, 0.4), turn_scale=10),
    case('freeze_points_0x40_keeps_orient_flag20', visible1=1, points_flags=0x40, owner_flags_359=0x20,
         rot_delta=(0.1, 2.0, 0.3, 0.4), turn_scale=10),
    case('freeze_dbg_missing_singleton_reports', visible1=1, points_flags=0x00, world_dbg_present=False,
         debug_freeze=0),
    case('freeze_dbg_active_exempt_a', visible1=1, debug_freeze=1, exempt_a=1),
    case('freeze_dbg_active_exempt_b', visible1=1, debug_freeze=1, exempt_a=0, exempt_b=1),
    case('freeze_dbg_active_triggers_reset_orient', visible1=1, debug_freeze=1, exempt_a=0, exempt_b=0,
         owner_flags_359=0x00, rot_delta=(0.1, 2.0, 0.3, 0.4), turn_scale=10),
    case('freeze_dbg_active_triggers_keep_orient', visible1=1, debug_freeze=1, exempt_a=0, exempt_b=0,
         owner_flags_359=0x20),

    # --- extra_rotation + rotation_delta NaN payload ordering ---
    case('extra_rot_add_nan_order',
         extra_rot=(0x7fc00111, 0x7fc00222),
         rot_delta=(0x7fc00333, 0x7fc00444, 0x7fc00555, 0x7fc00666),
         visible1=0, hp=5),

    # --- Matrix transform (asymmetric non-unit rows, w preservation, NaN ordering, receiver mutation) ---
    case('mat_transform_asymmetric_non_unit',
         local_motion=(1.5, -2.25, 3.75, -0.625),
         row_x=(0.25, -0.5, 1.25, 0.75),
         row_y=(-1.5, 0.75, 0.5, -0.25),
         row_z=(0.5, 1.75, -0.25, 2.0),
         project_xz=0, dt=0.04),
    case('mat_transform_nan_order_mul_and_hadd',
         local_motion=(0x7fc000a1, 0x7fc000a2, 0x7fc000a3, 0x7fc000a4),
         row_x=(0x7fc000b1, 1.0, 1.0, 1.0),
         row_y=(1.0, 0x7fc000b2, 1.0, 1.0),
         row_z=(1.0, 1.0, 0x7fc000b3, 0x7fc000b4),
         project_xz=0),
    case('mat_transform_hadd_lane_order',
         local_motion=(1.0, 1.0, 1.0, 1.0),
         row_x=(0x7fc000c0, 0x7fc000c1, 0x7fc000c2, 0x7fc000c3),
         row_y=(1.0, 0x7fc000d1, 0x7fc000d2, 1.0),
         row_z=(1.0, 1.0, 0x7fc000e2, 0x7fc000e3),
         project_xz=0),
    case('receiver_modifies_motion_delta',
         local_motion=(1.0, 2.0, 3.0, 4.0),
         receiver_writes_vec=(9.5, -8.25, 7.125, -6.5)),

    # --- Horizontal XZ projection (owner +0x4d8) ---
    case('flatten_xz_normal_3d', local_motion=(3.0, 12.0, 4.0, 99.0), project_xz=1),
    case('flatten_xz_exact_epsilon_takes_zero', local_motion=(0x34000000, 5.0, 0.0, 1.0), project_xz=1),
    case('flatten_xz_above_epsilon_scales', local_motion=(0x34000001, 5.0, 0.0, 1.0), project_xz=1),
    case('flatten_xz_nan_rx_takes_zero', local_motion=(0x7fc00099, 2.0, 3.0, 1.0), project_xz=1),
    case('flatten_xz_inf_uses_vector_nan', local_motion=(1e20, 3.0, 0.0, 1.0), project_xz=1),
    case('flatten_xz_inf_and_ry_nan_payload_order',
         local_motion=(1e20, 1.0, 0.0, 1.0), row_y=(0.0, 0x7fc000aa, 0.0, 0.0), project_xz=1),
    case('flatten_xz_finite_xz_inf_ry', local_motion=(3.0, 1e20, 4.0, 1.0), project_xz=1),
    case('flatten_xz_finite_xz_nan_ry',
         local_motion=(3.0, 1.0, 4.0, 1.0), row_y=(0.0, 0x7fc00055, 0.0, 0.0), project_xz=1),
    case('flatten_xz_only_rz_above_epsilon', local_motion=(0.0, 5.0, 0x34000001, 1.0), project_xz=1),
    case('flatten_xz_all_negative', local_motion=(-3.0, -12.0, -4.0, -7.0), project_xz=1),

    # --- Cross-phase target re-read, visibility independence, INT32_MIN scale, NaN dt ---
    case('visible_sequence_0_1_0', visible1=0, hp=5, visible2=1, visible3=0, mode_flags=0x06),
    case('visible_sequence_1_0_1', visible1=1, visible2=0, visible3=1, mode_flags=0x06),
    case('visible_overrides_linked_and_gate', visible1=1,
         linked_seq=[('state', 1), ('state', 10), ('state', 10)], gate_active=1, allow_motion_b_ret=0),
    case('target_switches_to_fallback_in_update_active', has_primary=True, has_fallback=True,
         clear_primary_in_target_update=True),
    case('yaw_int32_min_scale_pos_y', turn_scale=-0x80000000, rot_delta=(0.1, 1e-8, 0.2, 0.3)),
    case('dt_nan_payload_passthrough', dt=0x7fc00123),

    # --- Review follow-up targeted fixtures (pre-call store ordering & post-receiver target re-read) ---
    case('apply_step_observes_flag_stores_before_call', visible1=0, hp=5, flags_1bc=0x8000,
         motion_b_enabled=0x01, init_flag_1d9=0xaa, init_flag_28a=0x55,
         snapshot_self_flags_in_apply_step=True),
    case('target_switches_to_fallback_in_motion_receiver', has_primary=True, has_fallback=True,
         clear_primary_in_motion_receiver=True),
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
