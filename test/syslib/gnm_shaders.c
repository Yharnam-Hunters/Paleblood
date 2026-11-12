/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceGnmSetVsShader, sceGnmSetPsShader and their Update forms, and the tessellation and geometry
   stages' setters (runtime/syslib/gnm.c): the reserved space becomes one NOP packet, the shader goes
   to the renderer (a no-op in this build, without graphics), and a shader modifier with bits outside
   PGM_RSRC1's SGPRS and VGPR_COMP_CNT stops by name. */
#include "../syslib_test.h"

#include <stdint.h>

typedef int32_t (*f_set_vs)(uint32_t *, uint32_t, const uint32_t *, uint32_t);
typedef int32_t (*f_set_ps)(uint32_t *, uint32_t, const uint32_t *);

static f_set_vs set_vs, set_ls;

static void set_ls_with_modifier(void)
{
    uint32_t space[23];
    const uint32_t registers[4] = {0};
    set_ls(space, 23, registers, 0x00800040);  /* bit 23 with the SGPRS field: outside both fields */
}

static void set_vs_with_modifier(void)
{
    uint32_t space[29];
    const uint32_t registers[7] = {0};
    set_vs(space, 29, registers, 0x00800000);  /* bit 23: outside both fields */
}

SYSLIB_TEST(gnm_shaders)
{
    set_vs = (f_set_vs)fn("libSceGnmDriver", "sceGnmSetVsShader");
    const f_set_vs update_vs = (f_set_vs)fn("libSceGnmDriver", "sceGnmUpdateVsShader");
    const f_set_ps set_ps = (f_set_ps)fn("libSceGnmDriver", "sceGnmSetPsShader");
    const f_set_ps update_ps = (f_set_ps)fn("libSceGnmDriver", "sceGnmUpdatePsShader");
    if (!set_vs || !update_vs || !set_ps || !update_ps) return;
    CHECK(is_implemented("libSceGnmDriver", "sceGnmSetVsShader") && is_implemented("libSceGnmDriver", "sceGnmSetPsShader"));

    const uint32_t vertex[7] = {0x20d1c3c2, 0, 0x000c0042, 0xc, 0, 4, 0};
    const uint32_t pixel[12] = {0x20d1c3c5, 0, 0x000c0041, 0x10, 0, 4, 2, 2, 1, 0, 0x10, 0xf};
    uint32_t space[40];
    for (int i = 0; i < 40; i++) space[i] = 0xdeadbeef;
    CHECK(set_vs(space, 29, vertex, 0) == 0);
    CHECK(space[0] == (0xc0001000u | 27u << 16) && space[1] == 0 && space[28] == 0 && space[29] == 0xdeadbeef);
    CHECK(update_vs(space, 29, vertex, 0) == 0);
    CHECK(set_vs(space, 29, vertex, 0x01000080) == 0);  /* VGPR_COMP_CNT 1, SGPRS 2 */
    CHECK(set_ps(space, 40, pixel) == 0 && space[0] == (0xc0001000u | 38u << 16) && space[39] == 0);
    CHECK(update_ps(space, 40, pixel) == 0);
    CHECK(set_ps(space, 40, NULL) == 0);  /* no pixel shader: without the renderer, nothing to stop */
    CHECK(died_in_fatal(set_vs_with_modifier));
    typedef int32_t (*f_set_cs)(uint32_t *, uint32_t, const uint32_t *);
    const f_set_cs set_cs = (f_set_cs)fn("libSceGnmDriver", "sceGnmSetCsShader");
    const uint32_t compute[7] = {0x20d1c3c0, 0, 0x000c0042, 0x10, 64, 1, 1};
    CHECK(set_cs && set_cs(space, 25, compute) == 0 && space[0] == (0xc0001000u | 23u << 16) && space[24] == 0);

    /* the tessellation and geometry stages (a boot's local and hull blocks) */
    set_ls = (f_set_vs)fn("libSceGnmDriver", "sceGnmSetLsShader");
    const f_set_vs set_hs = (f_set_vs)fn("libSceGnmDriver", "sceGnmSetHsShader");
    const f_set_vs update_hs = (f_set_vs)fn("libSceGnmDriver", "sceGnmUpdateHsShader");
    const f_set_vs set_es = (f_set_vs)fn("libSceGnmDriver", "sceGnmSetEsShader");
    const f_set_ps set_gs = (f_set_ps)fn("libSceGnmDriver", "sceGnmSetGsShader");
    const f_set_ps update_gs = (f_set_ps)fn("libSceGnmDriver", "sceGnmUpdateGsShader");
    if (!set_ls || !set_hs || !update_hs || !set_es || !set_gs || !update_gs) return;
    const uint32_t local[4] = {0x20d52d5b, 0, 0x010c0048, 0x38c};
    const uint32_t hull[7] = {0x20d52e01, 0, 0x000c0043, 0x10, 0x42, 0x40400000, 0x3f800000};
    const uint32_t geometry[7] = {0x20d52d7d, 0, 0x000c0149, 0x10, 0, 2, 0};
    for (int i = 0; i < 40; i++) space[i] = 0xdeadbeef;
    CHECK(set_ls(space, 23, local, 0x03000040) == 0 && space[0] == (0xc0001000u | 21u << 16) && space[22] == 0 && space[23] == 0xdeadbeef);
    CHECK(set_ls(space, 23, local, 0x00000080) == 0); /* SGPRS alone */
    CHECK(set_hs(space, 30, hull, 0x410f) == 0 && space[0] == (0xc0001000u | 28u << 16) && space[29] == 0);
    CHECK(update_hs(space, 30, hull, 0x410f) == 0);
    CHECK(set_es(space, 20, local, 0x03000080) == 0 && space[19] == 0);
    CHECK(set_gs(space, 29, geometry) == 0 && update_gs(space, 29, geometry) == 0 && space[28] == 0);
    CHECK(died_in_fatal(set_ls_with_modifier));
}
