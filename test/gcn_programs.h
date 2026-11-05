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

#endif
