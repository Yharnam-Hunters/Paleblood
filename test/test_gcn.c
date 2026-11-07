/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GCN decoder (runtime/renderer/gcn_decode.c) on synthetic GFX7 instructions. Each case's
   encoding was produced by LLVM's AMDGPU assembler (llvm-mc -triple=amdgcn -mcpu=bonaire
   --show-encoding) from the instruction in its comment: an independent check of the encodings
   and of the generated opcode table. No game code is used. */
#include "runtime/gcn.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct {
    uint32_t words[3];
    unsigned dwords;
    const char *name;
} decode_case;

static const decode_case cases[] = {
    {{0xbf810000}, 1, "s_endpgm"}, /* s_endpgm */
    {{0xbe810302}, 1, "s_mov_b32"}, /* s_mov_b32 s1, s2 */
    {{0xbe8103ff, 0x12345678}, 2, "s_mov_b32"}, /* s_mov_b32 s1, 0x12345678 */
    {{0x8001ff02, 0x00001000}, 2, "s_add_u32"}, /* s_add_u32 s1, s2, 0x1000 */
    {{0xbf060201}, 1, "s_cmp_eq_u32"}, /* s_cmp_eq_u32 s1, s2 */
    {{0xb0010010}, 1, "s_movk_i32"}, /* s_movk_i32 s1, 0x10 */
    {{0xbf8c0f70}, 1, "s_waitcnt"}, /* s_waitcnt vmcnt(0) */
    {{0x7e020302}, 1, "v_mov_b32"}, /* v_mov_b32 v1, v2 */
    {{0x7e0202ff, 0x40490fdb}, 2, "v_mov_b32"}, /* v_mov_b32 v1, 0x40490fdb */
    {{0x06020702}, 1, "v_add_f32"}, /* v_add_f32 v1, v2, v3 */
    {{0x40020702, 0x40000000}, 2, "v_madmk_f32"}, /* v_madmk_f32 v1, v2, 0x40000000, v3 */
    {{0x42020702, 0x3f800000}, 2, "v_madak_f32"}, /* v_madak_f32 v1, v2, v3, 0x3f800000 */
    {{0x7c080702}, 1, "v_cmp_gt_f32"}, /* v_cmp_gt_f32 vcc, v2, v3 */
    {{0xd2060001, 0x00020702}, 2, "v_add_f32"}, /* v_add_f32_e64 v1, v2, v3 */
    {{0xd0080002, 0x00020702}, 2, "v_cmp_gt_f32"}, /* v_cmp_gt_f32_e64 s[2:3], v2, v3 */
    {{0xd3540001, 0x00000102}, 2, "v_rcp_f32"}, /* v_rcp_f32_e64 v1, v2 */
    {{0xd2820001, 0x04120702}, 2, "v_mad_f32"}, /* v_mad_f32 v1, v2, v3, v4 */
    {{0xc0820301}, 1, "s_load_dwordx4"}, /* s_load_dwordx4 s[4:7], s[2:3], 0x1 */
    {{0xc20084ff, 0x00012345}, 2, "s_buffer_load_dword"}, /* s_buffer_load_dword s1, s[4:7], 0x12345 */
    {{0xc8040002}, 1, "v_interp_p1_f32"}, /* v_interp_p1_f32 v1, v2, attr0.x */
    {{0xd8d80000, 0x01000002}, 2, "ds_read_b32"}, /* ds_read_b32 v1, v2 */
    {{0xe0301000, 0x01010102}, 2, "buffer_load_dword"}, /* buffer_load_dword v1, v2, s[4:7], s1 offen */
    {{0xe8081000, 0x01010102}, 2, "tbuffer_load_format_x"}, /* tbuffer_load_format_x v1, v2, s[4:7], dfmt:1, nfmt:0, s1 offen */
    {{0xf0800f00, 0x00820102}, 2, "image_sample"}, /* image_sample v[1:4], v[2:3], s[8:15], s[16:19] dmask:0xf */
    {{0xf800180f, 0x04030201}, 2, "exp"}, /* exp mrt0 v1, v2, v3, v4 done vm */
    {{0xdc300000, 0x01000002}, 2, "flat_load_dword"}, /* flat_load_dword v1, v[2:3] */
    {{0x5e020702}, 1, "v_cvt_pkrtz_f16_f32"}, /* v_cvt_pkrtz_f16_f32 v1, v2, v3 */
};
enum { CASE_COUNT = sizeof cases / sizeof cases[0] };

int main(void)
{
    /* Each instruction alone: its size, its name, and its literal where it has one. */
    for (size_t i = 0; i < CASE_COUNT; i++) {
        const decode_case *c = &cases[i];
        gcn_instruction ins;
        CHECK(gcn_decode(c->words, c->dwords, &ins) == 0);
        CHECK(ins.dwords == c->dwords);
        if (!ins.name || strcmp(ins.name, c->name) || ins.dwords != c->dwords)
            fprintf(stderr, "  case %zu: %s (%s, %u dwords), want %s\n", i, ins.name ? ins.name : "no name",
                    gcn_encoding_name(ins.encoding), ins.dwords, c->name);
        CHECK(ins.name && !strcmp(ins.name, c->name));
        const int literal_expected = c->dwords == 2 && ins.encoding < GCN_VOP3;
        CHECK(ins.has_literal == literal_expected && (!literal_expected || ins.literal == c->words[1]));
        /* A buffer that ends early is refused. */
        if (c->dwords > 1) CHECK(gcn_decode(c->words, c->dwords - 1, &ins) == -1);
    }

    /* All of them as one program: decoding steps from instruction to instruction. */
    uint32_t program[3 * CASE_COUNT];
    size_t length = 0;
    for (size_t i = 0; i < CASE_COUNT; i++)
        for (unsigned w = 0; w < cases[i].dwords; w++) program[length++] = cases[i].words[w];
    size_t at = 0, decoded = 0;
    gcn_instruction ins;
    while (at < length && decoded < CASE_COUNT && gcn_decode(program + at, length - at, &ins) == 0) {
        CHECK(ins.name && !strcmp(ins.name, cases[decoded].name));
        at += ins.dwords;
        decoded++;
    }
    CHECK(at == length && decoded == CASE_COUNT);

    /* Program length: up to s_endpgm, leaving out what follows it; a forward branch past an early
       s_endpgm keeps the walk going; a branch before the start is reported as such. */
    const uint32_t trailing[] = {0x7e020302, 0xbf810000, 0x12345678}; /* v_mov_b32, s_endpgm, metadata */
    CHECK(gcn_program_dwords(trailing, 3) == 2);
    const uint32_t early_exit[] = {0xbf820001, 0xbf810000, 0x7e020302, 0xbf810000}; /* s_branch +1 */
    CHECK(gcn_program_dwords(early_exit, 4) == 4);
    const uint32_t loop_before[] = {0xbf82fffd, 0xbf810000}; /* s_branch -3: before the code */
    CHECK(gcn_program_dwords(loop_before, 2) == GCN_BRANCH_BEFORE_START);
    const uint32_t no_end[] = {0x7e020302};
    CHECK(gcn_program_dwords(no_end, 1) == -1);

    /* Words that are no GFX7 encoding, and an empty buffer, are refused. */
    const uint32_t not_gfx7 = 0xfc000000u; /* bits 31:26 = 0x3f */
    CHECK(gcn_decode(&not_gfx7, 1, &ins) == -1);
    CHECK(gcn_decode(program, 0, &ins) == -1);
    CHECK(!strcmp(gcn_encoding_name(GCN_VOP3), "VOP3") && !strcmp(gcn_encoding_name(GCN_EXP), "EXP"));

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
