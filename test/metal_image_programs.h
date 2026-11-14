/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_METAL_IMAGE_PROGRAMS_H
#define TEST_METAL_IMAGE_PROGRAMS_H
/* Owned probes, LLVM llvm-mc -triple=amdgcn -mcpu=bonaire. One covered pixel stores
   raw image results before exporting zero. Instruction comments are our assembler input. */
#include <stdint.h>

static const uint32_t image_probe_load[] = {
    0x7e040208, /* v_mov_b32_e32 v2, s8 */
    0x7e060209, /* v_mov_b32_e32 v3, s9 */
    0x7e08020a, /* v_mov_b32_e32 v4, s10 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8003080c, /* buffer_store_dwordx4 v[8:11], v12, s[12:15], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_load_mip[] = {
    0x7e040208, /* v_mov_b32_e32 v2, s8 */
    0x7e060209, /* v_mov_b32_e32 v3, s9 */
    0x7e08020a, /* v_mov_b32_e32 v4, s10 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0xf0040f00, 0x00000802, /* image_load_mip v[8:11], v[2:4], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8003080c, /* buffer_store_dwordx4 v[8:11], v12, s[12:15], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_load_array[] = {
    0x7e040208, /* v_mov_b32_e32 v2, s8 */
    0x7e060209, /* v_mov_b32_e32 v3, s9 */
    0x7e08020a, /* v_mov_b32_e32 v4, s10 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0xf0044f00, 0x00000802, /* image_load_mip v[8:11], v[2:5], s[0:7] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8003080c, /* buffer_store_dwordx4 v[8:11], v12, s[12:15], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_load_memory[] = {
    0xc0c00100, /* s_load_dwordx8 s[0:7], s[0:1], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040208, /* v_mov_b32_e32 v2, s8 */
    0x7e060209, /* v_mov_b32_e32 v3, s9 */
    0x7e08020a, /* v_mov_b32_e32 v4, s10 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0xf0040f00, 0x00000802, /* image_load_mip v[8:11], v[2:4], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8003080c, /* buffer_store_dwordx4 v[8:11], v12, s[12:15], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_sample[] = {
    0xc0880d00, /* s_load_dwordx4 s[16:19], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e06020f, /* v_mov_b32_e32 v3, s15 */
    0x7e080280, /* v_mov_b32_e32 v4, 0 */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8004080c, /* buffer_store_dwordx4 v[8:11], v12, s[16:19], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_sample_array[] = {
    0xc0880d00, /* s_load_dwordx4 s[16:19], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e06020f, /* v_mov_b32_e32 v3, s15 */
    0x7e0802f2, /* v_mov_b32_e32 v4, 1.0 */
    0xf0804f00, 0x00400802, /* image_sample v[8:11], v[2:4], s[0:7], s[8:11] dmask:0xf da */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8004080c, /* buffer_store_dwordx4 v[8:11], v12, s[16:19], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_sample_memory[] = {
    0xc0880d00, /* s_load_dwordx4 s[16:19], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xc0c00100, /* s_load_dwordx8 s[0:7], s[0:1], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e06020f, /* v_mov_b32_e32 v3, s15 */
    0x7e080280, /* v_mov_b32_e32 v4, 0 */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8004080c, /* buffer_store_dwordx4 v[8:11], v12, s[16:19], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_sample_memory_sampler[] = {
    0xc0880d00, /* s_load_dwordx4 s[16:19], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xc0840900, /* s_load_dwordx4 s[8:11], s[8:9], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e06020f, /* v_mov_b32_e32 v3, s15 */
    0x7e080280, /* v_mov_b32_e32 v4, 0 */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8004080c, /* buffer_store_dwordx4 v[8:11], v12, s[16:19], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_two_images[] = {
    0xc0900b00, /* s_load_dwordx4 s[32:35], s[10:11], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000402, /* image_load v[4:7], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xc0c00900, /* s_load_dwordx8 s[0:7], s[8:9], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0xf0800f00, 0x00600802, /* image_sample v[8:11], v[2:3], s[0:7], s[12:15] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8008040c, /* buffer_store_dwordx4 v[4:7], v12, s[32:35], 0 offen */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_load_high[] = {
    0x7e040208, /* v_mov_b32_e32 v2, s8 */
    0x7e060209, /* v_mov_b32_e32 v3, s9 */
    0x7e08020a, /* v_mov_b32_e32 v4, s10 */
    0x7e0a020b, /* v_mov_b32_e32 v5, s11 */
    0xf0000f00, 0x00040802, /* image_load v[8:11], v[2:3], s[16:23] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8003080c, /* buffer_store_dwordx4 v[8:11], v12, s[12:15], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_sample_high[] = {
    0xc0880d00, /* s_load_dwordx4 s[16:19], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e04020e, /* v_mov_b32_e32 v2, s14 */
    0x7e06020f, /* v_mov_b32_e32 v3, s15 */
    0x7e080280, /* v_mov_b32_e32 v4, 0 */
    0xf0800f00, 0x00c00802, /* image_sample v[8:11], v[2:3], s[0:7], s[24:27] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xe0781000, 0x8004080c, /* buffer_store_dwordx4 v[8:11], v12, s[16:19], 0 offen */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_eight_images[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xc0c00f00, /* s_load_dwordx8 s[0:7], s[14:15], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen */
    0xc0c00f08, /* s_load_dwordx8 s[0:7], s[14:15], 0x8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0xc0c00f10, /* s_load_dwordx8 s[0:7], s[14:15], 0x10 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0xc0c00f18, /* s_load_dwordx8 s[0:7], s[14:15], 0x18 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0xc0c00f20, /* s_load_dwordx8 s[0:7], s[14:15], 0x20 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0xc0c00f28, /* s_load_dwordx8 s[0:7], s[14:15], 0x28 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0xc0c00f30, /* s_load_dwordx8 s[0:7], s[14:15], 0x30 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0xc0c00f38, /* s_load_dwordx8 s[0:7], s[14:15], 0x38 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_nine_images[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xc0c00f00, /* s_load_dwordx8 s[0:7], s[14:15], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen */
    0xc0c00f08, /* s_load_dwordx8 s[0:7], s[14:15], 0x8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0xc0c00f10, /* s_load_dwordx8 s[0:7], s[14:15], 0x10 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0xc0c00f18, /* s_load_dwordx8 s[0:7], s[14:15], 0x18 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0xc0c00f20, /* s_load_dwordx8 s[0:7], s[14:15], 0x20 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0xc0c00f28, /* s_load_dwordx8 s[0:7], s[14:15], 0x28 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0xc0c00f30, /* s_load_dwordx8 s[0:7], s[14:15], 0x30 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0xc0c00f38, /* s_load_dwordx8 s[0:7], s[14:15], 0x38 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0xc0c00f40, /* s_load_dwordx8 s[0:7], s[14:15], 0x40 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781080, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:128 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_eight_with_sampler[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xc0c00f00, /* s_load_dwordx8 s[0:7], s[14:15], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen */
    0xc0c00f08, /* s_load_dwordx8 s[0:7], s[14:15], 0x8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0xc0c00f10, /* s_load_dwordx8 s[0:7], s[14:15], 0x10 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0xc0c00f18, /* s_load_dwordx8 s[0:7], s[14:15], 0x18 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0xc0c00f20, /* s_load_dwordx8 s[0:7], s[14:15], 0x20 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0xc0c00f28, /* s_load_dwordx8 s[0:7], s[14:15], 0x28 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0xc0c00f30, /* s_load_dwordx8 s[0:7], s[14:15], 0x30 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0xc0c00f38, /* s_load_dwordx8 s[0:7], s[14:15], 0x38 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_two_samplers[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3e800000, /* v_mov_b32_e32 v3, 0x3e800000 */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xc0840f00, /* s_load_dwordx4 s[8:11], s[14:15], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400402, /* image_sample v[4:7], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xc0840f04, /* s_load_dwordx4 s[8:11], s[14:15], 0x4 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008040c, /* buffer_store_dwordx4 v[4:7], v12, s[32:35], 0 offen */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_eight_samplers[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f00, /* s_load_dwordx4 s[8:11], s[14:15], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f04, /* s_load_dwordx4 s[8:11], s[14:15], 0x4 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f08, /* s_load_dwordx4 s[8:11], s[14:15], 0x8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f0c, /* s_load_dwordx4 s[8:11], s[14:15], 0xc */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f10, /* s_load_dwordx4 s[8:11], s[14:15], 0x10 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f14, /* s_load_dwordx4 s[8:11], s[14:15], 0x14 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f18, /* s_load_dwordx4 s[8:11], s[14:15], 0x18 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0xc0840f1c, /* s_load_dwordx4 s[8:11], s[14:15], 0x1c */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_nine_samplers[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f00, /* s_load_dwordx4 s[8:11], s[14:15], 0x0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f04, /* s_load_dwordx4 s[8:11], s[14:15], 0x4 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f08, /* s_load_dwordx4 s[8:11], s[14:15], 0x8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f0c, /* s_load_dwordx4 s[8:11], s[14:15], 0xc */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f10, /* s_load_dwordx4 s[8:11], s[14:15], 0x10 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f14, /* s_load_dwordx4 s[8:11], s[14:15], 0x14 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f18, /* s_load_dwordx4 s[8:11], s[14:15], 0x18 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0xc0840f1c, /* s_load_dwordx4 s[8:11], s[14:15], 0x1c */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 0x3fa00000 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 0x3fa00000 */
    0xc0840f20, /* s_load_dwordx4 s[8:11], s[14:15], 0x20 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781080, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:128 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

#endif
