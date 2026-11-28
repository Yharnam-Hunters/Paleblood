/* SPDX-License-Identifier: GPL-2.0-or-later */
/* gcn_linear_copy_shape (runtime/linear_copy.h): which programs count as a linear copy. */

#include "runtime/linear_copy.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                     \
    do {                                                                             \
        if (!(c)) fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c), failures++; \
    } while (0)

/* s_lshl_b32 s2, s16, 6; v_add_i32 v1, s2, v0; buffer_load_dword v2, v1, s[4:7] idxen;
   v_cmp_gt_u32 vcc, s14, v1; s_and_saveexec_b64 s[0:1], vcc; s_waitcnt vmcnt(0);
   buffer_store_dword v2, v1, s[12:15] idxen; s_endpgm. */
enum { LSHL, ADD, LOAD, LOAD_1, COMPARE, SAVEEXEC, WAIT, STORE, STORE_1, END, WORDS };
static const uint32_t copy_dwords[WORDS] = {0x8f028610, 0x4a020002, 0xe0302000, 0x80010201, 0x7d88020e,
                                            0xbe80246a, 0xbf8c0f70, 0xe0702000, 0x80030201, 0xbf810000};

static int shape_of(const uint32_t *code, size_t dwords, gcn_linear_copy *shape)
{
    memset(shape, 0xa5, sizeof *shape);
    return gcn_linear_copy_shape(code, dwords, shape);
}

/* The copy with `count` dwords inserted before copy_dwords[before]: whether it still counts as one. */
static int with_inserted(size_t before, const uint32_t *inserted, size_t count)
{
    uint32_t code[WORDS + 4];
    gcn_linear_copy shape;
    memcpy(code, copy_dwords, before * 4);
    memcpy(code + before, inserted, count * 4);
    memcpy(code + before + count, copy_dwords + before, (WORDS - before) * 4);
    return shape_of(code, WORDS + count, &shape);
}

/* Insert s_mov_b32 sdst, 0 before one word of the guarded copy. */
static void check_scalar_move(unsigned sdst, unsigned before, int expected)
{
    uint32_t code[WORDS + 1];
    memcpy(code, copy_dwords, before * sizeof *code);
    code[before] = 0xbe800380u | sdst << 16;
    memcpy(code + before + 1, copy_dwords + before, (WORDS - before) * sizeof *code);
    gcn_linear_copy shape;
    const int matched = shape_of(code, WORDS + 1, &shape);
    if (matched != expected) fprintf(stderr, "scalar move to %u before word %u: matched %d, expected %d\n", sdst, before, matched, expected);
    CHECK(matched == expected);
}

int main(void)
{
    gcn_linear_copy shape;
    uint32_t code[WORDS + 2];

    CHECK(shape_of(copy_dwords, WORDS, &shape) == 1);
    CHECK(shape.source_sgpr == 4 && shape.destination_sgpr == 12 && shape.workgroup_sgpr == 16 && shape.group_elements == 64 && shape.format == 0);

    /* the format forms of the load and store (buffer_load_format_x, buffer_store_format_x) */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[LOAD] = 0xe0002000, code[STORE] = 0xe0102000;
    CHECK(shape_of(code, WORDS, &shape) == 1 && shape.format == 1);
    code[STORE] = 0xe0702000; /* a format load with a dword store: not one */
    CHECK(shape_of(code, WORDS, &shape) == 0);

    /* no guard: the destination's range check bounds the copy alone */
    const uint32_t unguarded[] = {copy_dwords[LSHL], copy_dwords[ADD], copy_dwords[LOAD], copy_dwords[LOAD_1], copy_dwords[WAIT],
                                  copy_dwords[STORE], copy_dwords[STORE_1], copy_dwords[END]};
    CHECK(shape_of(unguarded, sizeof unguarded / 4, &shape) == 1 && shape.destination_sgpr == 12);

    /* a guard on another SGPR than the destination's record count */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[COMPARE] = 0x7d88020d;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* a compare not taken into exec */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[SAVEEXEC] = 0xbf800000; /* s_nop */
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* the store of another register (v3) */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[STORE_1] = 0x80030301;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* a load with an immediate offset */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[LOAD] |= 4;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* the add of the scaled workgroup index to v1 rather than to the lane id v0 */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[ADD] = 0x4a020202;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* an s_mov_b32 into the source descriptor (s5), and one into an unused SGPR (vcc_hi) */
    const uint32_t clobber[] = {0xbe850380, copy_dwords[LSHL], copy_dwords[ADD], copy_dwords[LOAD], copy_dwords[LOAD_1], copy_dwords[WAIT],
                                copy_dwords[STORE], copy_dwords[STORE_1], copy_dwords[END]};
    CHECK(shape_of(clobber, sizeof clobber / 4, &shape) == 0);
    const uint32_t harmless[] = {0xbeeb03ff, 7, copy_dwords[LSHL], copy_dwords[ADD], copy_dwords[LOAD], copy_dwords[LOAD_1], copy_dwords[WAIT],
                                 copy_dwords[STORE], copy_dwords[STORE_1], copy_dwords[END]};
    CHECK(shape_of(harmless, sizeof harmless / 4, &shape) == 1);
    /* EXEC's low and high halves independently select which lanes load and store. */
    enum { VCC_LO = 106, VCC_HI = 107, EXEC_LO = 126, EXEC_HI = 127 };
    check_scalar_move(EXEC_LO, LOAD, 0);
    check_scalar_move(EXEC_HI, LOAD, 0);
    const uint32_t exec_cleared[] = {copy_dwords[LSHL], copy_dwords[ADD], 0xbefe0380, 0xbeff0380, copy_dwords[LOAD], copy_dwords[LOAD_1],
                                     copy_dwords[COMPARE], copy_dwords[SAVEEXEC], copy_dwords[WAIT], copy_dwords[STORE], copy_dwords[STORE_1],
                                     copy_dwords[END]};
    CHECK(shape_of(exec_cleared, sizeof exec_cleared / sizeof *exec_cleared, &shape) == 0);
    /* Either VCC half written after the compare changes the mask consumed by saveexec. */
    check_scalar_move(VCC_LO, SAVEEXEC, 0);
    check_scalar_move(VCC_HI, SAVEEXEC, 0);
    /* Earlier VCC writes are overwritten by the compare; unused ordinary SGPR writes stay valid. */
    check_scalar_move(VCC_LO, LSHL, 1);
    check_scalar_move(VCC_HI, COMPARE, 1);
    check_scalar_move(8, COMPARE, 1);
    /* Flat scratch, trap registers, M0 and reserved destinations have no proven harmless role. */
    check_scalar_move(104, LOAD, 0);
    check_scalar_move(108, LOAD, 0);
    check_scalar_move(124, LOAD, 0);
    check_scalar_move(125, LOAD, 0);
    /* the scaled index's SGPR (s2) written after the add reads it (the exec save), and before it */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[SAVEEXEC] = 0xbe82246a;
    CHECK(shape_of(code, WORDS, &shape) == 1);
    const uint32_t early[] = {copy_dwords[LSHL], 0xbe820380, copy_dwords[ADD], copy_dwords[LOAD], copy_dwords[LOAD_1], copy_dwords[WAIT],
                              copy_dwords[STORE], copy_dwords[STORE_1], copy_dwords[END]};
    CHECK(shape_of(early, sizeof early / 4, &shape) == 0);
    /* anything else, a v_mov_b32 v2, 0 between the load and the store */
    const uint32_t moved[] = {copy_dwords[LSHL], copy_dwords[ADD], copy_dwords[LOAD], copy_dwords[LOAD_1], 0x7e040280,
                              copy_dwords[STORE], copy_dwords[STORE_1], copy_dwords[END]};
    CHECK(shape_of(moved, sizeof moved / 4, &shape) == 0);

    /* a format copy that sets VCC's high half first: s_mov_b32 vcc_hi, 7; s_lshl_b32 s0, s12, 6;
       v_add_i32 v0, s0, v0; buffer_load_format_x v1, v0, s[4:7] idxen; v_cmp_gt_u32 vcc, s10, v0;
       s_and_saveexec_b64 s[0:1], vcc; s_waitcnt vmcnt(0); buffer_store_format_x v1, v0, s[8:11] idxen;
       s_endpgm */
    static const uint32_t vcc_early_copy[] = {0xbeeb03ff, 7, 0x8f00860c, 0x4a000000, 0xe0002000, 0x80010100,
                                              0x7d88000a, 0xbe80246a, 0xbf8c0f70, 0xe0102000, 0x80020100, 0xbf810000};
    CHECK(shape_of(vcc_early_copy, sizeof vcc_early_copy / 4, &shape) == 1);
    CHECK(shape.source_sgpr == 4 && shape.destination_sgpr == 8 && shape.workgroup_sgpr == 12);
    CHECK(shape.group_elements == 64 && shape.format == 1);

    /* special registers other than VCC stay live before the shift and the add too (exec_lo, exec_hi,
       m0, ttmp0, ttmp11, tma_lo and flat_scratch_lo), and VCC after the exec save has read it */
    static const unsigned live_registers[] = {EXEC_LO, EXEC_HI, 124, 112, 123, 110, 104};
    for (size_t i = 0; i < sizeof live_registers / sizeof *live_registers; i++) {
        check_scalar_move(live_registers[i], LSHL, 0);
        check_scalar_move(live_registers[i], ADD, 0);
    }
    check_scalar_move(VCC_LO, WAIT, 0);
    check_scalar_move(VCC_HI, WAIT, 0);
    /* the exec save into EXEC itself, into an odd pair (s[1:2]), or another exec save (s_or_saveexec_b64,
       s_andn2_saveexec_b64) */
    static const uint32_t other_saves[] = {0xbefe246a, 0xbe81246a, 0xbe80256a, 0xbe80276a};
    for (size_t i = 0; i < sizeof other_saves / 4; i++) {
        memcpy(code, copy_dwords, sizeof copy_dwords);
        code[SAVEEXEC] = other_saves[i];
        CHECK(shape_of(code, WORDS, &shape) == 0);
    }
    /* the compare as v_cmpx_gt_u32 (which writes EXEC itself) */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[COMPARE] = 0x7da8020e;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* the compare and the add in their VOP3 forms (v_cmp_gt_u32 vcc, s14, v1; v_add_i32 v1, vcc, s2, v0) */
    const uint32_t compare_vop3[] = {0xd188006a, 0x0002020e}, add_vop3[] = {0xd24a6a01, 0x00020002};
    uint32_t vop3[WORDS + 1];
    memcpy(vop3, copy_dwords, COMPARE * 4);
    memcpy(vop3 + COMPARE, compare_vop3, sizeof compare_vop3);
    memcpy(vop3 + COMPARE + 2, copy_dwords + COMPARE + 1, (WORDS - COMPARE - 1) * 4);
    CHECK(shape_of(vop3, WORDS + 1, &shape) == 0);
    memcpy(vop3, copy_dwords, ADD * 4);
    memcpy(vop3 + ADD, add_vop3, sizeof add_vop3);
    memcpy(vop3 + ADD + 2, copy_dwords + ADD + 1, (WORDS - ADD - 1) * 4);
    CHECK(shape_of(vop3, WORDS + 1, &shape) == 0);
    /* an s_endpgm before the store, a second load before the wait and a second store after the first */
    const uint32_t end = copy_dwords[END];
    CHECK(with_inserted(STORE, &end, 1) == 0);
    CHECK(with_inserted(WAIT, copy_dwords + LOAD, 2) == 0);
    CHECK(with_inserted(END, copy_dwords + STORE, 2) == 0);
    /* control flow: an s_branch over nothing, an s_cbranch_execz and an s_setpc_b64 before the store */
    const uint32_t branch = 0xbf820000, execz = 0xbf880000, setpc = 0xbe802000;
    CHECK(with_inserted(STORE, &branch, 1) == 0);
    CHECK(with_inserted(STORE, &execz, 1) == 0);
    CHECK(with_inserted(STORE, &setpc, 1) == 0);
    /* the store not waiting for the load: no s_waitcnt, or one that leaves a load outstanding (vmcnt 1) */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[WAIT] = 0xbf800000;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    code[WAIT] = 0xbf8c0f71;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* a wait before the load only */
    const uint32_t early_wait[] = {copy_dwords[LSHL],    copy_dwords[ADD],    copy_dwords[WAIT],
                                   copy_dwords[LOAD],    copy_dwords[LOAD_1], copy_dwords[STORE],
                                   copy_dwords[STORE_1], copy_dwords[END]};
    CHECK(shape_of(early_wait, sizeof early_wait / 4, &shape) == 0);
    /* a reserved bit set in the load's first word (bit 17 or 25), the store's first (bit 25) or the
       store's second (bit 21) */
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[LOAD] |= 1u << 17;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[LOAD] |= 1u << 25;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[STORE] |= 1u << 25;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    memcpy(code, copy_dwords, sizeof copy_dwords);
    code[STORE_1] |= 1u << 21;
    CHECK(shape_of(code, WORDS, &shape) == 0);
    /* an unguarded store through a descriptor in the trap handler registers (TBA and TMA, SRSRC 27) */
    memcpy(code, unguarded, sizeof unguarded);
    code[6] = /* the unguarded store's second dword */ (copy_dwords[STORE_1] & ~(0x1fu << 16)) | (27u << 16);
    CHECK(shape_of(code, sizeof unguarded / 4, &shape) == 0);
    /* an unguarded load or store through the first descriptor past the ordinary SGPRs (SRSRC 26,
       s[104:107], which runs into FLAT_SCRATCH and VCC) */
    memcpy(code, unguarded, sizeof unguarded);
    code[3] = /* the unguarded load's second dword */ (copy_dwords[LOAD_1] & ~(0x1fu << 16)) | (26u << 16);
    CHECK(shape_of(code, sizeof unguarded / 4, &shape) == 0);
    memcpy(code, unguarded, sizeof unguarded);
    code[6] = (copy_dwords[STORE_1] & ~(0x1fu << 16)) | (26u << 16);
    CHECK(shape_of(code, sizeof unguarded / 4, &shape) == 0);

    if (failures) fprintf(stderr, "test_linear_copy: %d failures\n", failures);
    else printf("test_linear_copy: ok\n");
    return failures != 0;
}
