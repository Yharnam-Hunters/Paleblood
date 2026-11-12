/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Synthetic GCN (GFX7) compute, vertex and pixel programs for the shader translator's tests. Each encoding was
   produced by LLVM's AMDGPU assembler (llvm-mc -triple=amdgcn -mcpu=bonaire --show-encoding) from
   the instruction in its comment. No game code is used.

   User data, as the tests pass it: s[0:3] the input buffer's descriptor (or a constant buffer's),
   s[4:7] the output buffer's. In compute programs v0 is the lane's index; in vertex programs the
   vertex index; in pixel programs v0 and v1 are the barycentrics. */
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

/* Each lane of workgroup (x, y, z) of a 2 x 3 x 4 dispatch stores x | y << 8 | z << 16 at byte
   ((z * 3 + y) * 2 + x) * 256 + 4 * lane of the buffer in s[4:7]: the workgroup's indexes start in
   s8, s9 and s10, after 8 user SGPRs (TGID_X/Y/Z_EN all set). */
static const uint32_t workgroup_indexes[] = {
    0x930b830a, /* s_mul_i32 s11, s10, 3 */
    0x800b090b, /* s_add_u32 s11, s11, s9 */
    0x8f0b810b, /* s_lshl_b32 s11, s11, 1 */
    0x800b080b, /* s_add_u32 s11, s11, s8 */
    0x8f0b880b, /* s_lshl_b32 s11, s11, 8 */
    0x8f0c8809, /* s_lshl_b32 s12, s9, 8 */
    0x8f0d900a, /* s_lshl_b32 s13, s10, 16 */
    0x880c0d0c, /* s_or_b32 s12, s12, s13 */
    0x880c080c, /* s_or_b32 s12, s12, s8 */
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0x4a02020b, /* v_add_i32_e32 v1, vcc, s11, v1 */
    0x7e04020c, /* v_mov_b32_e32 v2, s12 */
    0xe0701000, 0x80010201, /* buffer_store_dword v2, v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* out[i] = v_rcp_iflag_f32(in[i]), out[64 + i] = v_rcp_f32(in[i]). */
static const uint32_t reciprocals[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0301000, 0x80000201, /* buffer_load_dword v2, v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e065702, /* v_rcp_iflag_f32_e32 v3, v2 */
    0x7e085502, /* v_rcp_f32_e32 v4, v2 */
    0xe0701000, 0x80010301, /* buffer_store_dword v3, v1, s[4:7], 0 offen */
    0xe0701100, 0x80010401, /* buffer_store_dword v4, v1, s[4:7], 0 offen offset:256 */
    0xbf810000, /* s_endpgm */
};

/* out[i] = in[i] * 2.5 + 1 as scale_add, the load indexed by the lane (idxen, the descriptor's
   stride) with instruction offset `offset`: the range check's indexed form. */
#define INDEXED_SCALE_ADD(offset) {                                                       \
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */                                         \
    0xe0302000 | (offset), 0x80000200, /* buffer_load_dword v2, v0, s[0:3], 0 idxen */  \
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */                                                  \
    0x7e0602f2, /* v_mov_b32_e32 v3, 1.0 */                                               \
    0x3e0604ff, 0x40200000, /* v_mac_f32_e32 v3, 0x40200000, v2 */                        \
    0xe0701000, 0x80010301, /* buffer_store_dword v3, v1, s[4:7], 0 offen */              \
    0xbf810000, /* s_endpgm */                                                            \
}
static const uint32_t indexed_scale_add[] = INDEXED_SCALE_ADD(0);
static const uint32_t indexed_scale_add_past_stride[] = INDEXED_SCALE_ADD(4);

/* A table-driven copy (gcn_table_copy_shape): user data s[0:3] the table of (x, y, count), three
   dwords per workgroup, s[4:7] the source, s[8:11] the destination, s12 the workgroup's x index; lane
   i <= count copies source element y + i to destination element x + i (both indexed, idxen). The
   table loads with `table_load` (buffer_load_dwordx3, 0xe03c2000, or buffer_load_format_xyz,
   0xe0082000), the store with instruction offset `store_offset`. */
#define TABLE_COPY(table_load, store_offset) {                                                   \
    0x7e02020c, /* v_mov_b32_e32 v1, s12 */                                                     \
    (table_load), 0x80000401, /* buffer_load_dwordx3 v[4:6], v1, s[0:3], 0 idxen */           \
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */                                                        \
    0x7d8c0106, /* v_cmp_ge_u32_e32 vcc, v6, v0 */                                              \
    0xbe8e246a, /* s_and_saveexec_b64 s[14:15], vcc */                                          \
    0xbf880007, /* s_cbranch_execz 7 (to s_endpgm) */                                           \
    0x4a0e0b00, /* v_add_i32_e32 v7, vcc, v0, v5 */                                             \
    0xe0302000, 0x80010807, /* buffer_load_dword v8, v7, s[4:7], 0 idxen */                    \
    0x4a120900, /* v_add_i32_e32 v9, vcc, v0, v4 */                                             \
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */                                                        \
    0xe0702000 | (store_offset), 0x80020809, /* buffer_store_dword v8, v9, s[8:11], 0 idxen */ \
    0xbf810000, /* s_endpgm */                                                                  \
}
static const uint32_t table_copy[] = TABLE_COPY(0xe03c2000, 0);
static const uint32_t table_copy_format[] = TABLE_COPY(0xe0082000, 0);
static const uint32_t table_copy_store_offset[] = TABLE_COPY(0xe03c2000, 4);

/* out[i] = v_min_i32(-3, in[i]), out[64 + i] = v_max_i32(-3, in[i]), out[128 + i] = v_min_u32(7, in[i]),
   out[192 + i] = v_max_u32(7, in[i]). */
static const uint32_t integer_min_max[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0301000, 0x80000201, /* buffer_load_dword v2, v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x220604c3, /* v_min_i32_e32 v3, -3, v2 */
    0x240804c3, /* v_max_i32_e32 v4, -3, v2 */
    0x260a0487, /* v_min_u32_e32 v5, 7, v2 */
    0x280c0487, /* v_max_u32_e32 v6, 7, v2 */
    0xe0701000, 0x80010301, /* buffer_store_dword v3, v1, s[4:7], 0 offen */
    0xe0701100, 0x80010401, /* buffer_store_dword v4, v1, s[4:7], 0 offen offset:256 */
    0xe0701200, 0x80010501, /* buffer_store_dword v5, v1, s[4:7], 0 offen offset:512 */
    0xe0701300, 0x80010601, /* buffer_store_dword v6, v1, s[4:7], 0 offen offset:768 */
    0xbf810000, /* s_endpgm */
};

/* out[i] = v_mul_hi_u32(in[i], in[i]), out[64 + i] = v_mul_hi_i32(in[i], in[i]),
   out[128 + i] = v_mul_hi_i32(in[i], 4 * i). */
static const uint32_t multiply_high[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0301000, 0x80000201, /* buffer_load_dword v2, v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xd2d40003, 0x00020502, /* v_mul_hi_u32 v3, v2, v2 */
    0xd2d80004, 0x00020502, /* v_mul_hi_i32 v4, v2, v2 */
    0xd2d80005, 0x00020302, /* v_mul_hi_i32 v5, v2, v1 */
    0xe0701000, 0x80010301, /* buffer_store_dword v3, v1, s[4:7], 0 offen */
    0xe0701100, 0x80010401, /* buffer_store_dword v4, v1, s[4:7], 0 offen offset:256 */
    0xe0701200, 0x80010501, /* buffer_store_dword v5, v1, s[4:7], 0 offen offset:512 */
    0xbf810000, /* s_endpgm */
};

/* Per lane i with x = in[i]: out[i], out[64 + i], out[128 + i] and out[192 + i] = x * x + x (v_mad_f32)
   with output modifiers 1, 2 and 3 (times 2, 4 and 0.5), and with modifier 1 and clamp. */
static const uint32_t output_modifiers[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0301000, 0x80000201, /* buffer_load_dword v2, v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xd2820003, 0x0c0a0502, /* v_mad_f32 v3, v2, v2, v2 mul:2 */
    0xd2820004, 0x140a0502, /* v_mad_f32 v4, v2, v2, v2 mul:4 */
    0xd2820005, 0x1c0a0502, /* v_mad_f32 v5, v2, v2, v2 div:2 */
    0xd2820806, 0x0c0a0502, /* v_mad_f32 v6, v2, v2, v2 clamp mul:2 */
    0xe0701000, 0x80010301, /* buffer_store_dword v3, v1, s[4:7], 0 offen */
    0xe0701100, 0x80010401, /* buffer_store_dword v4, v1, s[4:7], 0 offen offset:256 */
    0xe0701200, 0x80010501, /* buffer_store_dword v5, v1, s[4:7], 0 offen offset:512 */
    0xe0701300, 0x80010601, /* buffer_store_dword v6, v1, s[4:7], 0 offen offset:768 */
    0xbf810000, /* s_endpgm */
};


/* Per lane i, the direction (x, y, z) at in[3i .. 3i + 2]: out[i], out[64 + i], out[128 + i] and
   out[192 + i] = its cube face id, s, t and major axis (v_cubeid/sc/tc/ma_f32). */
static const uint32_t cube_faces[] = {
    0x1602008c, /* v_mul_u32_u24_e32 v1, 12, v0 */
    0xe0301000, 0x80000201, /* buffer_load_dword v2, v1, s[0:3], 0 offen */
    0xe0301004, 0x80000301, /* buffer_load_dword v3, v1, s[0:3], 0 offen offset:4 */
    0xe0301008, 0x80000401, /* buffer_load_dword v4, v1, s[0:3], 0 offen offset:8 */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xd2880005, 0x04120702, /* v_cubeid_f32 v5, v2, v3, v4 */
    0xd28a0006, 0x04120702, /* v_cubesc_f32 v6, v2, v3, v4 */
    0xd28c0007, 0x04120702, /* v_cubetc_f32 v7, v2, v3, v4 */
    0xd28e0008, 0x04120702, /* v_cubema_f32 v8, v2, v3, v4 */
    0x34120082, /* v_lshlrev_b32_e32 v9, 2, v0 */
    0xe0701000, 0x80010509, /* buffer_store_dword v5, v9, s[4:7], 0 offen */
    0xe0701100, 0x80010609, /* buffer_store_dword v6, v9, s[4:7], 0 offen offset:256 */
    0xe0701200, 0x80010709, /* buffer_store_dword v7, v9, s[4:7], 0 offen offset:512 */
    0xe0701300, 0x80010809, /* buffer_store_dword v8, v9, s[4:7], 0 offen offset:768 */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, out[4i .. 4i + 3] = the four dwords s_buffer_load_dwordx4 reads from the buffer in s[0:3]. */
static const uint32_t scalar_buffer_quad[] = {
    0xc2840100, /* s_buffer_load_dwordx4 s[8:11], s[0:3], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040208, /* v_mov_b32_e32 v2, s8 */
    0x7e060209, /* v_mov_b32_e32 v3, s9 */
    0x7e08020a, /* v_mov_b32_e32 v4, s10 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0x34020084, /* v_lshlrev_b32_e32 v1, 4, v0 */
    0xe0781000, 0x80010201, /* buffer_store_dwordx4 v[2:5], v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* scalar_buffer_quad from dword 2 of the buffer: a literal offset (OFFSET 255, the next word, in dwords). */
static const uint32_t scalar_buffer_quad_literal[] = {
    0xc28400ff, 0x00000002, /* s_buffer_load_dwordx4 s[8:11], s[0:3], 2 (literal) */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040208, /* v_mov_b32_e32 v2, s8 */
    0x7e060209, /* v_mov_b32_e32 v3, s9 */
    0x7e08020a, /* v_mov_b32_e32 v4, s10 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0x34020084, /* v_lshlrev_b32_e32 v1, 4, v0 */
    0xe0781000, 0x80010201, /* buffer_store_dwordx4 v[2:5], v1, s[4:7], 0 offen */
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

/* Typed loads and stores of 32-bit float data, indexed by lane (idxen, the descriptors' strides:
   16 bytes in, 48 out). Per lane i, from in record i (x, y, z, w): out record i holds x, y, z, w,
   then y, then z, w, then x, y, z. */
static const uint32_t typed_buffers[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xeba02004, 0x80000500, /* tbuffer_load_format_x v5, v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32,BUF_NUM_FORMAT_FLOAT] idxen offset:4 */
    0xebd92008, 0x80000600, /* tbuffer_load_format_xy v[6:7], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32,BUF_NUM_FORMAT_FLOAT] idxen offset:8 */
    0xebea2000, 0x80000800, /* tbuffer_load_format_xyz v[8:10], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xebf72000, 0x80010100, /* tbuffer_store_format_xyzw v[1:4], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xeba42010, 0x80010500, /* tbuffer_store_format_x v5, v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32,BUF_NUM_FORMAT_FLOAT] idxen offset:16 */
    0xebdd2014, 0x80010600, /* tbuffer_store_format_xy v[6:7], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32,BUF_NUM_FORMAT_FLOAT] idxen offset:20 */
    0xebee201c, 0x80010800, /* tbuffer_store_format_xyz v[8:10], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen offset:28 */
    0xbf810000, /* s_endpgm */
};

/* Typed loads the translator refuses: a converted format (8_8_8_8 unorm), and more components
   than the format has. */
static const uint32_t typed_unorm[] = {
    0xe8532000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_8_8_8_8] idxen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t typed_too_many_components[] = {
    0xeba12000, 0x80000100, /* tbuffer_load_format_xy v[1:2], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, from in[2i] and in[2i + 1] (a, b): out[i] = the halves of a (low) and b, rounded
   toward zero; out[64 + i] = the same with -a. */
static const uint32_t pack_halves[] = {
    0x34020083, /* v_lshlrev_b32_e32 v1, 3, v0 */
    0xe0341000, 0x80000201, /* buffer_load_dwordx2 v[2:3], v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x5e080702, /* v_cvt_pkrtz_f16_f32_e32 v4, v2, v3 */
    0xd25e0006, 0x20020702, /* v_cvt_pkrtz_f16_f32_e64 v6, -v2, v3 */
    0x340a0082, /* v_lshlrev_b32_e32 v5, 2, v0 */
    0xe0701000, 0x80010405, /* buffer_store_dword v4, v5, s[4:7], 0 offen */
    0xe0701100, 0x80010605, /* buffer_store_dword v6, v5, s[4:7], 0 offen offset:256 */
    0xbf810000, /* s_endpgm */
};

/* The negated float compares: per lane i, the floats (a, b) at in[2i], in[2i + 1]; each of nge, nlg, ngt
   and nle stores 1 where it holds, 0 elsewhere, at out[8i .. 8i + 3]; neq and nlt at out[8i + 4], out[8i + 5];
   then v_cmpx_ngt leaves active only the lanes where ngt holds, which store (neq, nlt, 1, 0) at
   out[8i + 4 .. 8i + 7] (the others store nothing there). */
static const uint32_t negated_compares[] = {
    0x34020083, /* v_lshlrev_b32_e32 v1, 3, v0 */
    0xe0341000, 0x80000201, /* buffer_load_dwordx2 v[2:3], v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34020085, /* v_lshlrev_b32_e32 v1, 5, v0 */
    0x7c120702, /* v_cmp_nge_f32_e32 vcc, v2, v3 */
    0xd2000004, 0x01a90280, /* v_cndmask_b32_e64 v4, 0, 1, vcc */
    0x7c140702, /* v_cmp_nlg_f32_e32 vcc, v2, v3 */
    0xd2000005, 0x01a90280, /* v_cndmask_b32_e64 v5, 0, 1, vcc */
    0x7c160702, /* v_cmp_ngt_f32_e32 vcc, v2, v3 */
    0xd2000006, 0x01a90280, /* v_cndmask_b32_e64 v6, 0, 1, vcc */
    0x7c180702, /* v_cmp_nle_f32_e32 vcc, v2, v3 */
    0xd2000007, 0x01a90280, /* v_cndmask_b32_e64 v7, 0, 1, vcc */
    0xe0781000, 0x80010401, /* buffer_store_dwordx4 v[4:7], v1, s[4:7], 0 offen */
    0x7c1a0702, /* v_cmp_neq_f32_e32 vcc, v2, v3 */
    0xd2000004, 0x01a90280, /* v_cndmask_b32_e64 v4, 0, 1, vcc */
    0x7c1c0702, /* v_cmp_nlt_f32_e32 vcc, v2, v3 */
    0xd2000005, 0x01a90280, /* v_cndmask_b32_e64 v5, 0, 1, vcc */
    0x7c360702, /* v_cmpx_ngt_f32_e32 vcc, v2, v3 */
    0x7e0c0281, /* v_mov_b32_e32 v6, 1 */
    0x7e0e0280, /* v_mov_b32_e32 v7, 0 */
    0xe0781010, 0x80010401, /* buffer_store_dwordx4 v[4:7], v1, s[4:7], 0 offen offset:16 */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, a one-component format store of i at i * 4 bytes (buffer_store_format_x, the
   descriptor's format). */
/* Per lane i, (i, i + 1, i + 2, i + 3) stored with one buffer_store_dwordx4 at i * 16 bytes plus the
   instruction offset `offset`: a multi-dword store, at an address that is a multiple of its 16 bytes
   (offset 0) or not (offset 4). */
#define STORE_DWORDX4(offset) {                                                                  \
    0x34020084, /* v_lshlrev_b32_e32 v1, 4, v0 */                                                \
    0x7e080300, /* v_mov_b32_e32 v4, v0 */                                                       \
    0x4a0a0081, /* v_add_i32_e32 v5, vcc, 1, v0 */                                               \
    0x4a0c0082, /* v_add_i32_e32 v6, vcc, 2, v0 */                                               \
    0x4a0e0083, /* v_add_i32_e32 v7, vcc, 3, v0 */                                               \
    0xe0781000 | (offset), 0x80010401, /* buffer_store_dwordx4 v[4:7], v1, s[4:7], 0 offen offset:... */ \
    0xbf810000, /* s_endpgm */                                                                   \
}
static const uint32_t store_dwordx4_aligned[] = STORE_DWORDX4(0);
static const uint32_t store_dwordx4_unaligned[] = STORE_DWORDX4(4);

static const uint32_t store_format_x[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0101000, 0x80010001, /* buffer_store_format_x v0, v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, a format store of (i, 1.0, 2.0, 0.5) at i * 16 bytes: buffer_store_format_xyzw, whose
   format is the descriptor's (s[4:7]). */
static const uint32_t store_format_xyzw[] = {
    0x34020084, /* v_lshlrev_b32_e32 v1, 4, v0 */
    0x7e080300, /* v_mov_b32_e32 v4, v0 */
    0x7e0a02f2, /* v_mov_b32_e32 v5, 1.0 */
    0x7e0c02f4, /* v_mov_b32_e32 v6, 2.0 */
    0x7e0e02f0, /* v_mov_b32_e32 v7, 0.5 */
    0xe01c1000, 0x80010401, /* buffer_store_format_xyzw v[4:7], v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane, in[4i .. 4i + 3] stored with buffer_store_format_xyzw at lane * 16 bytes (the descriptor's
   format). */
static const uint32_t store_format_loaded[] = {
    0x34020084, /* v_lshlrev_b32_e32 v1, 4, v0 */
    0xe0381000, 0x80000401, /* buffer_load_dwordx4 v[4:7], v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe01c1000, 0x80010401, /* buffer_store_format_xyzw v[4:7], v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane, a format store of (0, -1.0, 1.0, 2.0) at lane * 16 bytes (buffer_store_format_xyzw,
   the descriptor's format): as 8_8_8_8 unorm those clamp to 0, 0, 255, 255. */
static const uint32_t store_format_packed[] = {
    0x34020084, /* v_lshlrev_b32_e32 v1, 4, v0 */
    0x7e080280, /* v_mov_b32_e32 v4, 0 */
    0x7e0a02f3, /* v_mov_b32_e32 v5, -1.0 */
    0x7e0c02f2, /* v_mov_b32_e32 v6, 1.0 */
    0x7e0e02f4, /* v_mov_b32_e32 v7, 2.0 */
    0xe01c1000, 0x80010401, /* buffer_store_format_xyzw v[4:7], v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, texel (i, 0) of the image in s[0:7] copied to the image in s[8:15]: image_load then
   image_store, all four components. */
static const uint32_t image_copy[] = {
    0x7e020300, /* v_mov_b32_e32 v1, v0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0xf0000f00, 0x00000401, /* image_load v[4:7], v[1:2], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf0200f00, 0x00020401, /* image_store v[4:7], v[1:2], s[8:15] dmask:0xf */
    0xbf810000, /* s_endpgm */
};

/* image_copy through image_load_mip at level `level` (in v3, after the coordinates). */
#define IMAGE_COPY_MIP(level) {                                                     \
    0x7e020300,             /* v_mov_b32_e32 v1, v0 */                              \
    0x7e040280,             /* v_mov_b32_e32 v2, 0 */                               \
    0x7e060280 + (level),   /* v_mov_b32_e32 v3, level */                           \
    0xf0040f00, 0x00000401, /* image_load_mip v[4:7], v[1:3], s[0:7] dmask:0xf */   \
    0xbf8c0f70,             /* s_waitcnt vmcnt(0) */                                \
    0xf0200f00, 0x00020401, /* image_store v[4:7], v[1:2], s[8:15] dmask:0xf */     \
    0xbf810000,             /* s_endpgm */                                          \
}
static const uint32_t image_copy_level_0[] = IMAGE_COPY_MIP(0);
static const uint32_t image_copy_level_1[] = IMAGE_COPY_MIP(1);

/* Per lane i, texel (i, 0) of the image in s[8:15] set to (float(i), 1.0, 2.0, 0.5). */
static const uint32_t image_fill[] = {
    0x7e080d00, /* v_cvt_f32_u32_e32 v4, v0 */
    0x7e0a02f2, /* v_mov_b32_e32 v5, 1.0 */
    0x7e0c02f4, /* v_mov_b32_e32 v6, 2.0 */
    0x7e0e02f0, /* v_mov_b32_e32 v7, 0.5 */
    0x7e020300, /* v_mov_b32_e32 v1, v0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0xf0200f00, 0x00020401, /* image_store v[4:7], v[1:2], s[8:15] dmask:0xf */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, texel (i, 0, 1) of the 3D image in s[8:15] (slice 1) set to (float(i), 1.0, 2.0, 0.5). */
static const uint32_t image_fill_3d[] = {
    0x7e080d00, /* v_cvt_f32_u32_e32 v4, v0 */
    0x7e0a02f2, /* v_mov_b32_e32 v5, 1.0 */
    0x7e0c02f4, /* v_mov_b32_e32 v6, 2.0 */
    0x7e0e02f0, /* v_mov_b32_e32 v7, 0.5 */
    0x7e020300, /* v_mov_b32_e32 v1, v0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060281, /* v_mov_b32_e32 v3, 1 */
    0xf0200f00, 0x00020401, /* image_store v[4:7], v[1:3], s[8:15] dmask:0xf */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, texel (i, 0, z) of the 3D image in s[8:15] stored as four dwords at i * 16 bytes of the
   buffer in s[0:3]; z is 1 here, 0 in image_to_buffer_3d_slice0. */
static const uint32_t image_to_buffer_3d[] = {
    0x7e020300, /* v_mov_b32_e32 v1, v0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060281, /* v_mov_b32_e32 v3, 1 */
    0xf0000f00, 0x00020401, /* image_load v[4:7], v[1:3], s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060084, /* v_lshlrev_b32_e32 v3, 4, v0 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_to_buffer_3d_slice0[] = {
    0x7e020300, /* v_mov_b32_e32 v1, v0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00020401, /* image_load v[4:7], v[1:3], s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060084, /* v_lshlrev_b32_e32 v3, 4, v0 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, texel (i, 0) of the image in s[8:15] gets float(i) as the shader's w component alone
   (DMASK 8): with DST_SEL (0, 0, 0, x) it lands in a one-component image's x. */
static const uint32_t image_fill_alpha[] = {
    0x7e080d00, /* v_cvt_f32_u32_e32 v4, v0 */
    0x7e020300, /* v_mov_b32_e32 v1, v0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0xf0200800, 0x00020401, /* image_store v4, v[1:2], s[8:15] dmask:0x8 */
    0xbf810000, /* s_endpgm */
};

/* A clear: every lane of a 16 x 16 group stores (0.25, 0.5, 0.75, 1) at its (x, y), v0 and v1, of the
   image in s[8:15]. */
static const uint32_t image_clear_tile[] = {
    0x7e0802ff, 0x3e800000, /* v_mov_b32_e32 v4, 0.25 */
    0x7e0a02f0, /* v_mov_b32_e32 v5, 0.5 */
    0x7e0c02ff, 0x3f400000, /* v_mov_b32_e32 v6, 0.75 */
    0x7e0e02f2, /* v_mov_b32_e32 v7, 1.0 */
    0xf0200f00, 0x00020400, /* image_store v[4:7], v[0:1], s[8:15] dmask:0xf */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, texel (i, 0) of the image in s[8:15] stored as four dwords at i * 16 bytes of the
   buffer in s[0:3]. */
static const uint32_t image_to_buffer[] = {
    0x7e020300, /* v_mov_b32_e32 v1, v0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0xf0000f00, 0x00020401, /* image_load v[4:7], v[1:2], s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060084, /* v_lshlrev_b32_e32 v3, 4, v0 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, a format load of record i (4 bytes each) from the buffer in s[0:3], whose format and
   swizzle are its descriptor's, stored as four dwords at i * 16 bytes of the buffer in s[4:7]. */
static const uint32_t load_format_xyzw[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe00c1000, 0x80000401, /* buffer_load_format_xyzw v[4:7], v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060084, /* v_lshlrev_b32_e32 v3, 4, v0 */
    0xe0781000, 0x80010403, /* buffer_store_dwordx4 v[4:7], v3, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, a format load of record i (the descriptor's stride apart, so any byte alignment) from
   the buffer in s[0:3], stored as four dwords at i * 16 bytes of the buffer in s[4:7]. */
static const uint32_t load_format_indexed[] = {
    0xe00c2000, 0x80000400, /* buffer_load_format_xyzw v[4:7], v0, s[0:3], 0 idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060084, /* v_lshlrev_b32_e32 v3, 4, v0 */
    0xe0781000, 0x80010403, /* buffer_store_dwordx4 v[4:7], v3, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Whole quad mode: v_cmpx leaves lane i active when i % 4 == 1, s_wqm_b64 then activates every lane of
   each quad with an active lane, so all 64 store 1. */
static const uint32_t whole_quad[] = {
    0x36020083, /* v_and_b32_e32 v1, 3, v0 */
    0x7da40281, /* v_cmpx_eq_u32_e32 vcc, 1, v1 */
    0xbefe0a7e, /* s_wqm_b64 exec, exec */
    0x7e040281, /* v_mov_b32_e32 v2, 1 */
    0x34060082, /* v_lshlrev_b32_e32 v3, 2, v0 */
    0xe0701000, 0x80010203, /* buffer_store_dword v2, v3, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* Scalar writes of VCC's halves: VCC_LO = 0x0000ffff, VCC_HI = 1, then per lane i (in a 64-lane
   workgroup) out[i] = VCC bit ? 1.0 : 0, so lanes 0-15 and 32 write 1.0. */
static const uint32_t vcc_halves[] = {
    0xbeea03ff, 0x0000ffff, /* s_mov_b32 vcc_lo, 0xffff */
    0xbeeb0381, /* s_mov_b32 vcc_hi, 1 */
    0x7e0602f2, /* v_mov_b32_e32 v3, 1.0 */
    0x00040680, /* v_cndmask_b32_e32 v2, 0, v3, vcc */
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0701000, 0x80010201, /* buffer_store_dword v2, v1, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};

/* A vertex shader: vertex i's position and colour from 16-byte records (s[0:3] positions, s[4:7]
   colours, by index); the colour goes out as parameter 0. */
static const uint32_t vertex_position_color[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};

/* As vertex_position_color, but the position also goes out as parameter 0 and the colour as parameter 1. */
static const uint32_t vertex_colour_second[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800020f, 0x04030201, /* exp param0, v1, v2, v3, v4 */
    0xf800021f, 0x08070605, /* exp param1, v5, v6, v7, v8 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};

/* As vertex_position_color, through a fetch shader as the game's vertex shaders do: the program calls
   the code at the address in s[0:1] (s_swappc_b64, the return address into s[0:1]), which loads the two
   buffer descriptors from the table at the pointer in s[2:3] and the vertex's position and colour by
   index (buffer_load_format_xyzw: the descriptors' formats), then returns (s_setpc_b64). */
static const uint32_t vertex_calling_fetch[] = {
    0xbe802100, /* s_swappc_b64 s[0:1], s[0:1] */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};

static const uint32_t fetch_position_color[] = {
    0xc0820300, /* s_load_dwordx4 s[4:7], s[2:3], 0x0 */
    0xc0840304, /* s_load_dwordx4 s[8:11], s[2:3], 0x4 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xe00c2000, 0x80010100, /* buffer_load_format_xyzw v[1:4], v0, s[4:7], 0 idxen */
    0xe00c2000, 0x80020500, /* buffer_load_format_xyzw v[5:8], v0, s[8:11], 0 idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbe802000, /* s_setpc_b64 s[0:1] */
};

/* A vertex shader that calls its fetch shader (fetch_position_only writes s[4:7]), then loads a
   texel at (0, 0), level 0, of the image whose descriptor is in its user data's s[8:15], and exports
   it as parameter 0. */
static const uint32_t vertex_fetch_then_image[] = {
    0xbe802100,             /* s_swappc_b64 s[0:1], s[0:1] */
    0x7e1a0280,             /* v_mov_b32 v13, 0 */
    0x7e1c0280,             /* v_mov_b32 v14, 0 */
    0x7e1e0280,             /* v_mov_b32 v15, 0 */
    0xf0040f00, 0x0002090d, /* image_load_mip v[9:12], v[13:15], s[8:15] dmask 0xf */
    0xbf8c0f70,             /* s_waitcnt vmcnt(0) */
    0xf800020f, 0x0c0b0a09, /* exp param0, v9, v10, v11, v12 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000,             /* s_endpgm */
};

/* The position only: one vertex buffer, its descriptor loaded into s[4:7]. */
static const uint32_t fetch_position_only[] = {
    0xc0820300,             /* s_load_dwordx4 s[4:7], s[2:3], 0x0 */
    0xbf8c007f,             /* s_waitcnt lgkmcnt(0) */
    0xe00c2000, 0x80010100, /* buffer_load_format_xyzw v[1:4], v0, s[4:7], 0 idxen */
    0xbf8c0f70,             /* s_waitcnt vmcnt(0) */
    0xbe802000,             /* s_setpc_b64 s[0:1] */
};

/* A fetch shader that loads into s[8:11] too: the image's descriptor no longer survives the call. */
static const uint32_t fetch_into_image_sgprs[] = {
    0xc0840300, /* s_load_dwordx4 s[8:11], s[2:3], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xbe802000, /* s_setpc_b64 s[0:1] */
};

/* A pixel shader: attribute 0 interpolated (barycentrics in v0 and v1), packed as halves and
   written to colour target 0. */
static const uint32_t pixel_color[] = {
    0xbefc0300, /* s_mov_b32 m0, s0 */
    0xc8080000, /* v_interp_p1_f32 v2, v0, attr0.x */
    0xc8090001, /* v_interp_p2_f32 v2, v1, attr0.x */
    0xc80c0100, /* v_interp_p1_f32 v3, v0, attr0.y */
    0xc80d0101, /* v_interp_p2_f32 v3, v1, attr0.y */
    0xc8100200, /* v_interp_p1_f32 v4, v0, attr0.z */
    0xc8110201, /* v_interp_p2_f32 v4, v1, attr0.z */
    0xc8140300, /* v_interp_p1_f32 v5, v0, attr0.w */
    0xc8150301, /* v_interp_p2_f32 v5, v1, attr0.w */
    0x5e0c0702, /* v_cvt_pkrtz_f16_f32_e32 v6, v2, v3 */
    0x5e0e0b04, /* v_cvt_pkrtz_f16_f32_e32 v7, v4, v5 */
    0xf8001c0f, 0x00000706, /* exp mrt0, v6, v6, v7, v7 done compr vm */
    0xbf810000, /* s_endpgm */
};

/* Pixel shaders (SPI_PS_INPUT 0x1002: the front face in v2 after the barycentrics) colouring each pixel
   (face, 0.25, 0.25, 1): the face VGPR as a float, or (pixel_face_bits) converted from an unsigned integer. */
static const uint32_t pixel_face_float[] = {
    0x7e0602ff, 0x3e800000, /* v_mov_b32_e32 v3, 0.25 */
    0x7e0802f2, /* v_mov_b32_e32 v4, 1.0 */
    0xf800180f, 0x04030302, /* exp mrt0, v2, v3, v3, v4 done vm */
    0xbf810000, /* s_endpgm */
};
static const uint32_t pixel_face_bits[] = {
    0x7e040d02, /* v_cvt_f32_u32_e32 v2, v2 */
    0x7e0602ff, 0x3e800000, /* v_mov_b32_e32 v3, 0.25 */
    0x7e0802f2, /* v_mov_b32_e32 v4, 1.0 */
    0xf800180f, 0x04030302, /* exp mrt0, v2, v3, v3, v4 done vm */
    0xbf810000, /* s_endpgm */
};

/* pixel_color writing its colour to colour targets 0 and 2 (two exports, the second done). */
static const uint32_t pixel_color_two_targets[] = {
    0xbefc0300, /* s_mov_b32 m0, s0 */
    0xc8080000, /* v_interp_p1_f32 v2, v0, attr0.x */
    0xc8090001, /* v_interp_p2_f32 v2, v1, attr0.x */
    0xc80c0100, /* v_interp_p1_f32 v3, v0, attr0.y */
    0xc80d0101, /* v_interp_p2_f32 v3, v1, attr0.y */
    0xc8100200, /* v_interp_p1_f32 v4, v0, attr0.z */
    0xc8110201, /* v_interp_p2_f32 v4, v1, attr0.z */
    0xc8140300, /* v_interp_p1_f32 v5, v0, attr0.w */
    0xc8150301, /* v_interp_p2_f32 v5, v1, attr0.w */
    0x5e0c0702, /* v_cvt_pkrtz_f16_f32_e32 v6, v2, v3 */
    0x5e0e0b04, /* v_cvt_pkrtz_f16_f32_e32 v7, v4, v5 */
    0xf800140f, 0x00000706, /* exp mrt0, v6, v6, v7, v7 compr vm */
    0xf8001c2f, 0x00000706, /* exp mrt2, v6, v6, v7, v7 done compr vm */
    0xbf810000, /* s_endpgm */
};

/* A pixel shader whose colour comes from the pixel's position (x in v2, y in v3 after the barycentrics,
   SPI_PS_INPUT 0x302): (x / 16, y / 16, 0.25, 1), the same image as pixel_color on the test triangle. */
static const uint32_t pixel_position_color[] = {
    0x100804ff, 0x3d800000, /* v_mul_f32_e32 v4, 0.0625, v2 */
    0x100a06ff, 0x3d800000, /* v_mul_f32_e32 v5, 0.0625, v3 */
    0x7e0c02ff, 0x3e800000, /* v_mov_b32_e32 v6, 0.25 */
    0x7e0e02f2, /* v_mov_b32_e32 v7, 1.0 */
    0xf800180f, 0x07060504, /* exp mrt0, v4, v5, v6, v7 done vm */
    0xbf810000, /* s_endpgm */
};

/* A pixel shader (SPI_PS_INPUT 0x302: position x and y in v2 and v3) that colours each pixel with
   the image texel under it: image_load_mip at (int(x), int(y)), level 0, of the image in s[0:7]. */
static const uint32_t pixel_image_texel[] = {
    0x7e081102, /* v_cvt_i32_f32_e32 v4, v2 */
    0x7e0a1103, /* v_cvt_i32_f32_e32 v5, v3 */
    0x7e0c0280, /* v_mov_b32_e32 v6, 0 */
    0xf0040f00, 0x00000804, /* image_load_mip v[8:11], v[4:6], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800180f, 0x0b0a0908, /* exp mrt0, v8, v9, v10, v11 done vm */
    0xbf810000, /* s_endpgm */
};

/* As the game's fullscreen passes do: image_load_mip from the user data's image in s[4:11], then an
   s_load_dwordx8 over the same SGPRs of a descriptor 16 bytes past the pointer in s[2:3], and
   image_load_mip from that one; the pixel's colour is the two texels' sum. */
static const uint32_t pixel_two_images[] = {
    0x7e081102, /* v_cvt_i32_f32_e32 v4, v2 */
    0x7e0a1103, /* v_cvt_i32_f32_e32 v5, v3 */
    0x7e0c0280, /* v_mov_b32_e32 v6, 0 */
    0xf0040f00, 0x00010804, /* image_load_mip v[8:11], v[4:6], s[4:11] dmask:0xf */
    0xc0c20304, /* s_load_dwordx8 s[4:11], s[2:3], 0x4 */
    0xbf8c0070, /* s_waitcnt vmcnt(0) lgkmcnt(0) */
    0xf0040f00, 0x00010c04, /* image_load_mip v[12:15], v[4:6], s[4:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x06101908, /* v_add_f32_e32 v8, v8, v12 */
    0x06121b09, /* v_add_f32_e32 v9, v9, v13 */
    0x06141d0a, /* v_add_f32_e32 v10, v10, v14 */
    0x06161f0b, /* v_add_f32_e32 v11, v11, v15 */
    0xf800180f, 0x0b0a0908, /* exp mrt0, v8, v9, v10, v11 done vm */
    0xbf810000, /* s_endpgm */
};

/* A colour lookup, as the game's fullscreen passes do: the texel under the pixel from the user data's
   image in s[4:11], then each of red, green and blue through layer 0, 1 and 2 of a 1D array whose
   descriptor is loaded from the pointer in s[2:3] over s[0:7], sampled with the sampler in s[12:15];
   alpha as loaded. */
static const uint32_t pixel_lookup[] = {
    0x7e081102, /* v_cvt_i32_f32_e32 v4, v2 */
    0x7e0a1103, /* v_cvt_i32_f32_e32 v5, v3 */
    0x7e0c0280, /* v_mov_b32_e32 v6, 0 */
    0xf0040f00, 0x00010004, /* image_load_mip v[0:3], v[4:6], s[4:11] dmask:0xf */
    0xc0c00300, /* s_load_dwordx8 s[0:7], s[2:3], 0x0 */
    0xbf8c0070, /* s_waitcnt vmcnt(0) lgkmcnt(0) */
    0x7e080300, /* v_mov_b32_e32 v4, v0 */
    0x7e0a0280, /* v_mov_b32_e32 v5, 0 */
    0xf0804100, 0x00600004, /* image_sample v0, v[4:5], s[0:7], s[12:15] dmask:0x1 da */
    0x7e080301, /* v_mov_b32_e32 v4, v1 */
    0x7e0a02f2, /* v_mov_b32_e32 v5, 1.0 */
    0xf0804100, 0x00600104, /* image_sample v1, v[4:5], s[0:7], s[12:15] dmask:0x1 da */
    0x7e080302, /* v_mov_b32_e32 v4, v2 */
    0x7e0a02f4, /* v_mov_b32_e32 v5, 2.0 */
    0xf0804100, 0x00600204, /* image_sample v2, v[4:5], s[0:7], s[12:15] dmask:0x1 da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800180f, 0x03020100, /* exp mrt0, v0, v1, v2, v3 done vm */
    0xbf810000, /* s_endpgm */
};

/* The image in s[4:11] sampled through the sampler in s[12:15] at (x / 16, y / 16) of the pixel's
   position: the level its derivatives pick. */
static const uint32_t pixel_minified[] = {
    0x100804ff, 0x3d800000, /* v_mul_f32_e32 v4, 0x3d800000 (1/16), v2 */
    0x100a06ff, 0x3d800000, /* v_mul_f32_e32 v5, 0x3d800000 (1/16), v3 */
    0xf0800f00, 0x00610604, /* image_sample v[6:9], v[4:5], s[4:11], s[12:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800180f, 0x09080706, /* exp mrt0, v6, v7, v8, v9 done vm */
    0xbf810000, /* s_endpgm */
};

/* pixel_minified sampling at level 0 whatever the derivatives (image_sample_lz: no LOD VGPR). */
static const uint32_t pixel_level_zero[] = {
    0x100804ff, 0x3d800000, /* v_mul_f32_e32 v4, 0x3d800000 (1/16), v2 */
    0x100a06ff, 0x3d800000, /* v_mul_f32_e32 v5, 0x3d800000 (1/16), v3 */
    0xf09c0f00, 0x00610704, /* image_sample_lz v[7:10], v[4:5], s[4:11], s[12:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800180f, 0x0a090807, /* exp mrt0, v7, v8, v9, v10 done vm */
    0xbf810000, /* s_endpgm */
};

/* A depth compare at level 0: the reference 0.5 against the image in s[4:11] at (x / 16, y / 16), through
   the sampler in s[12:15] (its compare function and filter); the result in red, green and blue, alpha 1. */
static const uint32_t pixel_compared[] = {
    0x100804ff, 0x3d800000, /* v_mul_f32_e32 v4, 0x3d800000 (1/16), v2 */
    0x100a06ff, 0x3d800000, /* v_mul_f32_e32 v5, 0x3d800000 (1/16), v3 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0x7e1002f2, /* v_mov_b32_e32 v8, 1.0 */
    0xf0bc0100, 0x00610703, /* image_sample_c_lz v7, v[3:5], s[4:11], s[12:15] dmask:0x1 */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800180f, 0x08070707, /* exp mrt0, v7, v7, v7, v8 done vm */
    0xbf810000, /* s_endpgm */
};

/* pixel_minified sampling at level of detail 1 whatever the derivatives (image_sample_l). */
static const uint32_t pixel_explicit_level[] = {
    0x100804ff, 0x3d800000, /* v_mul_f32_e32 v4, 0x3d800000 (1/16), v2 */
    0x100a06ff, 0x3d800000, /* v_mul_f32_e32 v5, 0x3d800000 (1/16), v3 */
    0x7e0c02f2, /* v_mov_b32_e32 v6, 1.0 */
    0xf0900f00, 0x00610704, /* image_sample_l v[7:10], v[4:6], s[4:11], s[12:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800180f, 0x0a090807, /* exp mrt0, v7, v8, v9, v10 done vm */
    0xbf810000, /* s_endpgm */
};

/* Samples the cube in s[4:11] with the sampler in s[0:3] at (1.5, 1.5) on the face whose number (a
   float) is in s12, level 0 (image_sample_l, DA 0 as the game's shaders sample cubes). */
static const uint32_t pixel_cube_face[] = {
    0x7e0802ff, 0x3fc00000, /* v_mov_b32_e32 v4, 1.5 */
    0x7e0a02ff, 0x3fc00000, /* v_mov_b32_e32 v5, 1.5 */
    0x7e0c020c, /* v_mov_b32_e32 v6, s12 */
    0x7e0e0280, /* v_mov_b32_e32 v7, 0 */
    0xf0900f00, 0x00010804, /* image_sample_l v[8:11], v[4:7], s[4:11], s[0:3] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800180f, 0x0b0a0908, /* exp mrt0, v8, v9, v10, v11 done vm */
    0xbf810000, /* s_endpgm */
};

/* As pixel_color, but pixels whose red is below 0.5 are discarded (v_cmpx clears their EXEC bit
   and the export's valid mask drops them). */
static const uint32_t pixel_discard_left[] = {
    0xbefc0300, /* s_mov_b32 m0, s0 */
    0xc8080000, /* v_interp_p1_f32 v2, v0, attr0.x */
    0xc8090001, /* v_interp_p2_f32 v2, v1, attr0.x */
    0xc80c0100, /* v_interp_p1_f32 v3, v0, attr0.y */
    0xc80d0101, /* v_interp_p2_f32 v3, v1, attr0.y */
    0xc8100200, /* v_interp_p1_f32 v4, v0, attr0.z */
    0xc8110201, /* v_interp_p2_f32 v4, v1, attr0.z */
    0xc8140300, /* v_interp_p1_f32 v5, v0, attr0.w */
    0xc8150301, /* v_interp_p2_f32 v5, v1, attr0.w */
    0x7c2604f0, /* v_cmpx_le_f32_e32 vcc, 0.5, v2 */
    0x5e0c0702, /* v_cvt_pkrtz_f16_f32_e32 v6, v2, v3 */
    0x5e0e0b04, /* v_cvt_pkrtz_f16_f32_e32 v7, v4, v5 */
    0xf8001c0f, 0x00000706, /* exp mrt0, v6, v6, v7, v7 done compr vm */
    0xbf810000, /* s_endpgm */
};

/* Refused: a v_interp_p2 with no p1 before it. */
static const uint32_t pixel_p2_alone[] = {
    0xc8090001, /* v_interp_p2_f32 v2, v1, attr0.x */
    0xbf810000, /* s_endpgm */
};

/* Refused: barycentrics from v2 (not the layout's perspective-centre pair). */
static const uint32_t pixel_other_barycentrics[] = {
    0xc80c0002, /* v_interp_p1_f32 v3, v2, attr0.x */
    0xbf810000, /* s_endpgm */
};

/* Refused: pos1, whose meaning depends on GPU settings. */
static const uint32_t vertex_pos1[] = {
    0xf80008df, 0x04030201, /* exp pos1, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, from in record i (x, y, z, w; 16 bytes): out record i (64 bytes) holds
   v_mul_i32_i24 x, y; v_mul_u32_u24 x, y; v_mad_i32_i24 x, y, z; v_bfe_u32 and v_bfe_i32 of x at
   offset y, width z; v_bfm_b32 y, z; x + y + carry (carry in: w != 0) and its carry out; x - y -
   borrow (borrow in: w != 0) and its borrow out; v_mad_u32_u24 x, y, z. */
static const uint32_t integer_batch[] = {
    0x34020084, /* v_lshlrev_b32_e32 v1, 4, v0 */
    0xe0381000, 0x80000201, /* buffer_load_dwordx4 v[2:5], v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x120c0702, /* v_mul_i32_i24_e32 v6, v2, v3 */
    0xd2160007, 0x00020702, /* v_mul_u32_u24_e64 v7, v2, v3 */
    0xd2840008, 0x04120702, /* v_mad_i32_i24 v8, v2, v3, v4 */
    0xd2900009, 0x04120702, /* v_bfe_u32 v9, v2, v3, v4 */
    0xd292000a, 0x04120702, /* v_bfe_i32 v10, v2, v3, v4 */
    0x3c160903, /* v_bfm_b32_e32 v11, v3, v4 */
    0x7d8a0a80, /* v_cmp_ne_u32_e32 vcc, 0, v5 */
    0x50180702, /* v_addc_u32_e32 v12, vcc, v2, v3, vcc */
    0xd200000d, 0x01a90280, /* v_cndmask_b32_e64 v13, 0, 1, vcc */
    0xd18a0008, 0x00020a80, /* v_cmp_ne_u32_e64 s[8:9], 0, v5 */
    0xd2520a0e, 0x00220702, /* v_subb_u32_e64 v14, s[10:11], v2, v3, s[8:9] */
    0xd200000f, 0x00290280, /* v_cndmask_b32_e64 v15, 0, 1, s[10:11] */
    0xd2860010, 0x04120702, /* v_mad_u32_u24 v16, v2, v3, v4 */
    0x34220086, /* v_lshlrev_b32_e32 v17, 6, v0 */
    0xe0701000, 0x80010611, /* buffer_store_dword v6, v17, s[4:7], 0 offen */
    0xe0701004, 0x80010711, /* buffer_store_dword v7, v17, s[4:7], 0 offen offset:4 */
    0xe0701008, 0x80010811, /* buffer_store_dword v8, v17, s[4:7], 0 offen offset:8 */
    0xe070100c, 0x80010911, /* buffer_store_dword v9, v17, s[4:7], 0 offen offset:12 */
    0xe0701010, 0x80010a11, /* buffer_store_dword v10, v17, s[4:7], 0 offen offset:16 */
    0xe0701014, 0x80010b11, /* buffer_store_dword v11, v17, s[4:7], 0 offen offset:20 */
    0xe0701018, 0x80010c11, /* buffer_store_dword v12, v17, s[4:7], 0 offen offset:24 */
    0xe070101c, 0x80010d11, /* buffer_store_dword v13, v17, s[4:7], 0 offen offset:28 */
    0xe0701020, 0x80010e11, /* buffer_store_dword v14, v17, s[4:7], 0 offen offset:32 */
    0xe0701024, 0x80010f11, /* buffer_store_dword v15, v17, s[4:7], 0 offen offset:36 */
    0xe0701028, 0x80011011, /* buffer_store_dword v16, v17, s[4:7], 0 offen offset:40 */
    0xbf810000, /* s_endpgm */
};

/* Scalar compares with constants, min and max, bit-field extracts, and adds and multiplies by
   constants; every lane stores the same 22 words (out[0..21]): see test_gcn_compute.c for each. */
static const uint32_t scalar_batch[] = {
    0xbe8803ff, 0x0000ffff, /* s_mov_b32 s8, 0xffff */
    0xbe8903c1, /* s_mov_b32 s9, -1 */
    0xbe8a03ff, 0x12345678, /* s_mov_b32 s10, 0x12345678 */
    0xbe8b03ff, 0x0000f000, /* s_mov_b32 s11, 0xf000 */
    0xb488ffff, /* s_cmpk_eq_u32 s8, 0xffff */
    0x85148081, /* s_cselect_b32 s20, 1, 0 */
    0xb189ffff, /* s_cmpk_eq_i32 s9, 0xffff */
    0x85158081, /* s_cselect_b32 s21, 1, 0 */
    0xb3888000, /* s_cmpk_lt_i32 s8, 0x8000 */
    0x85168081, /* s_cselect_b32 s22, 1, 0 */
    0xb5888000, /* s_cmpk_gt_u32 s8, 0x8000 */
    0x85178081, /* s_cselect_b32 s23, 1, 0 */
    0x83180809, /* s_min_i32 s24, s9, s8 */
    0x85198081, /* s_cselect_b32 s25, 1, 0 */
    0x849a0809, /* s_max_u32 s26, s9, s8 */
    0x851b8081, /* s_cselect_b32 s27, 1, 0 */
    0x841c0809, /* s_max_i32 s28, s9, s8 */
    0x851d8081, /* s_cselect_b32 s29, 1, 0 */
    0x939eff0a, 0x00080008, /* s_bfe_u32 s30, s10, 0x80008 */
    0x851f8081, /* s_cselect_b32 s31, 1, 0 */
    0x9420ff0b, 0x0004000c, /* s_bfe_i32 s32, s11, 0x4000c */
    0x85218081, /* s_cselect_b32 s33, 1, 0 */
    0x93a2880a, /* s_bfe_u32 s34, s10, 8 */
    0x85238081, /* s_cselect_b32 s35, 1, 0 */
    0xbe8c03ff, 0x7ffffff8, /* s_mov_b32 s12, 0x7ffffff8 */
    0xbe8d0385, /* s_mov_b32 s13, 5 */
    0xbe8e0387, /* s_mov_b32 s14, 7 */
    0xb78c0010, /* s_addk_i32 s12, 0x10: overflows */
    0x85248081, /* s_cselect_b32 s36, 1, 0 */
    0xb80efffd, /* s_mulk_i32 s14, 0xfffd (-3): SCC kept */
    0x85268081, /* s_cselect_b32 s38, 1, 0 */
    0xb78dfffe, /* s_addk_i32 s13, 0xfffe (-2) */
    0x85258081, /* s_cselect_b32 s37, 1, 0 */
    0x7e020214, /* v_mov_b32_e32 v1, s20 */
    0x7e040215, /* v_mov_b32_e32 v2, s21 */
    0x7e060216, /* v_mov_b32_e32 v3, s22 */
    0x7e080217, /* v_mov_b32_e32 v4, s23 */
    0x7e0a0218, /* v_mov_b32_e32 v5, s24 */
    0x7e0c0219, /* v_mov_b32_e32 v6, s25 */
    0x7e0e021a, /* v_mov_b32_e32 v7, s26 */
    0x7e10021b, /* v_mov_b32_e32 v8, s27 */
    0x7e12021c, /* v_mov_b32_e32 v9, s28 */
    0x7e14021d, /* v_mov_b32_e32 v10, s29 */
    0x7e16021e, /* v_mov_b32_e32 v11, s30 */
    0x7e18021f, /* v_mov_b32_e32 v12, s31 */
    0x7e1a0220, /* v_mov_b32_e32 v13, s32 */
    0x7e1c0221, /* v_mov_b32_e32 v14, s33 */
    0x7e1e0222, /* v_mov_b32_e32 v15, s34 */
    0x7e200223, /* v_mov_b32_e32 v16, s35 */
    0xe0700000, 0x80010100, /* buffer_store_dword v1, off, s[4:7], 0 */
    0xe0700004, 0x80010200, /* buffer_store_dword v2, off, s[4:7], 0 offset:4 */
    0xe0700008, 0x80010300, /* buffer_store_dword v3, off, s[4:7], 0 offset:8 */
    0xe070000c, 0x80010400, /* buffer_store_dword v4, off, s[4:7], 0 offset:12 */
    0xe0700010, 0x80010500, /* buffer_store_dword v5, off, s[4:7], 0 offset:16 */
    0xe0700014, 0x80010600, /* buffer_store_dword v6, off, s[4:7], 0 offset:20 */
    0xe0700018, 0x80010700, /* buffer_store_dword v7, off, s[4:7], 0 offset:24 */
    0xe070001c, 0x80010800, /* buffer_store_dword v8, off, s[4:7], 0 offset:28 */
    0xe0700020, 0x80010900, /* buffer_store_dword v9, off, s[4:7], 0 offset:32 */
    0xe0700024, 0x80010a00, /* buffer_store_dword v10, off, s[4:7], 0 offset:36 */
    0xe0700028, 0x80010b00, /* buffer_store_dword v11, off, s[4:7], 0 offset:40 */
    0xe070002c, 0x80010c00, /* buffer_store_dword v12, off, s[4:7], 0 offset:44 */
    0xe0700030, 0x80010d00, /* buffer_store_dword v13, off, s[4:7], 0 offset:48 */
    0xe0700034, 0x80010e00, /* buffer_store_dword v14, off, s[4:7], 0 offset:52 */
    0xe0700038, 0x80010f00, /* buffer_store_dword v15, off, s[4:7], 0 offset:56 */
    0xe070003c, 0x80011000, /* buffer_store_dword v16, off, s[4:7], 0 offset:60 */
    0x7e22020c, /* v_mov_b32_e32 v17, s12 */
    0x7e240224, /* v_mov_b32_e32 v18, s36 */
    0x7e26020e, /* v_mov_b32_e32 v19, s14 */
    0x7e280226, /* v_mov_b32_e32 v20, s38 */
    0x7e2a020d, /* v_mov_b32_e32 v21, s13 */
    0x7e2c0225, /* v_mov_b32_e32 v22, s37 */
    0xe0700040, 0x80011100, /* buffer_store_dword v17, off, s[4:7], 0 offset:64 */
    0xe0700044, 0x80011200, /* buffer_store_dword v18, off, s[4:7], 0 offset:68 */
    0xe0700048, 0x80011300, /* buffer_store_dword v19, off, s[4:7], 0 offset:72 */
    0xe070004c, 0x80011400, /* buffer_store_dword v20, off, s[4:7], 0 offset:76 */
    0xe0700050, 0x80011500, /* buffer_store_dword v21, off, s[4:7], 0 offset:80 */
    0xe0700054, 0x80011600, /* buffer_store_dword v22, off, s[4:7], 0 offset:84 */
    0xbf810000, /* s_endpgm */
};

/* Per lane i, from in[i] (x): out record i (32 bytes) holds v_sin_f32, v_cos_f32, v_exp_f32,
   v_log_f32 and v_rsq_clamp_f32 of x. */
static const uint32_t transcendentals[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0301000, 0x80000201, /* buffer_load_dword v2, v1, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e066b02, /* v_sin_f32_e32 v3, v2 */
    0x7e086d02, /* v_cos_f32_e32 v4, v2 */
    0x7e0a4b02, /* v_exp_f32_e32 v5, v2 */
    0x7e0c4f02, /* v_log_f32_e32 v6, v2 */
    0x7e0e5902, /* v_rsq_clamp_f32_e32 v7, v2 */
    0x34100085, /* v_lshlrev_b32_e32 v8, 5, v0 */
    0xe0701000, 0x80010308, /* buffer_store_dword v3, v8, s[4:7], 0 offen */
    0xe0701004, 0x80010408, /* buffer_store_dword v4, v8, s[4:7], 0 offen offset:4 */
    0xe0701008, 0x80010508, /* buffer_store_dword v5, v8, s[4:7], 0 offen offset:8 */
    0xe070100c, 0x80010608, /* buffer_store_dword v6, v8, s[4:7], 0 offen offset:12 */
    0xe0701010, 0x80010708, /* buffer_store_dword v7, v8, s[4:7], 0 offen offset:16 */
    0xbf810000, /* s_endpgm */
};

/* As vertex_position_color, plus pos1 = (x, y): with output control 0x400003 (clip distances 0
   and 1 in the first distance vector) clip distance 0 is the position's x and 1 its y, so x < 0
   and y < 0 are clipped. */
static const uint32_t vertex_clip[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80000cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 */
    0xf80008d3, 0x00000201, /* exp pos1, v1, v2, off, off done */
    0xbf810000, /* s_endpgm */
};

/* A pixel shader that colours every pixel from its own user data, s0-s3 (red, green, blue,
   alpha), packed as halves. */
static const uint32_t pixel_user_color[] = {
    0x7e040200, /* v_mov_b32_e32 v2, s0 */
    0x7e060201, /* v_mov_b32_e32 v3, s1 */
    0x7e080202, /* v_mov_b32_e32 v4, s2 */
    0x7e0a0203, /* v_mov_b32_e32 v5, s3 */
    0x5e0c0702, /* v_cvt_pkrtz_f16_f32_e32 v6, v2, v3 */
    0x5e0e0b04, /* v_cvt_pkrtz_f16_f32_e32 v7, v4, v5 */
    0xf8001c0f, 0x00000706, /* exp mrt0, v6, v6, v7, v7 done compr vm */
    0xbf810000, /* s_endpgm */
};

/* The local data share (LDS, 272 bytes: 68 dwords). Each lane writes lane + 100 at dword lane and adds 1
   to dword 64; lanes 0-3 write byte 260 + lane = lane + 1; after s_barrier each lane stores, at byte
   lane * 32 of s[4:7]: dword lane ^ 1, dwords lane and lane + 1 (ds_read2), dword 64 (the count, 64),
   the short at byte 261 (0x0302), and dword 1024 (past the LDS: 0). */
static const uint32_t lds_exchange[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0x4a0400ff, 0x00000064, /* v_add_i32_e32 v2, vcc, 0x64, v0 */
    0xd8340000, 0x00000201, /* ds_write_b32 v1, v2 */
    0x7e1402ff, 0x00000100, /* v_mov_b32_e32 v10, 0x100 */
    0x7e120281, /* v_mov_b32_e32 v9, 1 */
    0xd8000000, 0x0000090a, /* ds_add_u32 v10, v9 */
    0x7d880084, /* v_cmp_gt_u32_e32 vcc, 4, v0 */
    0xbe8c246a, /* s_and_saveexec_b64 s[12:13], vcc */
    0x4a1600ff, 0x00000104, /* v_add_i32_e32 v11, vcc, 0x104, v0 */
    0x4a180081, /* v_add_i32_e32 v12, vcc, 1, v0 */
    0xd8780000, 0x00000c0b, /* ds_write_b8 v11, v12 */
    0xbefe040c, /* s_mov_b64 exec, s[12:13] */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xbf8a0000, /* s_barrier */
    0x3a060081, /* v_xor_b32_e32 v3, 1, v0 */
    0x34060682, /* v_lshlrev_b32_e32 v3, 2, v3 */
    0xd8d80000, 0x04000003, /* ds_read_b32 v4, v3 */
    0xd8dc0100, 0x05000001, /* ds_read2_b32 v[5:6], v1 offset1:1 */
    0xd8d80000, 0x0700000a, /* ds_read_b32 v7, v10 */
    0x7e1a02ff, 0x00000104, /* v_mov_b32_e32 v13, 0x104 */
    0xd8f00001, 0x0800000d, /* ds_read_u16 v8, v13 offset:1 */
    0x7e1e02ff, 0x00001000, /* v_mov_b32_e32 v15, 0x1000 */
    0xd8d80000, 0x1000000f, /* ds_read_b32 v16, v15 */
    0x341c0085, /* v_lshlrev_b32_e32 v14, 5, v0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xe0781000, 0x8001040e, /* buffer_store_dwordx4 v[4:7], v14, s[4:7], 0 offen */
    0xe0701010, 0x8001080e, /* buffer_store_dword v8, v14, s[4:7], 0 offen offset:16 */
    0xe0701014, 0x8001100e, /* buffer_store_dword v16, v14, s[4:7], 0 offen offset:20 */
    0xbf810000, /* s_endpgm */
};

/* Tessellation stages (runtime/gcn_spirv.h). The local shader writes (float(vertex index), 0.5) at
   16 * its RelAutoIndex in LDS; the hull shader writes six factors of 1.0 (a quad's) at its patch's 24
   bytes of the tessellation-factor ring (the descriptor in s[0:3], the workgroup's offset in s8); the
   domain shader reads its patch's two words back (16 * v2) and exports them as parameter 0, with the
   position (u, v, 0, 1). */
static const uint32_t tess_local[] = {
    0x34080284, /* v_lshlrev_b32_e32 v4, 4, v1 */
    0x7e0a0d00, /* v_cvt_f32_u32_e32 v5, v0 */
    0x7e0c02f0, /* v_mov_b32_e32 v6, 0.5 */
    0xd8380100, 0x00060504, /* ds_write2_b32 v4, v5, v6 offset1:1 */
    0xbf810000, /* s_endpgm */
};
static const uint32_t tess_hull[] = {
    0xd2900002, 0x02210101, /* v_bfe_u32 v2, v1, 0, 8 */
    0x16060498, /* v_mul_u32_u24_e32 v3, 24, v2 */
    0x7e0802f2, /* v_mov_b32_e32 v4, 1.0 */
    0xe0701000, 0x08000403, /* buffer_store_dword v4, v3, s[0:3], s8 offen */
    0xe0701004, 0x08000403, /* buffer_store_dword v4, v3, s[0:3], s8 offen offset:4 */
    0xe0701008, 0x08000403, /* buffer_store_dword v4, v3, s[0:3], s8 offen offset:8 */
    0xe070100c, 0x08000403, /* buffer_store_dword v4, v3, s[0:3], s8 offen offset:12 */
    0xe0701010, 0x08000403, /* buffer_store_dword v4, v3, s[0:3], s8 offen offset:16 */
    0xe0701014, 0x08000403, /* buffer_store_dword v4, v3, s[0:3], s8 offen offset:20 */
    0xbf810000, /* s_endpgm */
};
static const uint32_t tess_domain[] = {
    0x34080484, /* v_lshlrev_b32_e32 v4, 4, v2 */
    0xd8dc0100, 0x05000004, /* ds_read2_b32 v[5:6], v4 offset1:1 */
    0x7e0e0280, /* v_mov_b32_e32 v7, 0 */
    0x7e1002f2, /* v_mov_b32_e32 v8, 1.0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf, 0x08070100, /* exp pos0, v0, v1, v7, v8 done */
    0xbf810000, /* s_endpgm */
};

/* Geometry-shader stages (runtime/gcn_spirv.h, "Geometry shaders"); ring accesses carry GLC and SLC, through
   s[0:3], whose descriptor is not read. The export shader writes each vertex's (float(vertex index), 0.5)
   to the ESGS ring (its offset in s4). The geometry shader, for a point list, reads its vertex's two words
   back (v0 its ESGS offset, the second word 256 bytes on) and emits two strips: a quad over x from index - 1
   to index and y from 0 to 1, a cut, then a quad over the same x and y from -1 to -0.5; each of its eight
   vertices is four GSVS dwords (x, y, the index, 0.5) stored at offset (dword * 8 + vertex) * 4 from its
   GSVS offset in s4, for VGT_GS_MAX_VERT_OUT 8. The copy shader reads its vertex's four dwords (v0 << 2, then
   8 * 256 bytes a dword on) and exports the position (x, y, 0, 1) and parameter 0 (index, 0.5, 0, 1). */
static const uint32_t gs_export[] = {
    0x7e020d00, /* v_cvt_f32_u32_e32 v1, v0 */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0xe0704000, 0x04400100, /* buffer_store_dword v1, off, s[0:3], s4 glc slc */
    0xe0704004, 0x04400200, /* buffer_store_dword v2, off, s[0:3], s4 offset:4 glc slc */
    0xbf810000, /* s_endpgm */
};
static const uint32_t gs_geometry[] = {
    0xbefc0305, /* s_mov_b32 m0, s5 */
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0305000, 0x80400301, /* buffer_load_dword v3, v1, s[0:3], 0 offen glc slc */
    0xb0060100, /* s_movk_i32 s6, 0x100 */
    0xe0305000, 0x06400401, /* buffer_load_dword v4, v1, s[0:3], s6 offen glc slc */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x060a06f3, /* v_add_f32_e32 v5, -1.0, v3 */
    0x060c0af2, /* v_add_f32_e32 v6, 1.0, v5 */
    0x7e0e0280, /* v_mov_b32_e32 v7, 0 */
    0x7e1002f2, /* v_mov_b32_e32 v8, 1.0 */
    0x7e1202f3, /* v_mov_b32_e32 v9, -1.0 */
    0x7e1402f1, /* v_mov_b32_e32 v10, -0.5 */
    0xe0704000, 0x04400500, /* buffer_store_dword v5, off, s[0:3], s4 glc slc */
    0xe0704020, 0x04400700, /* buffer_store_dword v7, off, s[0:3], s4 offset:32 glc slc */
    0xe0704040, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:64 glc slc */
    0xe0704060, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:96 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xe0704004, 0x04400600, /* buffer_store_dword v6, off, s[0:3], s4 offset:4 glc slc */
    0xe0704024, 0x04400700, /* buffer_store_dword v7, off, s[0:3], s4 offset:36 glc slc */
    0xe0704044, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:68 glc slc */
    0xe0704064, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:100 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xe0704008, 0x04400500, /* buffer_store_dword v5, off, s[0:3], s4 offset:8 glc slc */
    0xe0704028, 0x04400800, /* buffer_store_dword v8, off, s[0:3], s4 offset:40 glc slc */
    0xe0704048, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:72 glc slc */
    0xe0704068, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:104 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xe070400c, 0x04400600, /* buffer_store_dword v6, off, s[0:3], s4 offset:12 glc slc */
    0xe070402c, 0x04400800, /* buffer_store_dword v8, off, s[0:3], s4 offset:44 glc slc */
    0xe070404c, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:76 glc slc */
    0xe070406c, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:108 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xbf900012, /* s_sendmsg sendmsg(MSG_GS, GS_OP_CUT, 0) */
    0xe0704010, 0x04400500, /* buffer_store_dword v5, off, s[0:3], s4 offset:16 glc slc */
    0xe0704030, 0x04400900, /* buffer_store_dword v9, off, s[0:3], s4 offset:48 glc slc */
    0xe0704050, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:80 glc slc */
    0xe0704070, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:112 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xe0704014, 0x04400600, /* buffer_store_dword v6, off, s[0:3], s4 offset:20 glc slc */
    0xe0704034, 0x04400900, /* buffer_store_dword v9, off, s[0:3], s4 offset:52 glc slc */
    0xe0704054, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:84 glc slc */
    0xe0704074, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:116 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xe0704018, 0x04400500, /* buffer_store_dword v5, off, s[0:3], s4 offset:24 glc slc */
    0xe0704038, 0x04400a00, /* buffer_store_dword v10, off, s[0:3], s4 offset:56 glc slc */
    0xe0704058, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:88 glc slc */
    0xe0704078, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:120 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xe070401c, 0x04400600, /* buffer_store_dword v6, off, s[0:3], s4 offset:28 glc slc */
    0xe070403c, 0x04400a00, /* buffer_store_dword v10, off, s[0:3], s4 offset:60 glc slc */
    0xe070405c, 0x04400300, /* buffer_store_dword v3, off, s[0:3], s4 offset:92 glc slc */
    0xe070407c, 0x04400400, /* buffer_store_dword v4, off, s[0:3], s4 offset:124 glc slc */
    0xbf900022, /* s_sendmsg sendmsg(MSG_GS, GS_OP_EMIT, 0) */
    0xbf900003, /* s_sendmsg sendmsg(MSG_GS_DONE, GS_OP_NOP) */
    0xbf810000, /* s_endpgm */
};
static const uint32_t gs_copy[] = {
    0x34020082, /* v_lshlrev_b32_e32 v1, 2, v0 */
    0xe0305000, 0x80400201, /* buffer_load_dword v2, v1, s[0:3], 0 offen glc slc */
    0xe0305800, 0x80400301, /* buffer_load_dword v3, v1, s[0:3], 0 offen offset:2048 glc slc */
    0xb0041000, /* s_movk_i32 s4, 0x1000 */
    0xe0305000, 0x04400401, /* buffer_load_dword v4, v1, s[0:3], s4 offen glc slc */
    0xb0041800, /* s_movk_i32 s4, 0x1800 */
    0xe0305000, 0x04400501, /* buffer_load_dword v5, v1, s[0:3], s4 offen glc slc */
    0x7e0c0280, /* v_mov_b32_e32 v6, 0 */
    0x7e0e02f2, /* v_mov_b32_e32 v7, 1.0 */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800020f, 0x07060504, /* exp param0, v4, v5, v6, v7 */
    0xf80008cf, 0x07060302, /* exp pos0, v2, v3, v6, v7 done */
    0xbf810000, /* s_endpgm */
};

#endif
