/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned Bonaire encodings: llvm-mc -triple=amdgcn -mcpu=bonaire --show-encoding. */
#ifndef PB_METAL_VOLUME_PROGRAMS_H
#define PB_METAL_VOLUME_PROGRAMS_H
#include <stdint.h>
static const uint32_t volume_grid_load[]={
    0x8710ff04,0x00001fff, /* s_and_b32 s16, s4, 0x1fff */
    0x80108110, /* s_add_u32 s16, s16, 1 */
    0x8711830f, /* s_and_b32 s17, s15, 3 */
    0x8f118211, /* s_lshl_b32 s17, s17, 2 */
    0x4a000011, /* v_add_i32_e32 v0, vcc, s17, v0 */
    0x9011820f, /* s_lshr_b32 s17, s15, 2 */
    0x8f118211, /* s_lshl_b32 s17, s17, 2 */
    0x4a020211, /* v_add_i32_e32 v1, vcc, s17, v1 */
    0x4a04040c, /* v_add_i32_e32 v2, vcc, s12, v2 */
    0x7d88000d, /* v_cmp_gt_u32_e32 vcc, s13, v0 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0x7d88020e, /* v_cmp_gt_u32_e32 vcc, s14, v1 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0x7d880410, /* v_cmp_gt_u32_e32 vcc, s16, v2 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0xd2d20003,0x0002040e, /* v_mul_lo_u32 v3, s14, v2 */
    0x4a060303, /* v_add_i32_e32 v3, vcc, v3, v1 */
    0xd2d20003,0x0002060d, /* v_mul_lo_u32 v3, s13, v3 */
    0x4a060103, /* v_add_i32_e32 v3, vcc, v3, v0 */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0xf0000f00,0x00000400, /* image_load v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000,0x80020403, /* buffer_store_dwordx4 v[4:7], v3, s[8:11], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_grid_store_float[]={
    0x8710ff04,0x00001fff, /* s_and_b32 s16, s4, 0x1fff */
    0x80108110, /* s_add_u32 s16, s16, 1 */
    0x8711830f, /* s_and_b32 s17, s15, 3 */
    0x8f118211, /* s_lshl_b32 s17, s17, 2 */
    0x4a000011, /* v_add_i32_e32 v0, vcc, s17, v0 */
    0x9011820f, /* s_lshr_b32 s17, s15, 2 */
    0x8f118211, /* s_lshl_b32 s17, s17, 2 */
    0x4a020211, /* v_add_i32_e32 v1, vcc, s17, v1 */
    0x4a04040c, /* v_add_i32_e32 v2, vcc, s12, v2 */
    0x7d88000d, /* v_cmp_gt_u32_e32 vcc, s13, v0 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0x7d88020e, /* v_cmp_gt_u32_e32 vcc, s14, v1 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0x7d880410, /* v_cmp_gt_u32_e32 vcc, s16, v2 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0xd2d20003,0x0002040e, /* v_mul_lo_u32 v3, s14, v2 */
    0x4a060303, /* v_add_i32_e32 v3, vcc, v3, v1 */
    0xd2d20003,0x0002060d, /* v_mul_lo_u32 v3, s13, v3 */
    0x4a060103, /* v_add_i32_e32 v3, vcc, v3, v0 */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0x7e080d00, /* v_cvt_f32_u32_e32 v4, v0 */
    0x100808ff,0x3d000000, /* v_mul_f32_e32 v4, 0x3d000000, v4 */
    0x7e0a0d01, /* v_cvt_f32_u32_e32 v5, v1 */
    0x100a0aff,0x3d000000, /* v_mul_f32_e32 v5, 0x3d000000, v5 */
    0x7e0c0d02, /* v_cvt_f32_u32_e32 v6, v2 */
    0x100c0cff,0x3d000000, /* v_mul_f32_e32 v6, 0x3d000000, v6 */
    0x7e0e02f0, /* v_mov_b32_e32 v7, 0.5 */
    0x7e1002ff,0x13572468, /* v_mov_b32_e32 v8, 0x13572468 */
    0xe0701000,0x80020803, /* buffer_store_dword v8, v3, s[8:11], 0 offen */
    0xf0200f00,0x00000400, /* image_store v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_grid_store_bits[]={
    0x8710ff04,0x00001fff, /* s_and_b32 s16, s4, 0x1fff */
    0x80108110, /* s_add_u32 s16, s16, 1 */
    0x8711830f, /* s_and_b32 s17, s15, 3 */
    0x8f118211, /* s_lshl_b32 s17, s17, 2 */
    0x4a000011, /* v_add_i32_e32 v0, vcc, s17, v0 */
    0x9011820f, /* s_lshr_b32 s17, s15, 2 */
    0x8f118211, /* s_lshl_b32 s17, s17, 2 */
    0x4a020211, /* v_add_i32_e32 v1, vcc, s17, v1 */
    0x4a04040c, /* v_add_i32_e32 v2, vcc, s12, v2 */
    0x7d88000d, /* v_cmp_gt_u32_e32 vcc, s13, v0 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0x7d88020e, /* v_cmp_gt_u32_e32 vcc, s14, v1 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0x7d880410, /* v_cmp_gt_u32_e32 vcc, s16, v2 */
    0xbefe046a, /* s_mov_b64 exec, vcc */
    0xd2d20003,0x0002040e, /* v_mul_lo_u32 v3, s14, v2 */
    0x4a060303, /* v_add_i32_e32 v3, vcc, v3, v1 */
    0xd2d20003,0x0002060d, /* v_mul_lo_u32 v3, s13, v3 */
    0x4a060103, /* v_add_i32_e32 v3, vcc, v3, v0 */
    0x34060684, /* v_lshlrev_b32_e32 v3, 4, v3 */
    0x4a0800a5, /* v_add_i32_e32 v4, vcc, 37, v0 */
    0x4a0a02b5, /* v_add_i32_e32 v5, vcc, 53, v1 */
    0x4a0c04ff,0x00000047, /* v_add_i32_e32 v6, vcc, 0x47, v2 */
    0x7e0e02ff,0x000000a7, /* v_mov_b32_e32 v7, 0xa7 */
    0x7e1002ff,0x13572468, /* v_mov_b32_e32 v8, 0x13572468 */
    0xe0701000,0x80020803, /* buffer_store_dword v8, v3, s[8:11], 0 offen */
    0xf0200f00,0x00000400, /* image_store v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_probe_load[]={
    0x7e00020c, /* v_mov_b32_e32 v0, s12 */
    0x7e02020d, /* v_mov_b32_e32 v1, s13 */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00,0x00000400, /* image_load v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000,0x80020403, /* buffer_store_dwordx4 v[4:7], v3, s[8:11], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_probe_store_float[]={
    0x7e00020c, /* v_mov_b32_e32 v0, s12 */
    0x7e02020d, /* v_mov_b32_e32 v1, s13 */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0x7e0802f0, /* v_mov_b32_e32 v4, 0.5 */
    0x7e0a02f0, /* v_mov_b32_e32 v5, 0.5 */
    0x7e0c02f0, /* v_mov_b32_e32 v6, 0.5 */
    0x7e0e02f0, /* v_mov_b32_e32 v7, 0.5 */
    0x7e1002ff,0x13572468, /* v_mov_b32_e32 v8, 0x13572468 */
    0xe0701000,0x80020803, /* buffer_store_dword v8, v3, s[8:11], 0 offen */
    0xf0200f00,0x00000400, /* image_store v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_probe_inplace_float[]={
    0x7e00020c, /* v_mov_b32_e32 v0, s12 */
    0x7e02020d, /* v_mov_b32_e32 v1, s13 */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00,0x00000400, /* image_load v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x060808ff,0x3e800000, /* v_add_f32_e32 v4, 0x3e800000, v4 */
    0xf0200f00,0x00000400, /* image_store v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_probe_inplace_bits[]={
    0x7e00020c, /* v_mov_b32_e32 v0, s12 */
    0x7e02020d, /* v_mov_b32_e32 v1, s13 */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00,0x00000400, /* image_load v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x3a080881, /* v_xor_b32_e32 v4, 1, v4 */
    0xf0200f00,0x00000400, /* image_store v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_raw_copy[]={
    0x8f0c860c, /* s_lshl_b32 s12, s12, 6 */
    0x4a00000c, /* v_add_i32_e32 v0, vcc, s12, v0 */
    0x34000082, /* v_lshlrev_b32_e32 v0, 2, v0 */
    0xe0301000,0x80000400, /* buffer_load_dword v4, v0, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0701000,0x80020400, /* buffer_store_dword v4, v0, s[8:11], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_dual_copy[]={
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0x7e020280, /* v_mov_b32_e32 v1, 0 */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0xf0000f00,0x00000400, /* image_load v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e000281, /* v_mov_b32_e32 v0, 1 */
    0xf0200f00,0x00020400, /* image_store v[4:7], v[0:2], s[8:15] dmask:0xf */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_read_marker[]={
    0x7e00020c, /* v_mov_b32_e32 v0, s12 */
    0x7e02020d, /* v_mov_b32_e32 v1, s13 */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00,0x00000400, /* image_load v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e1002ff,0x3f200000, /* v_mov_b32_e32 v8, 0x3f200000 */
    0xe0701000,0x80020803, /* buffer_store_dword v8, v3, s[8:11], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t volume_store_then_stop[]={
    0x7e00020c, /* v_mov_b32_e32 v0, s12 */
    0x7e02020d, /* v_mov_b32_e32 v1, s13 */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0x7e0802f0, /* v_mov_b32_e32 v4, 0.5 */
    0x7e0a02f0, /* v_mov_b32_e32 v5, 0.5 */
    0x7e0c02f0, /* v_mov_b32_e32 v6, 0.5 */
    0x7e0e02f0, /* v_mov_b32_e32 v7, 0.5 */
    0xf0200f00,0x00000400, /* image_store v[4:7], v[0:2], s[0:7] dmask:0xf */
    0xe0001000,0x80020803, /* buffer_load_format_x v8, v3, s[8:11], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};
#endif
