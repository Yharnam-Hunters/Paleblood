/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independently owned GFX7 pixel program. Encodings produced by LLVM llvm-mc
   -triple=amdgcn-amd-amdhsa -mcpu=bonaire; no game instruction stream is used. */
#ifndef TEST_METAL_FRAGMENT_PROGRAMS_H
#define TEST_METAL_FRAGMENT_PROGRAMS_H
#include <stdint.h>

/* User s0/s1 are ordered x/y thresholds; s2 is a red value. Interpolated attr0.xy
   narrows EXEC to x<s0 && y<s1. Only the red write uses whole-quad EXEC; the saved
   conditional EXEC and then the original coverage EXEC are restored before export.
   Baseline RGBA is (0,.25,.5,1), and no instruction reads the generated SCC flags. */
static const uint32_t pixel_masked_exec_wqm[] = {
    0xc8080000, /* v_interp_p1_f32 v2, v0, attr0.x */
    0xc8090001, /* v_interp_p2_f32 v2, v1, attr0.x */
    0xc80c0100, /* v_interp_p1_f32 v3, v0, attr0.y */
    0xc80d0101, /* v_interp_p2_f32 v3, v1, attr0.y */
    0x7e080280, /* v_mov_b32_e32 v4, 0 */
    0x7e0a02ff, 0x3e800000, /* v_mov_b32_e32 v5, 0x3e800000 */
    0x7e0c02f0, /* v_mov_b32_e32 v6, 0.5 */
    0x7e0e02f2, /* v_mov_b32_e32 v7, 1.0 */
    0xd0020014, 0x00000102, /* v_cmp_lt_f32_e64 s[20:21], v2, s0 */
    0xd0020018, 0x00000303, /* v_cmp_lt_f32_e64 s[24:25], v3, s1 */
    0x87941814, /* s_and_b64 s[20:21], s[20:21], s[24:25] */
    0xbe962414, /* s_and_saveexec_b64 s[22:23], s[20:21] */
    0xbf880004, /* s_cbranch_execz JOIN (four dwords forward) */
    0xbe9a047e, /* s_mov_b64 s[26:27], exec */
    0xbefe0a7e, /* s_wqm_b64 exec, exec */
    0x7e080202, /* v_mov_b32_e32 v4, s2 */
    0xbefe041a, /* s_mov_b64 exec, s[26:27] */
    0xbefe0416, /* JOIN: s_mov_b64 exec, s[22:23] */
    0xf800180f, 0x07060504, /* exp mrt0, v4, v5, v6, v7 done vm */
    0xbf810000, /* s_endpgm */
};
#endif
