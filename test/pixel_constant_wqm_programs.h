/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independently authored from the public AMD GCN S_WQM_B64 definition: each nonempty
   four-bit group expands to four set bits; SCC records whether the result is nonzero.
   https://gpuopen.com/download/AMD_GCN3_Instruction_Set_Architecture_rev1.1.pdf
   Encodings: LLVM llvm-mc -triple=amdgcn-amd-amdhsa -mcpu=bonaire (GFX7).
   These are owned synthetic programs, not captured shader bytes.

   Compound positives expose the destination in red and SCC in green. SCC starts opposite
   the expected value and is captured before restoring EXEC. v2 starts at0, so only result-
   active lanes make red1. Full is yellow; empty is black; alpha is always1.
   The independent simple-initial program is magenta. Unknown/conservative cases are
   translation/refusal witnesses; their RGBA entries are unused. No logical64 mask claim. */
#ifndef TEST_PIXEL_CONSTANT_WQM_PROGRAMS_H
#define TEST_PIXEL_CONSTANT_WQM_PROGRAMS_H
#include <stddef.h>
#include <stdint.h>

enum { PIXEL_CONSTANT_WQM_USER_SGPRS = 0, PIXEL_CONSTANT_WQM_SGPRS = 12, PIXEL_CONSTANT_WQM_VGPRS = 6 };
typedef struct {
    const char *name;
    const uint32_t *code;
    size_t dwords;
    unsigned expected_quad_swaps;
    unsigned char rgba[4];
} pb_pixel_constant_wqm_program;

/* literal_full_exec:
   v_mov_b32 v2, 0
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, -1
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_full_exec[] = {
    0x7e040280u, 0xbf068180u, 0xbefe0ac1u, 0x850080f2u, 0x7e0402f2u, 0xbefe04c1u,
    0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* literal_empty_exec:
   v_mov_b32 v2, 0
   s_cmp_eq_u32 0, 0
   s_wqm_b64 exec, 0
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_empty_exec[] = {
    0x7e040280u, 0xbf068080u, 0xbefe0a80u, 0x850080f2u, 0x7e0402f2u, 0xbefe04c1u,
    0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* literal_full_vcc:
   v_mov_b32 v2, 0
   s_mov_b64 vcc, 0
   s_cmp_eq_u32 0, 1
   s_wqm_b64 vcc, -1
   s_cselect_b32 s0, 1.0, 0
   s_mov_b64 exec, vcc
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_full_vcc[] = {
    0x7e040280u, 0xbeea0480u, 0xbf068180u, 0xbeea0ac1u, 0x850080f2u, 0xbefe046au,
    0x7e0402f2u, 0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu,
    0x05040302u, 0xbf810000u,
};

/* literal_empty_vcc:
   v_mov_b32 v2, 0
   s_mov_b64 vcc, -1
   s_cmp_eq_u32 0, 0
   s_wqm_b64 vcc, 0
   s_cselect_b32 s0, 1.0, 0
   s_mov_b64 exec, vcc
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_empty_vcc[] = {
    0x7e040280u, 0xbeea04c1u, 0xbf068080u, 0xbeea0a80u, 0x850080f2u, 0xbefe046au,
    0x7e0402f2u, 0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu,
    0x05040302u, 0xbf810000u,
};

/* literal_full_pair:
   v_mov_b32 v2, 0
   s_mov_b64 s[10:11], 0
   s_cmp_eq_u32 0, 1
   s_wqm_b64 s[10:11], -1
   s_cselect_b32 s0, 1.0, 0
   s_mov_b64 exec, s[10:11]
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_full_pair[] = {
    0x7e040280u, 0xbe8a0480u, 0xbf068180u, 0xbe8a0ac1u, 0x850080f2u, 0xbefe040au,
    0x7e0402f2u, 0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu,
    0x05040302u, 0xbf810000u,
};

/* literal_empty_pair:
   v_mov_b32 v2, 0
   s_mov_b64 s[10:11], -1
   s_cmp_eq_u32 0, 0
   s_wqm_b64 s[10:11], 0
   s_cselect_b32 s0, 1.0, 0
   s_mov_b64 exec, s[10:11]
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_empty_pair[] = {
    0x7e040280u, 0xbe8a04c1u, 0xbf068080u, 0xbe8a0a80u, 0x850080f2u, 0xbefe040au,
    0x7e0402f2u, 0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu,
    0x05040302u, 0xbf810000u,
};

/* initial_exec_full:
   v_mov_b32 v2, 0
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_initial_exec_full[] = {
    0x7e040280u, 0xbf068180u, 0xbefe0a7eu, 0x850080f2u, 0x7e0402f2u, 0xbefe04c1u,
    0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* moved_exec_full:
   v_mov_b32 v2, 0
   s_mov_b64 exec, -1
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_moved_exec_full[] = {
    0x7e040280u, 0xbefe04c1u, 0xbf068180u, 0xbefe0a7eu, 0x850080f2u, 0x7e0402f2u,
    0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u,
    0xbf810000u,
};

/* moved_exec_empty:
   v_mov_b32 v2, 0
   s_mov_b64 exec, 0
   s_cmp_eq_u32 0, 0
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_moved_exec_empty[] = {
    0x7e040280u, 0xbefe0480u, 0xbf068080u, 0xbefe0a7eu, 0x850080f2u, 0x7e0402f2u,
    0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u,
    0xbf810000u,
};

/* initial_exec_simple:
   s_wqm_b64 exec, exec
   v_mov_b32 v2, 1.0
   v_mov_b32 v3, 0
   v_mov_b32 v4, 1.0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_initial_exec_simple[] = {
    0xbefe0a7eu, 0x7e0402f2u, 0x7e060280u, 0x7e0802f2u, 0x7e0a02f2u, 0xf800180fu,
    0x05040302u, 0xbf810000u,
};

/* literal_full_after_branch:
   v_mov_b32 v2, 0
   s_branch .Lwqm
   .Lwqm:
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, -1
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_full_after_branch[] = {
    0x7e040280u, 0xbf820000u, 0xbf068180u, 0xbefe0ac1u, 0x850080f2u, 0x7e0402f2u,
    0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u,
    0xbf810000u,
};

/* literal_full_after_export:
   v_mov_b32 v2, 0
   v_mov_b32 v3, 0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, -1
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_full_after_export[] = {
    0x7e040280u, 0x7e060280u, 0x7e080280u, 0x7e0a02f2u, 0xf800000fu, 0x05040302u,
    0xbf068180u, 0xbefe0ac1u, 0x850080f2u, 0x7e0402f2u, 0xbefe04c1u, 0x7e060200u,
    0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* literal_empty_after_branch:
   v_mov_b32 v2, 0
   s_branch .Lwqm
   .Lwqm:
   s_cmp_eq_u32 0, 0
   s_wqm_b64 exec, 0
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_empty_after_branch[] = {
    0x7e040280u, 0xbf820000u, 0xbf068080u, 0xbefe0a80u, 0x850080f2u, 0x7e0402f2u,
    0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u,
    0xbf810000u,
};

/* literal_empty_after_export:
   v_mov_b32 v2, 0
   v_mov_b32 v3, 0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5
   s_cmp_eq_u32 0, 0
   s_wqm_b64 exec, 0
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_literal_empty_after_export[] = {
    0x7e040280u, 0x7e060280u, 0x7e080280u, 0x7e0a02f2u, 0xf800000fu, 0x05040302u,
    0xbf068080u, 0xbefe0a80u, 0x850080f2u, 0x7e0402f2u, 0xbefe04c1u, 0x7e060200u,
    0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* unknown_initial_across_branch:
   v_mov_b32 v2, 0
   s_branch .Lwqm
   .Lwqm:
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_unknown_initial_across_branch[] = {
    0x7e040280u, 0xbf820000u, 0xbf068180u, 0xbefe0a7eu, 0x850080f2u, 0x7e0402f2u,
    0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u,
    0xbf810000u,
};

/* unknown_cmpx_exec:
   v_mov_b32 v2, 0
   v_mov_b32 v0, 0
   v_cmpx_eq_u32 vcc, 0, v0
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_unknown_cmpx_exec[] = {
    0x7e040280u, 0x7e000280u, 0x7da40080u, 0xbf068180u, 0xbefe0a7eu, 0x850080f2u,
    0x7e0402f2u, 0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu,
    0x05040302u, 0xbf810000u,
};

/* unknown_compare_vcc:
   v_mov_b32 v2, 0
   v_mov_b32 v0, 0
   v_cmp_eq_u32 vcc, 0, v0
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, vcc
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_unknown_compare_vcc[] = {
    0x7e040280u, 0x7e000280u, 0x7d840080u, 0xbf068180u, 0xbefe0a6au, 0x850080f2u,
    0x7e0402f2u, 0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu,
    0x05040302u, 0xbf810000u,
};

/* unknown_compare_pair:
   v_mov_b32 v2, 0
   v_mov_b32 v0, 0
   v_cmp_eq_u32_e64 s[10:11], 0, v0
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, s[10:11]
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_unknown_compare_pair[] = {
    0x7e040280u, 0x7e000280u, 0xd184000au, 0x00020080u, 0xbf068180u, 0xbefe0a0au,
    0x850080f2u, 0x7e0402f2u, 0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u,
    0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* unknown_saveexec:
   v_mov_b32 v2, 0
   s_and_saveexec_b64 s[10:11], 0
   s_cmp_eq_u32 0, 0
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_unknown_saveexec[] = {
    0x7e040280u, 0xbe8a2480u, 0xbf068080u, 0xbefe0a7eu, 0x850080f2u, 0x7e0402f2u,
    0xbefe04c1u, 0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u,
    0xbf810000u,
};

/* unknown_after_export:
   v_mov_b32 v2, 0
   v_mov_b32 v3, 0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_unknown_after_export[] = {
    0x7e040280u, 0x7e060280u, 0x7e080280u, 0x7e0a02f2u, 0xf800000fu, 0x05040302u,
    0xbf068180u, 0xbefe0a7eu, 0x850080f2u, 0x7e0402f2u, 0xbefe04c1u, 0x7e060200u,
    0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* unknown_after_export_then_full_move:
   v_mov_b32 v2, 0
   v_mov_b32 v3, 0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5
   s_mov_b64 exec, -1
   s_cmp_eq_u32 0, 1
   s_wqm_b64 exec, exec
   s_cselect_b32 s0, 1.0, 0
   v_mov_b32 v2, 1.0
   s_mov_b64 exec, -1
   v_mov_b32 v3, s0
   v_mov_b32 v4, 0
   v_mov_b32 v5, 1.0
   exp mrt0 v2, v3, v4, v5 done vm
   s_endpgm */
static const uint32_t pixel_wqm_unknown_after_export_then_full_move[] = {
    0x7e040280u, 0x7e060280u, 0x7e080280u, 0x7e0a02f2u, 0xf800000fu, 0x05040302u,
    0xbefe04c1u, 0xbf068180u, 0xbefe0a7eu, 0x850080f2u, 0x7e0402f2u, 0xbefe04c1u,
    0x7e060200u, 0x7e080280u, 0x7e0a02f2u, 0xf800180fu, 0x05040302u, 0xbf810000u,
};

/* nonpixel_literal_full:
   s_wqm_b64 exec, -1
   s_endpgm */
static const uint32_t pixel_wqm_nonpixel_literal_full[] = {
    0xbefe0ac1u, 0xbf810000u,
};

/* nonpixel_literal_empty:
   s_wqm_b64 exec, 0
   s_endpgm */
static const uint32_t pixel_wqm_nonpixel_literal_empty[] = {
    0xbefe0a80u, 0xbf810000u,
};

static const pb_pixel_constant_wqm_program pixel_constant_wqm_positive[] = {
    {"literal_full_exec", pixel_wqm_literal_full_exec, sizeof pixel_wqm_literal_full_exec / sizeof pixel_wqm_literal_full_exec[0], 0, {255, 255, 0, 255}},
    {"literal_empty_exec", pixel_wqm_literal_empty_exec, sizeof pixel_wqm_literal_empty_exec / sizeof pixel_wqm_literal_empty_exec[0], 0, {0, 0, 0, 255}},
    {"literal_full_vcc", pixel_wqm_literal_full_vcc, sizeof pixel_wqm_literal_full_vcc / sizeof pixel_wqm_literal_full_vcc[0], 0, {255, 255, 0, 255}},
    {"literal_empty_vcc", pixel_wqm_literal_empty_vcc, sizeof pixel_wqm_literal_empty_vcc / sizeof pixel_wqm_literal_empty_vcc[0], 0, {0, 0, 0, 255}},
    {"literal_full_pair", pixel_wqm_literal_full_pair, sizeof pixel_wqm_literal_full_pair / sizeof pixel_wqm_literal_full_pair[0], 0, {255, 255, 0, 255}},
    {"literal_empty_pair", pixel_wqm_literal_empty_pair, sizeof pixel_wqm_literal_empty_pair / sizeof pixel_wqm_literal_empty_pair[0], 0, {0, 0, 0, 255}},
    {"initial_exec_full", pixel_wqm_initial_exec_full, sizeof pixel_wqm_initial_exec_full / sizeof pixel_wqm_initial_exec_full[0], 0, {255, 255, 0, 255}},
    {"moved_exec_full", pixel_wqm_moved_exec_full, sizeof pixel_wqm_moved_exec_full / sizeof pixel_wqm_moved_exec_full[0], 0, {255, 255, 0, 255}},
    {"moved_exec_empty", pixel_wqm_moved_exec_empty, sizeof pixel_wqm_moved_exec_empty / sizeof pixel_wqm_moved_exec_empty[0], 0, {0, 0, 0, 255}},
    {"initial_exec_simple", pixel_wqm_initial_exec_simple, sizeof pixel_wqm_initial_exec_simple / sizeof pixel_wqm_initial_exec_simple[0], 0, {255, 0, 255, 255}},
    {"literal_full_after_branch", pixel_wqm_literal_full_after_branch, sizeof pixel_wqm_literal_full_after_branch / sizeof pixel_wqm_literal_full_after_branch[0], 0, {255, 255, 0, 255}},
    {"literal_full_after_export", pixel_wqm_literal_full_after_export, sizeof pixel_wqm_literal_full_after_export / sizeof pixel_wqm_literal_full_after_export[0], 0, {255, 255, 0, 255}},
    {"literal_empty_after_branch", pixel_wqm_literal_empty_after_branch, sizeof pixel_wqm_literal_empty_after_branch / sizeof pixel_wqm_literal_empty_after_branch[0], 0, {0, 0, 0, 255}},
    {"literal_empty_after_export", pixel_wqm_literal_empty_after_export, sizeof pixel_wqm_literal_empty_after_export / sizeof pixel_wqm_literal_empty_after_export[0], 0, {0, 0, 0, 255}},
};

static const pb_pixel_constant_wqm_program pixel_constant_wqm_negative[] = {
    {"unknown_initial_across_branch", pixel_wqm_unknown_initial_across_branch, sizeof pixel_wqm_unknown_initial_across_branch / sizeof pixel_wqm_unknown_initial_across_branch[0], 3, {255, 255, 0, 255}},
    {"unknown_cmpx_exec", pixel_wqm_unknown_cmpx_exec, sizeof pixel_wqm_unknown_cmpx_exec / sizeof pixel_wqm_unknown_cmpx_exec[0], 3, {255, 255, 0, 255}},
    {"unknown_compare_vcc", pixel_wqm_unknown_compare_vcc, sizeof pixel_wqm_unknown_compare_vcc / sizeof pixel_wqm_unknown_compare_vcc[0], 3, {255, 255, 0, 255}},
    {"unknown_compare_pair", pixel_wqm_unknown_compare_pair, sizeof pixel_wqm_unknown_compare_pair / sizeof pixel_wqm_unknown_compare_pair[0], 3, {255, 255, 0, 255}},
    {"unknown_saveexec", pixel_wqm_unknown_saveexec, sizeof pixel_wqm_unknown_saveexec / sizeof pixel_wqm_unknown_saveexec[0], 3, {0, 0, 0, 255}},
    {"unknown_after_export", pixel_wqm_unknown_after_export, sizeof pixel_wqm_unknown_after_export / sizeof pixel_wqm_unknown_after_export[0], 3, {255, 255, 0, 255}},
    {"unknown_after_export_then_full_move", pixel_wqm_unknown_after_export_then_full_move, sizeof pixel_wqm_unknown_after_export_then_full_move / sizeof pixel_wqm_unknown_after_export_then_full_move[0], 3, {255, 255, 0, 255}},
};

#define PIXEL_CONSTANT_WQM_POSITIVE_COUNT (sizeof pixel_constant_wqm_positive / sizeof pixel_constant_wqm_positive[0])
#define PIXEL_CONSTANT_WQM_NEGATIVE_COUNT (sizeof pixel_constant_wqm_negative / sizeof pixel_constant_wqm_negative[0])
#endif
