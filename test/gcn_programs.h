/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Synthetic GCN (GFX7) compute programs for the shader translator's tests. Each encoding was
   produced by LLVM's AMDGPU assembler (llvm-mc -triple=amdgcn -mcpu=bonaire --show-encoding) from
   the instruction in its comment. No game code is used.

   User data, as the tests pass it: s[0:3] the input buffer's descriptor (or a constant buffer's),
   s[4:7] the output buffer's. v0 is the lane's index. */
#ifndef TEST_GCN_PROGRAMS_H
#define TEST_GCN_PROGRAMS_H

#include <stdint.h>

/* out[i] = in[i] * 2.5 + 1.0 */
static const uint32_t scale_add[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0301000, 0x80000201, /* buffer_load_dword v2, v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e0602f2, /* v_mov_b32_e32 v3, 1.0 */
    0x3e0604ff, 0x40200000, /* v_mac_f32_e32 v3, 0x40200000, v2 */
    0xe0701000, 0x80010301, /* buffer_store_dword v3, v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, eight dwords at i * 32 bytes: (8 > i ? float(i) : 2.0), 105 + i, the carry out of
   0xffffffff + i, the legacy product 0 * infinity (0; the zero is read from in[64], so no
   compiler can fold the product away), float -4 to signed (-4) and to unsigned (0: saturated). */
static const uint32_t integer_and_select[] = {
    0x7e040d00, /* v_cvt_f32_u32_e32 v2, v0 */
    0x7c0804ff, 0x41000000, /* v_cmp_gt_f32_e32 vcc, 0x41000000, v2 */
    0x000604f4, /* v_cndmask_b32_e32 v3, 2.0, v2, vcc */
    0xb0080064, /* s_movk_i32 s8, 0x64 */
    0x80098508, /* s_add_u32 s9, s8, 5 */
    0x4a080009, /* v_add_i32_e32 v4, vcc, s9, v0 */
    0x7e0a02c1, /* v_mov_b32_e32 v5, -1 */
    0xd24a0a06, 0x00020105, /* v_add_i32_e64 v6, s[10:11], v5, v0 */
    0xd2000007, 0x00290280, /* v_cndmask_b32_e64 v7, 0, 1, s[10:11] */
    0x7e1202ff, 0x7f800000, /* v_mov_b32_e32 v9, 0x7f800000 */
    0xe0300100, 0x80000d00, /* buffer_load_dword v13, off, s[0:3], 0 offset:256 */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x0e10130d, /* v_mul_legacy_f32_e32 v8, v13, v9 */
    0x7e1410f7, /* v_cvt_i32_f32_e32 v10, -4.0 */
    0x7e160ef7, /* v_cvt_u32_f32_e32 v11, -4.0 */
    0x34180085, /* v_lshlrev_b32_e32 v12, 5, v0 */
    0xe0701000, 0x8001030c, /* buffer_store_dword v3, v12, s[4:7], 0 offen */
    0xe0701004, 0x8001040c, /* buffer_store_dword v4, v12, s[4:7], 0 offen offset:4 */
    0xe0701008, 0x8001070c, /* buffer_store_dword v7, v12, s[4:7], 0 offen offset:8 */
    0xe070100c, 0x8001080c, /* buffer_store_dword v8, v12, s[4:7], 0 offen offset:12 */
    0xe0701010, 0x80010a0c, /* buffer_store_dword v10, v12, s[4:7], 0 offen offset:16 */
    0xe0701014, 0x80010b0c, /* buffer_store_dword v11, v12, s[4:7], 0 offen offset:20 */
    0xbf810000, /* s_endpgm */
};

/* Constant buffer (a, b, c, d); per lane i, four dwords at i * 16 bytes: v = a * i - b, max(|v|, c),
   floor(v), d. */
static const uint32_t constants_and_modifiers[] = {
    0xc2840100, /* s_buffer_load_dwordx4 s[8:11], s[0:3], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e020d00, /* v_cvt_f32_u32_e32 v1, v0 */
    0x7e0e0209, /* v_mov_b32_e32 v7, s9 */
    0xd2820002, 0x841e0208, /* v_mad_f32 v2, s8, v1, -v7 */
    0xd2200103, 0x00001502, /* v_max_f32_e64 v3, |v2|, s10 */
    0x7e084902, /* v_floor_f32_e32 v4, v2 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0x340c0084, /* v_lshlrev_b32_e32 v6, 4, v0 */
    0xe0781000, 0x80010206, /* buffer_store_dwordx4 v[2:5], v6, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* An instruction the translator does not handle yet: translation stops naming it. */
static const uint32_t unsupported[] = {
    0xc8040002, /* v_interp_p1_f32 v1, v2, attr0.x */
    0xbf810000, /* s_endpgm */
};

/* Branches and EXEC. Offsets are dwords from the instruction after the branch, numbered in the
   comments. Per lane i: i < 20 ? 2 * i : 7 (s_and_saveexec narrows EXEC, s_cbranch_execz skips
   when no lane is left, EXEC is restored). */
static const uint32_t divergent_if[] = {
    0x7d880094, /*  0: v_cmp_gt_u32_e32 vcc, 20, v0 */
    0x7e020287, /*  1: v_mov_b32_e32 v1, 7 */
    0xbe88246a, /*  2: s_and_saveexec_b64 s[8:9], vcc */
    0xbf880001, /*  3: s_cbranch_execz 1 */
    0x34020081, /*  4: v_lshlrev_b32_e32 v1, 1, v0 */
    0xbefe0408, /*  5: s_mov_b64 exec, s[8:9] */
    0x34040082, /*  6: v_lshlrev_b32_e32 v2, 2, v0 */
    0xe0701000, 0x80010102, /*  7: buffer_store_dword v1, v2, s[4:7], 0 offen */
    0xbf810000, /*  9: s_endpgm */
};

/* A scalar loop: s9 = 0 + 1 + ... + 9 = 45 in every lane (SCC from s_cmp, a backward branch). */
static const uint32_t uniform_loop[] = {
    0xbe880380, /*  0: s_mov_b32 s8, 0 */
    0xbe890380, /*  1: s_mov_b32 s9, 0 */
    0x80090809, /*  2: s_add_u32 s9, s9, s8 */
    0x80088108, /*  3: s_add_u32 s8, s8, 1 */
    0xbf0a8a08, /*  4: s_cmp_lt_u32 s8, 10 */
    0xbf85fffc, /*  5: s_cbranch_scc1 -4 */
    0x7e020209, /*  6: v_mov_b32_e32 v1, s9 */
    0x34040082, /*  7: v_lshlrev_b32_e32 v2, 2, v0 */
    0xe0701000, 0x80010102, /*  8: buffer_store_dword v1, v2, s[4:7], 0 offen */
    0xbf810000, /* 10: s_endpgm */
};

/* Each lane loops (i & 3) times adding 10: EXEC narrows every iteration until s_cbranch_execz
   leaves; result 10 * (i & 3). */
static const uint32_t vector_loop[] = {
    0x36020083, /*  0: v_and_b32_e32 v1, 3, v0 */
    0x7e040280, /*  1: v_mov_b32_e32 v2, 0 */
    0x7e060280, /*  2: v_mov_b32_e32 v3, 0 */
    0xbe8a047e, /*  3: s_mov_b64 s[10:11], exec */
    0x7d820302, /*  4: v_cmp_lt_u32_e32 vcc, v2, v1 */
    0x87fe6a7e, /*  5: s_and_b64 exec, exec, vcc */
    0xbf880003, /*  6: s_cbranch_execz 3 */
    0x4a06068a, /*  7: v_add_i32_e32 v3, vcc, 10, v3 */
    0x4a040481, /*  8: v_add_i32_e32 v2, vcc, 1, v2 */
    0xbf82fffa, /*  9: s_branch -6 */
    0xbefe040a, /* 10: s_mov_b64 exec, s[10:11] */
    0x34080082, /* 11: v_lshlrev_b32_e32 v4, 2, v0 */
    0xe0701000, 0x80010304, /* 12: buffer_store_dword v3, v4, s[4:7], 0 offen */
    0xbf810000, /* 14: s_endpgm */
};

/* v_cmpx (EXEC from a compare), s_cselect on SCC, and s_cbranch_vccnz taken (a lane in every
   group of 32 has (i & 31) == 0) and not taken (no lane has i == 64). Two dwords per lane i:
   (i < 8 ? 1 : 0) + 64, then 5. */
static const uint32_t compare_exec[] = {
    0x7e020280, /*  0: v_mov_b32_e32 v1, 0 */
    0xbe8a047e, /*  1: s_mov_b64 s[10:11], exec */
    0x7da80088, /*  2: v_cmpx_gt_u32_e32 vcc, 8, v0 */
    0x7e020281, /*  3: v_mov_b32_e32 v1, 1 */
    0xbefe040a, /*  4: s_mov_b64 exec, s[10:11] */
    0xbf068080, /*  5: s_cmp_eq_u32 0, 0 */
    0x8508a0c0, /*  6: s_cselect_b32 s8, 64, 32 */
    0x4a020208, /*  7: v_add_i32_e32 v1, vcc, s8, v1 */
    0x360a009f, /*  8: v_and_b32_e32 v5, 31, v0 */
    0x7d840a80, /*  9: v_cmp_eq_u32_e32 vcc, 0, v5 */
    0xbf870002, /* 10: s_cbranch_vccnz 2 */
    0x7e0202ff, 0x000003e7, /* 11: v_mov_b32_e32 v1, 0x3e7 */
    0x7e0c0280, /* 13: v_mov_b32_e32 v6, 0 */
    0x7d8400c0, /* 14: v_cmp_eq_u32_e32 vcc, 64, v0 */
    0xbf870001, /* 15: s_cbranch_vccnz 1 */
    0x7e0c0285, /* 16: v_mov_b32_e32 v6, 5 */
    0x34040083, /* 17: v_lshlrev_b32_e32 v2, 3, v0 */
    0xe0701000, 0x80010102, /* 18: buffer_store_dword v1, v2, s[4:7], 0 offen */
    0xe0701004, 0x80010602, /* 20: buffer_store_dword v6, v2, s[4:7], 0 offen offset:4 */
    0xbf810000, /* 22: s_endpgm */
};

/* A store while only some lanes are active: lanes i < 20 write 0x55, the others leave memory as
   it was. */
static const uint32_t masked_store[] = {
    0x34040082, /*  0: v_lshlrev_b32_e32 v2, 2, v0 */
    0x7e0202ff, 0x00000055, /*  1: v_mov_b32_e32 v1, 0x55 */
    0x7d880094, /*  3: v_cmp_gt_u32_e32 vcc, 20, v0 */
    0xbe88246a, /*  4: s_and_saveexec_b64 s[8:9], vcc */
    0xe0701000, 0x80010102, /*  5: buffer_store_dword v1, v2, s[4:7], 0 offen */
    0xbefe0408, /*  7: s_mov_b64 exec, s[8:9] */
    0xbf810000, /*  8: s_endpgm */
};

#endif
