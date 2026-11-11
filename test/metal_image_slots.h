/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_METAL_IMAGE_SLOTS_H
#define TEST_METAL_IMAGE_SLOTS_H
/* Owned slot probes, assembled with LLVM llvm-mc -triple=amdgcn -mcpu=bonaire
   -show-encoding. Each comment is the corresponding assembler input. */
#include <stdint.h>

static const uint32_t image_probe_sixteen_images[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xc0c00f00, /* s_load_dwordx8 s[0:7], s[14:15], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:0 */
    0xc0c00f08, /* s_load_dwordx8 s[0:7], s[14:15], 8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0xc0c00f10, /* s_load_dwordx8 s[0:7], s[14:15], 16 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0xc0c00f18, /* s_load_dwordx8 s[0:7], s[14:15], 24 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0xc0c00f20, /* s_load_dwordx8 s[0:7], s[14:15], 32 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0xc0c00f28, /* s_load_dwordx8 s[0:7], s[14:15], 40 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0xc0c00f30, /* s_load_dwordx8 s[0:7], s[14:15], 48 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0xc0c00f38, /* s_load_dwordx8 s[0:7], s[14:15], 56 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0xc0c00f40, /* s_load_dwordx8 s[0:7], s[14:15], 64 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781080, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:128 */
    0xc0c00f48, /* s_load_dwordx8 s[0:7], s[14:15], 72 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781090, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:144 */
    0xc0c00f50, /* s_load_dwordx8 s[0:7], s[14:15], 80 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810a0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:160 */
    0xc0c00f58, /* s_load_dwordx8 s[0:7], s[14:15], 88 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810b0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:176 */
    0xc0c00f60, /* s_load_dwordx8 s[0:7], s[14:15], 96 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810c0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:192 */
    0xc0c00f68, /* s_load_dwordx8 s[0:7], s[14:15], 104 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810d0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:208 */
    0xc0c00f70, /* s_load_dwordx8 s[0:7], s[14:15], 112 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810e0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:224 */
    0xc0c00f78, /* s_load_dwordx8 s[0:7], s[14:15], 120 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810f0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:240 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0 v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_sixteen_with_sampler[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xc0c00f00, /* s_load_dwordx8 s[0:7], s[14:15], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:0 */
    0xc0c00f08, /* s_load_dwordx8 s[0:7], s[14:15], 8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0xc0c00f10, /* s_load_dwordx8 s[0:7], s[14:15], 16 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0xc0c00f18, /* s_load_dwordx8 s[0:7], s[14:15], 24 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0xc0c00f20, /* s_load_dwordx8 s[0:7], s[14:15], 32 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0xc0c00f28, /* s_load_dwordx8 s[0:7], s[14:15], 40 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0xc0c00f30, /* s_load_dwordx8 s[0:7], s[14:15], 48 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0xc0c00f38, /* s_load_dwordx8 s[0:7], s[14:15], 56 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0xc0c00f40, /* s_load_dwordx8 s[0:7], s[14:15], 64 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781080, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:128 */
    0xc0c00f48, /* s_load_dwordx8 s[0:7], s[14:15], 72 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781090, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:144 */
    0xc0c00f50, /* s_load_dwordx8 s[0:7], s[14:15], 80 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810a0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:160 */
    0xc0c00f58, /* s_load_dwordx8 s[0:7], s[14:15], 88 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810b0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:176 */
    0xc0c00f60, /* s_load_dwordx8 s[0:7], s[14:15], 96 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810c0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:192 */
    0xc0c00f68, /* s_load_dwordx8 s[0:7], s[14:15], 104 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810d0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:208 */
    0xc0c00f70, /* s_load_dwordx8 s[0:7], s[14:15], 112 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810e0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:224 */
    0xc0c00f78, /* s_load_dwordx8 s[0:7], s[14:15], 120 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810f0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:240 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0 v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_seventeen_images[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0xc0c00f00, /* s_load_dwordx8 s[0:7], s[14:15], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:0 */
    0xc0c00f08, /* s_load_dwordx8 s[0:7], s[14:15], 8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0xc0c00f10, /* s_load_dwordx8 s[0:7], s[14:15], 16 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0xc0c00f18, /* s_load_dwordx8 s[0:7], s[14:15], 24 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0xc0c00f20, /* s_load_dwordx8 s[0:7], s[14:15], 32 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0xc0c00f28, /* s_load_dwordx8 s[0:7], s[14:15], 40 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0xc0c00f30, /* s_load_dwordx8 s[0:7], s[14:15], 48 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0xc0c00f38, /* s_load_dwordx8 s[0:7], s[14:15], 56 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0xc0c00f40, /* s_load_dwordx8 s[0:7], s[14:15], 64 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781080, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:128 */
    0xc0c00f48, /* s_load_dwordx8 s[0:7], s[14:15], 72 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781090, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:144 */
    0xc0c00f50, /* s_load_dwordx8 s[0:7], s[14:15], 80 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810a0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:160 */
    0xc0c00f58, /* s_load_dwordx8 s[0:7], s[14:15], 88 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810b0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:176 */
    0xc0c00f60, /* s_load_dwordx8 s[0:7], s[14:15], 96 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810c0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:192 */
    0xc0c00f68, /* s_load_dwordx8 s[0:7], s[14:15], 104 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810d0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:208 */
    0xc0c00f70, /* s_load_dwordx8 s[0:7], s[14:15], 112 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810e0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:224 */
    0xc0c00f78, /* s_load_dwordx8 s[0:7], s[14:15], 120 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810f0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:240 */
    0xc0c00f80, /* s_load_dwordx8 s[0:7], s[14:15], 128 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e040280, /* v_mov_b32_e32 v2, 0 */
    0x7e060280, /* v_mov_b32_e32 v3, 0 */
    0xf0000f00, 0x00000802, /* image_load v[8:11], v[2:3], s[0:7] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781100, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:256 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0 v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_sixteen_samplers[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f00, /* s_load_dwordx4 s[8:11], s[14:15], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:0 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f04, /* s_load_dwordx4 s[8:11], s[14:15], 4 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f08, /* s_load_dwordx4 s[8:11], s[14:15], 8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f0c, /* s_load_dwordx4 s[8:11], s[14:15], 12 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f10, /* s_load_dwordx4 s[8:11], s[14:15], 16 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f14, /* s_load_dwordx4 s[8:11], s[14:15], 20 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f18, /* s_load_dwordx4 s[8:11], s[14:15], 24 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f1c, /* s_load_dwordx4 s[8:11], s[14:15], 28 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f20, /* s_load_dwordx4 s[8:11], s[14:15], 32 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781080, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:128 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f24, /* s_load_dwordx4 s[8:11], s[14:15], 36 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781090, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:144 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f28, /* s_load_dwordx4 s[8:11], s[14:15], 40 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810a0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:160 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f2c, /* s_load_dwordx4 s[8:11], s[14:15], 44 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810b0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:176 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f30, /* s_load_dwordx4 s[8:11], s[14:15], 48 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810c0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:192 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f34, /* s_load_dwordx4 s[8:11], s[14:15], 52 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810d0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:208 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f38, /* s_load_dwordx4 s[8:11], s[14:15], 56 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810e0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:224 */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0xc0840f3c, /* s_load_dwordx4 s[8:11], s[14:15], 60 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810f0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:240 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0 v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

static const uint32_t image_probe_seventeen_samplers[] = {
    0xc0900d00, /* s_load_dwordx4 s[32:35], s[12:13], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0x7e180280, /* v_mov_b32_e32 v12, 0 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f00, /* s_load_dwordx4 s[8:11], s[14:15], 0 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781000, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:0 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f04, /* s_load_dwordx4 s[8:11], s[14:15], 4 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781010, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:16 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f08, /* s_load_dwordx4 s[8:11], s[14:15], 8 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781020, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:32 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f0c, /* s_load_dwordx4 s[8:11], s[14:15], 12 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781030, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:48 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f10, /* s_load_dwordx4 s[8:11], s[14:15], 16 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781040, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:64 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f14, /* s_load_dwordx4 s[8:11], s[14:15], 20 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781050, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:80 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f18, /* s_load_dwordx4 s[8:11], s[14:15], 24 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781060, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:96 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f1c, /* s_load_dwordx4 s[8:11], s[14:15], 28 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781070, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:112 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f20, /* s_load_dwordx4 s[8:11], s[14:15], 32 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781080, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:128 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f24, /* s_load_dwordx4 s[8:11], s[14:15], 36 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781090, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:144 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f28, /* s_load_dwordx4 s[8:11], s[14:15], 40 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810a0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:160 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f2c, /* s_load_dwordx4 s[8:11], s[14:15], 44 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810b0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:176 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f30, /* s_load_dwordx4 s[8:11], s[14:15], 48 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810c0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:192 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f34, /* s_load_dwordx4 s[8:11], s[14:15], 52 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810d0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:208 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f38, /* s_load_dwordx4 s[8:11], s[14:15], 56 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810e0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:224 */
    0x7e0402ff, 0x3fa00000, /* v_mov_b32_e32 v2, 1.25 */
    0x7e0602ff, 0x3fa00000, /* v_mov_b32_e32 v3, 1.25 */
    0xc0840f3c, /* s_load_dwordx4 s[8:11], s[14:15], 60 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe07810f0, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:240 */
    0x7e0402f0, /* v_mov_b32_e32 v2, 0.5 */
    0x7e0602f0, /* v_mov_b32_e32 v3, 0.5 */
    0xc0840f40, /* s_load_dwordx4 s[8:11], s[14:15], 64 */
    0xbf8c007f, /* s_waitcnt lgkmcnt(0) */
    0xf0800f00, 0x00400802, /* image_sample v[8:11], v[2:3], s[0:7], s[8:11] dmask:0xf */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xe0781100, 0x8008080c, /* buffer_store_dwordx4 v[8:11], v12, s[32:35], 0 offen offset:256 */
    0x7e000280, /* v_mov_b32_e32 v0, 0 */
    0xf800180f, 0x00000000, /* exp mrt0 v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};

#endif
