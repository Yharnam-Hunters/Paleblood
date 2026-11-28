#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for character_params_post_update (0x0190a5f0), for tools/verify.py run.

usage: params_post_update_cases.py OUT_DIR

Every input sits in a named buffer; virtual methods on DrawTarget (+0x90, +0xc8, +0xf8) and
ChrIns (+0x440) point at spare guest addresses that the cases stub, and 0x01e60620 is stubbed
directly. Covers:
- early exits when owner->modules->anim_events is null or ready == 0 (plus non-canonical ready);
- event gate suppression (gate null, bit 0 clear with other bits set, bit 0 set skipping visible());
- byte-wide return semantics (`test al, al`) for target->visible (+0xc8) and target->sync_flags_ready
  (+0xf8), including 0x0100 (low byte zero, upper byte non-zero) and 0x02 / 0x80 (bit 0 clear);
- 32-bit return semantics (`test eax, eax`) for target->has_action_state (+0x90), including
  0x00000001_00000000 (low 32 bits zero) and 0x00000100 / 0x80000000 (low byte zero, EAX non-zero);
- visible-path bit mirroring from target->flags_0x90 (bit 0 -> self +0x5c bit 7, bit 1 -> owner
  +0x1e4 bit 3, bit 3 -> self +0x5c bit 6, bit 4 -> flags_module +0x818 bit 4, bit 2 ignored) and
  preservation of all neighboring bits across +0x5c..+0x5d, +0x1e1..+0x1e5, and +0x818;
- high-byte state transitions (raw_state = flags_0x90 >> 24, next_state = raw_state - 1): raw 0
  skipping vtable+0x440 while still writing -1, equal state skipping vtable+0x440, unsigned raw 255
  producing 254, pre-call state observation (old state visible inside +0x440 while flags are already
  updated), post-call overwrite of state_0x1c4, and post-call re-reads of target->flags_0x90 bit 4
  and self->data->owner across visible, sync_flags_ready, and on_state_change;
- hidden/gated path: signed points > 0 boundary (0, -1, INT32_MIN, 1, INT32_MAX) across all four
  (bits_0x20, bits_0x30) zero/non-zero combinations; action_id_0x94 -> self->action_id_0xa0 store
  before 0x01e60620; distinct action_request->owner flag indirection selecting bits_0x20 vs bits_0x30;
  0x01e60620 trigger conditions and mode priority (bit 0 -> 3, bit 2 -> 4, bit 0 + bit 2 -> 3,
  action_id 0x0c26 alone -> 3, 0x0c26 + bit 2 -> 4, 0x0c25 / 0x0c27 / other bits skipped);
  post-call owner/modules re-reads after has_action_state and 0x01e60620;
- final owner bit-10 update (owner +0x1e2 bit 2) gated by flags_0x10 bit 3, value from bit 4,
  suppression via owner->data->params->flags_0x42 bit 3 (with null owner->data, null params,
  distinct owner->data->params vs self, and preservation of all 39 other bits in +0x1e1..+0x1e5);
- unused xmm0 register independence (NaN in xmm0 ignored on both paths).
"""
import json
import os
import struct
import sys

THROW_REQUEST_CHECK = 0x01e60620

# Spare guest code addresses used as distinct virtual-method stub targets.
TARGET_HAS_ACTION_STATE = 0x00500000
TARGET_VISIBLE = 0x00500100
TARGET_SYNC_READY = 0x00500200
OWNER_ON_STATE_CHANGE = 0x00500300


def u16_hex(v):
    return struct.pack('<H', v & 0xffff).hex()


def u32_hex(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def i32_hex(v):
    return struct.pack('<i', v).hex()


def u64_hex(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def bytes_hex(seq):
    return bytes(seq).hex()


def case(
    cid,
    *,
    anim_exists=True,
    anim_ready=1,
    gate_exists=True,
    gate_flags=0x00,
    visible_ret=1,
    sync_ready_ret=1,
    has_action_state_ret=1,
    target_flags=0x0200001b,
    init_params_5c=0x0000,
    init_params_a0=0xdeadbeef,
    init_params_42=0x00,
    init_owner_state=0,
    init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00),
    init_flags_10=0x00,
    init_flags_818=0x00,
    points=100,
    bits_0x20=0,
    bits_0x30=0,
    action_id=0x0100,
    distinct_req_owner=False,
    req_owner_flags_10=0x00,
    owner_data_exists=True,
    owner_params_exists=True,
    distinct_owner_params=False,
    owner_params_42=0x00,
    snapshot_in_on_state_change=False,
    mutate_state_in_on_state_change=None,
    mutate_target_flags_in_on_state_change=None,
    switch_owner_in_visible=False,
    switch_owner_in_sync_ready=False,
    switch_owner_in_on_state_change=False,
    switch_owner_in_has_action_state=False,
    snapshot_action_id_in_throw_check=False,
    switch_owner_in_throw_check=False,
    owner2_flags_10=0x18,
    owner2_flags_818=0x00,
    owner2_state=77,
    owner2_1e1_1e5=(0x11, 0x22, 0x33, 0x44, 0x55),
    owner2_points=10,
    owner2_bits_0x20=0,
    owner2_bits_0x30=0x04,
    owner2_action_id=0x00ab,
    xmm0_hex=None,
):
    """Build one synthetic schema-1 differential test case for 0x0190a5f0."""
    b = {
        'self': 0x100,
        'data': 0x50,
        'owner': 0x480,
        'owner_vt': 0x448,
        'gate_holder': 0x40,
        'modules': 0x98,
        'flags_mod': 0x820,
        'points': 0x140,
        'action_req': 0xa0,
        'throw_req': 0x180,
        'target': 0x200,
        'target_vt': 0x100,
    }

    # Place `data` inside the callback's arg 0 buffer when a stub must rewrite data->owner.
    data_ref = 'data'
    data_buf = 'data'
    data_off = 0
    if switch_owner_in_visible or switch_owner_in_sync_ready or switch_owner_in_has_action_state:
        data_buf, data_off = 'target', 0x100
        data_ref = f'target+{data_off}'
    elif switch_owner_in_on_state_change:
        data_buf, data_off = 'owner', 0x400
        data_ref = f'owner+{data_off}'
    elif switch_owner_in_throw_check:
        data_buf, data_off = 'throw_req', 0x100
        data_ref = f'throw_req+{data_off}'

    # Place `self` (and `data`, when also switching owner) inside `throw_req` so
    # throw_request_check can snapshot self->action_id_0xa0 and mutate data->owner via arg 0.
    self_ref = 'self'
    self_buf = 'self'
    self_off = 0
    if snapshot_action_id_in_throw_check:
        b['throw_req'] = 0x220
        self_buf, self_off = 'throw_req', 0x100
        self_ref = f'throw_req+{self_off}'
        if switch_owner_in_throw_check:
            data_buf, data_off = 'throw_req', 0x40
            data_ref = f'throw_req+{data_off}'

    # Place `target` inside `owner` when on_state_change must mutate target->flags_0x90 via arg 0.
    target_ref = 'target'
    target_buf = 'target'
    target_off = 0
    if mutate_target_flags_in_on_state_change is not None:
        b['owner'] = 0x600
        target_buf, target_off = 'owner', 0x480
        target_ref = f'owner+{target_off}'

    m = [
        (self_buf, self_off + 0x08, 'p', data_ref),
        (self_buf, self_off + 0x42, f'{init_params_42:02x}'),
        (self_buf, self_off + 0x5c, u16_hex(init_params_5c)),
        (self_buf, self_off + 0xa0, u32_hex(init_params_a0)),
        (data_buf, data_off + 0x08, 'p', 'owner'),
        ('owner', 0x000, 'p', 'owner_vt'),
        ('owner', 0x018, 'p', 'gate_holder'),
        ('owner', 0x1c4, i32_hex(init_owner_state)),
        ('owner', 0x1e1, bytes_hex(init_owner_1e1_1e5)),
        ('owner', 0x3b0, 'p', 'modules'),
        ('owner_vt', 0x440, 'g', OWNER_ON_STATE_CHANGE),
        ('modules', 0x08, 'p', 'flags_mod'),
        ('modules', 0x20, 'p', 'points'),
        ('modules', 0x80, 'p', 'action_req'),
        ('modules', 0x88, 'p', 'throw_req'),
        ('flags_mod', 0x10, f'{init_flags_10:02x}'),
        ('flags_mod', 0x818, f'{init_flags_818:02x}'),
        ('points', 0x134, i32_hex(points)),
        ('action_req', 0x20, u64_hex(bits_0x20)),
        ('action_req', 0x30, u64_hex(bits_0x30)),
        ('action_req', 0x94, u32_hex(action_id)),
        (target_buf, target_off + 0x00, 'p', 'target_vt'),
        (target_buf, target_off + 0x90, u32_hex(target_flags)),
        ('target_vt', 0x90, 'g', TARGET_HAS_ACTION_STATE),
        ('target_vt', 0xc8, 'g', TARGET_VISIBLE),
        ('target_vt', 0xf8, 'g', TARGET_SYNC_READY),
    ]

    if owner_data_exists:
        b['owner_data'] = 0x50
        m.append(('owner', 0x058, 'p', 'owner_data'))
        if owner_params_exists:
            if distinct_owner_params:
                b['owner_params'] = 0x60
                m.append(('owner_data', 0x38, 'p', 'owner_params'))
                m.append(('owner_params', 0x42, f'{owner_params_42:02x}'))
            else:
                m.append(('owner_data', 0x38, 'p', self_ref))

    if anim_exists:
        b['anim'] = 0x390
        m.append(('modules', 0x30, 'p', 'anim'))
        m.append(('anim', 0x384, f'{anim_ready:02x}'))

    if gate_exists:
        b['gate'] = 0x20
        m.append(('gate_holder', 0x30, 'p', 'gate'))
        m.append(('gate', 0x18, f'{gate_flags:02x}'))

    if distinct_req_owner:
        b['req_owner'] = 0x3c0
        b['req_modules'] = 0x20
        b['req_flags'] = 0x20
        m += [
            ('action_req', 0x08, 'p', 'req_owner'),
            ('req_owner', 0x3b0, 'p', 'req_modules'),
            ('req_modules', 0x08, 'p', 'req_flags'),
            ('req_flags', 0x10, f'{req_owner_flags_10:02x}'),
        ]
    else:
        m.append(('action_req', 0x08, 'p', 'owner'))

    need_owner2 = (
        switch_owner_in_visible
        or switch_owner_in_sync_ready
        or switch_owner_in_on_state_change
        or switch_owner_in_has_action_state
        or switch_owner_in_throw_check
    )
    if need_owner2:
        b['owner2'] = 0x3c0
        b['owner2_modules'] = 0x98
        b['owner2_flags'] = 0x820
        m += [
            ('owner2', 0x000, 'p', 'owner_vt'),
            ('owner2', 0x058, 'p', 'owner_data') if owner_data_exists else ('owner2', 0x058, u64_hex(0)),
            ('owner2', 0x1c4, i32_hex(owner2_state)),
            ('owner2', 0x1e1, bytes_hex(owner2_1e1_1e5)),
            ('owner2', 0x3b0, 'p', 'owner2_modules'),
            ('owner2_modules', 0x08, 'p', 'owner2_flags'),
            ('owner2_flags', 0x10, f'{owner2_flags_10:02x}'),
            ('owner2_flags', 0x818, f'{owner2_flags_818:02x}'),
        ]
        if switch_owner_in_has_action_state:
            b['owner2_points'] = 0x140
            b['owner2_action_req'] = 0xa0
            b['owner2_throw_req'] = 0x40
            m += [
                ('owner2_modules', 0x20, 'p', 'owner2_points'),
                ('owner2_modules', 0x80, 'p', 'owner2_action_req'),
                ('owner2_modules', 0x88, 'p', 'owner2_throw_req'),
                ('owner2_points', 0x134, i32_hex(owner2_points)),
                ('owner2_action_req', 0x08, 'p', 'owner2'),
                ('owner2_action_req', 0x20, u64_hex(owner2_bits_0x20)),
                ('owner2_action_req', 0x30, u64_hex(owner2_bits_0x30)),
                ('owner2_action_req', 0x94, u32_hex(owner2_action_id)),
            ]

    # Script expected calls matching the original's exact control flow.
    stubs = []
    if anim_exists and anim_ready != 0:
        suppressed = gate_exists and ((gate_flags & 0x01) != 0)
        take_visible = False
        if not suppressed:
            vis_stub = {'address': f'0x{TARGET_VISIBLE:08x}', 'argc': 1, 'ret': f'0x{visible_ret:x}'}
            if switch_owner_in_visible:
                vis_stub['writes'] = [{'arg': 0, 'offset': data_off + 0x08, 'pointer': 'owner2'}]
            stubs.append(vis_stub)
            take_visible = (visible_ret & 0xff) != 0

        if take_visible:
            sync_stub = {'address': f'0x{TARGET_SYNC_READY:08x}', 'argc': 1, 'ret': f'0x{sync_ready_ret:x}'}
            if switch_owner_in_sync_ready:
                sync_stub['writes'] = [{'arg': 0, 'offset': data_off + 0x08, 'pointer': 'owner2'}]
            stubs.append(sync_stub)
            if (sync_ready_ret & 0xff) != 0:
                raw_state = (target_flags >> 24) & 0xff
                next_state = raw_state - 1
                cur_state = owner2_state if (switch_owner_in_visible or switch_owner_in_sync_ready) else init_owner_state
                if cur_state != next_state and raw_state != 0:
                    st_stub = {'address': f'0x{OWNER_ON_STATE_CHANGE:08x}', 'argc': 2}
                    if snapshot_in_on_state_change:
                        st_stub['argmem'] = [
                            {'arg': 0, 'offset': 0x1c4, 'size': 4},
                            {'arg': 0, 'offset': 0x1e4, 'size': 1},
                        ]
                    writes = []
                    if mutate_state_in_on_state_change is not None:
                        writes.append({'arg': 0, 'offset': 0x1c4, 'bytes': i32_hex(mutate_state_in_on_state_change)})
                    if mutate_target_flags_in_on_state_change is not None:
                        writes.append({'arg': 0, 'offset': target_off + 0x90,
                                       'bytes': u32_hex(mutate_target_flags_in_on_state_change)})
                    if switch_owner_in_on_state_change:
                        writes.append({'arg': 0, 'offset': data_off + 0x08, 'pointer': 'owner2'})
                    if writes:
                        st_stub['writes'] = writes
                    stubs.append(st_stub)
        else:
            has_stub = {'address': f'0x{TARGET_HAS_ACTION_STATE:08x}', 'argc': 1,
                        'ret': f'0x{has_action_state_ret:x}'}
            if switch_owner_in_has_action_state:
                has_stub['writes'] = [{'arg': 0, 'offset': data_off + 0x08, 'pointer': 'owner2'}]
            stubs.append(has_stub)
            eff_points = owner2_points if switch_owner_in_has_action_state else points
            if (has_action_state_ret & 0xffffffff) != 0 and eff_points > 0:
                if switch_owner_in_has_action_state:
                    sel_flags = owner2_flags_10
                    act_bits = owner2_bits_0x20 if (sel_flags & 0x01) else owner2_bits_0x30
                    eff_action_id = owner2_action_id
                else:
                    sel_flags = req_owner_flags_10 if distinct_req_owner else init_flags_10
                    act_bits = bits_0x20 if (sel_flags & 0x01) else bits_0x30
                    eff_action_id = action_id
                if (act_bits & 0x01) or (act_bits & 0x04) or eff_action_id == 0x0c26:
                    th_stub = {'address': f'0x{THROW_REQUEST_CHECK:08x}', 'argc': 3}
                    if snapshot_action_id_in_throw_check:
                        th_stub['argmem'] = [{'arg': 0, 'offset': self_off + 0xa0, 'size': 4}]
                    if switch_owner_in_throw_check:
                        th_stub['writes'] = [{'arg': 0, 'offset': data_off + 0x08, 'pointer': 'owner2'}]
                    stubs.append(th_stub)

    # Ensure every virtual method target and 0x01e60620 always has a safe scripted stub installed
    # even on paths the original does not take, so wrong-branch mutants produce clean behavioral
    # call/write mismatches rather than jumping into unpatched spare guest bytes.
    scripted_addrs = {s['address'] for s in stubs}
    for addr, argc, default_ret in (
        (TARGET_VISIBLE, 1, f'0x{visible_ret:x}'),
        (TARGET_SYNC_READY, 1, f'0x{sync_ready_ret:x}'),
        (TARGET_HAS_ACTION_STATE, 1, f'0x{has_action_state_ret:x}'),
        (OWNER_ON_STATE_CHANGE, 2, '0x0'),
        (THROW_REQUEST_CHECK, 3, '0x0'),
    ):
        hex_addr = f'0x{addr:08x}'
        if hex_addr not in scripted_addrs:
            stubs.append({'address': hex_addr, 'argc': argc, 'ret': default_ret})

    memory = []
    for entry in m:
        where = entry[0] if entry[1] is None else f'buf:{entry[0]}+{entry[1]}'
        if len(entry) == 3:
            memory.append({'addr': where, 'bytes': entry[2]})
        elif entry[2] == 'p':
            memory.append({'addr': where, 'pointer': entry[3]})
        else:
            memory.append({'addr': where, 'guest': f'0x{entry[3]:08x}'})

    args = {'rdi': f'buf:{self_ref}', 'rsi': f'buf:{target_ref}'}
    if xmm0_hex is not None:
        args['xmm0'] = xmm0_hex

    return {
        'schema': 1,
        'address': '0x0190a5f0',
        'id': cid,
        'returns': 'void',
        'args': args,
        'buffers': {k: {'size': v} for k, v in b.items()},
        'memory': memory,
        'stubs': stubs,
        'imports': [],
    }


CASES = [
    # --- Early exits: anim_events null / not ready / non-canonical ready ---
    case('anim_events_null_early_return', anim_exists=False, init_params_5c=0xaaaa,
         init_owner_1e1_1e5=(0x11, 0x22, 0x33, 0x44, 0x55), init_flags_818=0x55),
    case('anim_events_not_ready_early_return', anim_ready=0, init_params_5c=0xaaaa,
         init_owner_1e1_1e5=(0x11, 0x22, 0x33, 0x44, 0x55), init_flags_818=0x55),
    case('anim_events_ready_0x80_enters', anim_ready=0x80, target_flags=0x0300001b),

    # --- Event gate & visible (+0xc8) byte-wide callback semantics ---
    case('gate_null_calls_visible', gate_exists=False, visible_ret=1, target_flags=0x02000019),
    case('gate_other_bits_0xfe_calls_visible', gate_flags=0xfe, visible_ret=1, target_flags=0x02000013),
    case('gate_suppress_bit0_skips_visible', gate_flags=0x01, has_action_state_ret=0),
    case('gate_suppress_0xff_skips_visible_runs_hidden', gate_flags=0xff, has_action_state_ret=1,
         points=50, bits_0x30=0x04, init_flags_10=0x18, init_owner_1e1_1e5=(0x11, 0x22, 0x33, 0x44, 0x55)),
    case('visible_ret_0x00_goes_hidden', visible_ret=0x00, has_action_state_ret=0),
    case('visible_ret_0x0100_low_byte_zero_goes_hidden', visible_ret=0x0100, has_action_state_ret=0),
    case('visible_ret_0x02_even_nonzero_goes_visible', visible_ret=0x02, sync_ready_ret=1, target_flags=0x0200001b),
    case('visible_ret_0x80_high_bit_goes_visible', visible_ret=0x80, sync_ready_ret=1, target_flags=0x0200001b),

    # --- Visible path: sync_flags_ready (+0xf8) byte-wide semantics & tail bit 4 ---
    case('sync_ready_0x00_skips_sync_sets_bit4', sync_ready_ret=0x00, target_flags=0x0500001b,
         init_params_5c=0x1234, init_owner_state=9, init_owner_1e1_1e5=(0xaa, 0xbb, 0xcc, 0x00, 0xee),
         init_flags_818=0x05),
    case('sync_ready_0x00_clears_bit4_preserves_other_bits', sync_ready_ret=0x00, target_flags=0x0500000f,
         init_params_5c=0xffff, init_owner_state=9, init_owner_1e1_1e5=(0xff, 0xff, 0xff, 0xff, 0xff),
         init_flags_818=0xff),
    case('sync_ready_0x0100_low_byte_zero_skips_sync', sync_ready_ret=0x0100, target_flags=0x0500001b,
         init_params_5c=0x0000, init_owner_state=9, init_flags_818=0x00),
    case('sync_ready_0x02_even_nonzero_runs_sync', sync_ready_ret=0x02, target_flags=0x0200001b),
    case('sync_ready_0x80_high_bit_runs_sync', sync_ready_ret=0x80, target_flags=0x0200001b),

    # --- Visible path: exact bit mapping & neighbor bit preservation ---
    case('sync_all_bits_set_from_zero', target_flags=0x0200001b, init_params_5c=0x0000,
         init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00), init_flags_818=0x00),
    case('sync_all_bits_clear_preserves_neighbors', target_flags=0x02000004, init_params_5c=0xffff,
         init_owner_1e1_1e5=(0xff, 0xff, 0xff, 0xff, 0xff), init_flags_818=0xff),
    case('sync_only_bit0_sets_5c_bit7', target_flags=0x01000001, init_params_5c=0xaa55,
         init_owner_1e1_1e5=(0xa5, 0x5a, 0x3c, 0xff, 0xc3), init_flags_818=0xff),
    case('sync_only_bit1_sets_owner_1e4_bit3', target_flags=0x01000002, init_params_5c=0xffff,
         init_owner_1e1_1e5=(0xa5, 0x5a, 0x3c, 0x00, 0xc3), init_flags_818=0x00),
    case('sync_only_bit3_sets_5c_bit6', target_flags=0x01000008, init_params_5c=0xaa05,
         init_owner_1e1_1e5=(0xa5, 0x5a, 0x3c, 0xff, 0xc3), init_flags_818=0x00),
    case('sync_only_bit2_ignored', target_flags=0x01000004, init_params_5c=0x0000,
         init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00), init_flags_818=0x00),
    case('sync_only_bit4_sets_818_bit4', target_flags=0x01000010, init_params_5c=0x0000,
         init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00), init_flags_818=0xaa),

    # --- Visible path: high-byte state transitions, ordering & pointer re-reads ---
    case('state_raw_0_skips_call_writes_neg1', target_flags=0x0000000b, init_owner_state=5),
    case('state_raw_0_already_neg1_skips_call', target_flags=0x0000000b, init_owner_state=-1),
    case('state_raw_1_from_neg1_calls_with_0', target_flags=0x0100000b, init_owner_state=-1),
    case('state_raw_1_already_0_skips_call', target_flags=0x0100000b, init_owner_state=0),
    case('state_raw_255_calls_with_254', target_flags=0xff00000b, init_owner_state=0),
    case('state_change_observes_old_state_and_updated_flags', target_flags=0x04000002,
         init_owner_state=1, init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00),
         snapshot_in_on_state_change=True),
    case('state_change_mutates_state_and_target_bit4', target_flags=0x0400000b,
         init_owner_state=1, init_flags_818=0x05,
         mutate_state_in_on_state_change=999,
         mutate_target_flags_in_on_state_change=0x0400001b),
    case('visible_switches_owner_re_read_for_sync', target_flags=0x0500001b,
         switch_owner_in_visible=True, owner2_state=2, owner2_flags_818=0x01),
    case('sync_ready_switches_owner_re_read_for_sync', target_flags=0x0500001b,
         init_owner_state=4, init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00), init_flags_818=0x00,
         switch_owner_in_sync_ready=True, owner2_state=1,
         owner2_1e1_1e5=(0x11, 0x22, 0x33, 0x00, 0x55), owner2_flags_818=0x03),
    case('state_change_switches_owner_re_read_for_bit4', target_flags=0x0500001b,
         init_owner_state=1, init_flags_818=0x00,
         switch_owner_in_on_state_change=True, owner2_flags_818=0x02),

    # --- Hidden/gated path: has_action_state (+0x90) 32-bit return width ---
    case('hidden_action_state_zero_returns', visible_ret=0, has_action_state_ret=0,
         points=100, bits_0x30=0x01, init_flags_10=0x18),
    case('hidden_action_state_upper32_only_returns', visible_ret=0, has_action_state_ret=0x0000000100000000,
         points=100, bits_0x30=0x01, init_flags_10=0x18),
    case('hidden_action_state_0x100_low_byte_zero_continues', visible_ret=0, has_action_state_ret=0x00000100,
         points=100, bits_0x30=0x01, action_id=0x0077, init_flags_10=0x18),
    case('hidden_action_state_0x80000000_continues', visible_ret=0, has_action_state_ret=0x80000000,
         points=100, bits_0x30=0x04, action_id=0x0088, init_flags_10=0x18),
    case('has_action_state_switches_owner_re_read_for_action', visible_ret=0, has_action_state_ret=1,
         points=0, bits_0x20=0, bits_0x30=0, action_id=0x0100, init_flags_10=0x00,
         init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00),
         switch_owner_in_has_action_state=True, owner2_points=10, owner2_bits_0x20=0,
         owner2_bits_0x30=0x04, owner2_action_id=0x00ab, owner2_flags_10=0x18,
         owner2_1e1_1e5=(0x11, 0x00, 0x33, 0x44, 0x55)),

    # --- Hidden path: signed points > 0 boundary & bits_0x20/bits_0x30 combinations ---
    case('hidden_points_zero_skips_action_runs_final_flag', visible_ret=0, points=0,
         bits_0x20=0x01, bits_0x30=0x01, action_id=0x0c26, init_flags_10=0x18),
    case('hidden_points_neg_one_skips_action', visible_ret=0, points=-1,
         bits_0x20=0x00, bits_0x30=0x04, action_id=0x0c26, init_flags_10=0x18),
    case('hidden_points_int32_min_skips_action', visible_ret=0, points=-0x80000000,
         bits_0x20=0x00, bits_0x30=0x00, action_id=0x0c26, init_flags_10=0x18),
    case('hidden_points_one_both_bits_zero_writes_action_id', visible_ret=0, points=1,
         bits_0x20=0x00, bits_0x30=0x00, action_id=0x1234, init_flags_10=0x00),
    case('hidden_points_one_bits20_zero_bits30_bit2_calls_mode4', visible_ret=0, points=1,
         bits_0x20=0x00, bits_0x30=0x04, action_id=0x1234, init_flags_10=0x00),
    case('hidden_points_one_bits20_bit0_bits30_zero_calls_mode3', visible_ret=0, points=1,
         bits_0x20=0x01, bits_0x30=0x00, action_id=0x1234, init_flags_10=0x01),
    case('hidden_points_int32_max_enters_action', visible_ret=0, points=0x7fffffff,
         bits_0x20=0x00, bits_0x30=0x01, action_id=0x5678, init_flags_10=0x00),

    # --- Hidden path: action_request->owner flag selection & 0x01e60620 mode priority ---
    case('action_selects_bits20_via_distinct_req_owner', visible_ret=0, points=10,
         bits_0x20=0x04, bits_0x30=0x01, action_id=0x0042,
         init_flags_10=0x00, distinct_req_owner=True, req_owner_flags_10=0x01),
    case('action_selects_bits30_via_distinct_req_owner', visible_ret=0, points=10,
         bits_0x20=0x01, bits_0x30=0x04, action_id=0x0043,
         init_flags_10=0x01, distinct_req_owner=True, req_owner_flags_10=0xfe),
    case('action_bit0_only_calls_mode3', visible_ret=0, points=10, bits_0x30=0x01, action_id=0x0001),
    case('action_bit2_only_calls_mode4', visible_ret=0, points=10, bits_0x30=0x04, action_id=0x0002),
    case('action_bit0_and_bit2_prefers_mode3', visible_ret=0, points=10, bits_0x30=0x05, action_id=0x0003),
    case('action_id_0xc26_alone_calls_mode3', visible_ret=0, points=10, bits_0x30=0x00, action_id=0x0c26),
    case('action_id_0xc26_with_bit2_calls_mode4', visible_ret=0, points=10, bits_0x30=0x04, action_id=0x0c26),
    case('action_id_0xc25_other_bits_skips_call', visible_ret=0, points=10,
         bits_0x30=0xfffffffffffffffa, action_id=0x0c25),
    case('action_id_0xc27_other_bits_skips_call', visible_ret=0, points=10,
         bits_0x30=0x800000000000000a, action_id=0x0c27),
    case('throw_check_observes_action_id_and_switches_owner', visible_ret=0, points=10,
         bits_0x30=0x01, action_id=0x0badc0de, init_flags_10=0x00,
         snapshot_action_id_in_throw_check=True, switch_owner_in_throw_check=True,
         owner2_flags_10=0x18, owner2_1e1_1e5=(0xaa, 0x00, 0xcc, 0xdd, 0xee)),

    # --- Hidden path: final owner bit-10 (0x1e2 bit 2) & owner->data->params->flags_0x42 ---
    case('final_flag_bit3_clear_preserves_owner_1e2_when_set', visible_ret=0, points=0,
         init_flags_10=0x07, init_owner_1e1_1e5=(0xff, 0xff, 0xff, 0xff, 0xff)),
    case('final_flag_bit3_clear_preserves_owner_1e2_when_clear', visible_ret=0, points=0,
         init_flags_10=0xf7, init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00)),
    case('final_flag_bit3_set_bit4_clear_clears_bit10', visible_ret=0, points=0,
         init_flags_10=0x08, init_owner_1e1_1e5=(0xff, 0xff, 0xff, 0xff, 0xff)),
    case('final_flag_owner_data_null_sets_bit10', visible_ret=0, points=0,
         init_flags_10=0x18, owner_data_exists=False,
         init_owner_1e1_1e5=(0x00, 0x00, 0x00, 0x00, 0x00)),
    case('final_flag_owner_params_null_sets_bit10', visible_ret=0, points=0,
         init_flags_10=0x18, owner_data_exists=True, owner_params_exists=False,
         init_owner_1e1_1e5=(0x11, 0x00, 0x33, 0x44, 0x55)),
    case('final_flag_params42_bit3_clear_sets_bit10', visible_ret=0, points=0,
         init_flags_10=0x18, init_params_42=0xf7,
         init_owner_1e1_1e5=(0x11, 0x00, 0x33, 0x44, 0x55)),
    case('final_flag_params42_bit3_set_clears_bit10', visible_ret=0, points=0,
         init_flags_10=0x18, init_params_42=0x08,
         init_owner_1e1_1e5=(0xff, 0xff, 0xff, 0xff, 0xff)),
    case('final_flag_reads_owner_data_params_not_self', visible_ret=0, points=0,
         init_flags_10=0x18, init_params_42=0x00, distinct_owner_params=True, owner_params_42=0x08,
         init_owner_1e1_1e5=(0xff, 0xff, 0xff, 0xff, 0xff)),

    # --- Unused xmm0 independence ---
    case('unused_xmm0_nan_ignored_visible', visible_ret=1, target_flags=0x0200001b, xmm0_hex='2301c07f'),
    case('unused_xmm0_nan_ignored_hidden', visible_ret=0, points=10, bits_0x30=0x04,
         init_flags_10=0x18, xmm0_hex='2301c07f'),
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
