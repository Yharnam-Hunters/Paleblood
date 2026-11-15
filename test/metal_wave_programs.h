/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 programs assembled with LLVM for bonaire, not game shader data.
.text
.globl wave_threshold
wave_threshold:
 v_lshlrev_b32 v3, 3, v1
 v_add_i32 v3, vcc, v0, v3
 v_lshlrev_b32 v4, 5, v2
 v_add_i32 v3, vcc, v3, v4
 v_mov_b32 v4, 5
 v_cmp_le_u32 vcc, s8, v3
 s_and_saveexec_b64 s[10:11], vcc
 s_cbranch_execz threshold_none
 v_mov_b32 v4, 7
threshold_none:
 s_mov_b64 exec, s[10:11]
 s_cbranch_scc1 threshold_store
 v_mov_b32 v4, 9
threshold_store:
 v_lshlrev_b32 v5, 2, v3
 buffer_store_dword v4, v5, s[4:7], 0 offen
 s_endpgm
.globl wave_inactive_vcc
wave_inactive_vcc:
 s_mov_b64 s[10:11], exec
 s_mov_b64 exec, 0
 s_mov_b32 vcc_lo, 0
 s_mov_b32 vcc_hi, 1
 s_cbranch_vccnz inactive_good
 s_mov_b64 exec, s[10:11]
 v_mov_b32 v2, 9
 s_branch inactive_store
inactive_good:
 s_mov_b64 exec, s[10:11]
 v_mov_b32 v2, 7
inactive_store:
 v_lshlrev_b32 v1, 2, v0
 buffer_store_dword v2, v1, s[4:7], 0 offen
 s_endpgm
*/
static const uint32_t wave_threshold[] = {
    0x34060283, 0x4a060700, 0x34080485, 0x4a060903,
    0x7e080285, 0x7d860608, 0xbe8a246a, 0xbf880001,
    0x7e080287, 0xbefe040a, 0xbf850001, 0x7e080289,
    0x340a0682, 0xe0701000, 0x80010405, 0xbf810000,
};
static const uint32_t wave_inactive_vcc[] = {
    0xbe8a047e, 0xbefe0480, 0xbeea0380, 0xbeeb0381,
    0xbf870003, 0xbefe040a, 0x7e040289, 0xbf820002,
    0xbefe040a, 0x7e040287, 0x34020082, 0xe0701000,
    0x80010201, 0xbf810000,
};
