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
    /* the index from another lane scale: s_lshl_b32 of the lane... here the add takes v1, not the lane id */
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

    if (failures) fprintf(stderr, "test_linear_copy: %d failures\n", failures);
    else printf("test_linear_copy: ok\n");
    return failures != 0;
}
