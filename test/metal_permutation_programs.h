/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned LLVM Bonaire programs. Every image store has the required UNRM bit.
   Only X entry ID is consumed; Y and array coordinates come from explicit scalar inputs. */
#ifndef PB_METAL_PERMUTATION_PROGRAMS_H
#define PB_METAL_PERMUTATION_PROGRAMS_H
#include <stdint.h>

static const uint32_t permutation_load_1d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0xf0000f00, 0x00020400, /* image_load v[4:7], v0, s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_store_1d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e080200, /* v_mov_b32_e32 v4, s0 */
    0x7e0a0201, /* v_mov_b32_e32 v5, s1 */
    0x7e0c0202, /* v_mov_b32_e32 v6, s2 */
    0x7e0e0203, /* v_mov_b32_e32 v7, s3 */
    0xf0201f00, 0x00020400, /* image_store v[4:7], v0, s[8:15] dmask:0xf unorm */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_float_1d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0xf0000f00, 0x00020400, /* image_load v[4:7], v0, s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x060808ff, 0x3e000000, /* v_add_f32_e32 v4, 0x3e000000, v4 */
    0xf0201f00, 0x00020400, /* image_store v[4:7], v0, s[8:15] dmask:0xf unorm */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_uint_1d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0xf0000f00, 0x00020400, /* image_load v[4:7], v0, s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x4a08088d, /* v_add_i32_e32 v4, vcc, 13, v4 */
    0xf0201f00, 0x00020400, /* image_store v[4:7], v0, s[8:15] dmask:0xf unorm */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_load_2d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0xf0000f00, 0x00020400, /* image_load v[4:7], v[0:1], s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_store_2d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0x7e080200, /* v_mov_b32_e32 v4, s0 */
    0x7e0a0201, /* v_mov_b32_e32 v5, s1 */
    0x7e0c0202, /* v_mov_b32_e32 v6, s2 */
    0x7e0e0203, /* v_mov_b32_e32 v7, s3 */
    0xf0201f00, 0x00020400, /* image_store v[4:7], v[0:1], s[8:15] dmask:0xf unorm */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_float_2d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0xf0000f00, 0x00020400, /* image_load v[4:7], v[0:1], s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x060808ff, 0x3e000000, /* v_add_f32_e32 v4, 0x3e000000, v4 */
    0xf0201f00, 0x00020400, /* image_store v[4:7], v[0:1], s[8:15] dmask:0xf unorm */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_uint_2d[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0xf0000f00, 0x00020400, /* image_load v[4:7], v[0:1], s[8:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x4a08088d, /* v_add_i32_e32 v4, vcc, 13, v4 */
    0xf0201f00, 0x00020400, /* image_store v[4:7], v[0:1], s[8:15] dmask:0xf unorm */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_load_1d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020205, /* v_mov_b32_e32 v1, s5 */
    0xf0004f00, 0x00020400, /* image_load v[4:7], v[0:1], s[8:15] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_store_1d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020205, /* v_mov_b32_e32 v1, s5 */
    0x7e080200, /* v_mov_b32_e32 v4, s0 */
    0x7e0a0201, /* v_mov_b32_e32 v5, s1 */
    0x7e0c0202, /* v_mov_b32_e32 v6, s2 */
    0x7e0e0203, /* v_mov_b32_e32 v7, s3 */
    0xf0205f00, 0x00020400, /* image_store v[4:7], v[0:1], s[8:15] dmask:0xf unorm da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_float_1d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020205, /* v_mov_b32_e32 v1, s5 */
    0xf0004f00, 0x00020400, /* image_load v[4:7], v[0:1], s[8:15] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x060808ff, 0x3e000000, /* v_add_f32_e32 v4, 0x3e000000, v4 */
    0xf0205f00, 0x00020400, /* image_store v[4:7], v[0:1], s[8:15] dmask:0xf unorm da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_uint_1d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020205, /* v_mov_b32_e32 v1, s5 */
    0xf0004f00, 0x00020400, /* image_load v[4:7], v[0:1], s[8:15] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x4a08088d, /* v_add_i32_e32 v4, vcc, 13, v4 */
    0xf0205f00, 0x00020400, /* image_store v[4:7], v[0:1], s[8:15] dmask:0xf unorm da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_load_2d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0x7e040205, /* v_mov_b32_e32 v2, s5 */
    0xf0004f00, 0x00020400, /* image_load v[4:7], v[0:2], s[8:15] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_store_2d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0x7e040205, /* v_mov_b32_e32 v2, s5 */
    0x7e080200, /* v_mov_b32_e32 v4, s0 */
    0x7e0a0201, /* v_mov_b32_e32 v5, s1 */
    0x7e0c0202, /* v_mov_b32_e32 v6, s2 */
    0x7e0e0203, /* v_mov_b32_e32 v7, s3 */
    0xf0205f00, 0x00020400, /* image_store v[4:7], v[0:2], s[8:15] dmask:0xf unorm da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_float_2d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0x7e040205, /* v_mov_b32_e32 v2, s5 */
    0xf0004f00, 0x00020400, /* image_load v[4:7], v[0:2], s[8:15] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x060808ff, 0x3e000000, /* v_add_f32_e32 v4, 0x3e000000, v4 */
    0xf0205f00, 0x00020400, /* image_store v[4:7], v[0:2], s[8:15] dmask:0xf unorm da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_rw_uint_2d_array[] = {
    0x7e060300, /* v_mov_b32_e32 v3, v0 */
    0x4a000006, /* v_add_i32_e32 v0, vcc, s6, v0 */
    0x7e020204, /* v_mov_b32_e32 v1, s4 */
    0x7e040205, /* v_mov_b32_e32 v2, s5 */
    0xf0004f00, 0x00020400, /* image_load v[4:7], v[0:2], s[8:15] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xe0781000, 0x80000403, /* buffer_store_dwordx4 v[4:7], v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x4a08088d, /* v_add_i32_e32 v4, vcc, 13, v4 */
    0xf0205f00, 0x00020400, /* image_store v[4:7], v[0:2], s[8:15] dmask:0xf unorm da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

static const uint32_t permutation_raw_copy[] = {
    0x8f088608, /* s_lshl_b32 s8, s8, 6 */
    0x4a000008, /* v_add_i32_e32 v0, vcc, s8, v0 */
    0x34000082, /* v_lshlrev_b32_e32 v0, 2, v0 */
    0xe0301000, 0x80000400, /* buffer_load_dword v4, v0, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0701000, 0x80010400, /* buffer_store_dword v4, v0, s[4:7], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};

#endif
