/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The constant engine (runtime/syslib/gnm.c): a draw buffer's WAIT_ON_CE_COUNTER runs its constant
   command buffer up to the increment the wait needs, so a DUMP_CONST_RAM into a slot lands after
   the work that read the slot's previous contents. The draw buffers here check the slot with
   WAIT_REG_MEM packets; with the display off, steps are performed at once, in order. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*submit_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *);

enum {
    WAIT_ON_CE_COUNTER = 0xc0008600u,   /* PM4 type 3, opcode 0x86, one payload dword */
    INCREMENT_DE_COUNTER = 0xc0008500u, /* opcode 0x85 */
    INCREMENT_CE_COUNTER = 0xc0008400u, /* opcode 0x84 */
    WAIT_ON_DE_COUNTER_DIFF = 0xc0008800u,
    WRITE_CONST_RAM_ONE = 0xc0018100u, /* opcode 0x81: offset, one dword */
    DUMP_CONST_RAM = 0xc0038300u,      /* opcode 0x83: offset, dword count, address low, high */
    LOAD_CONST_RAM = 0xc0038000u,      /* opcode 0x80: not done */
    WAIT_REG_MEM = 0xc0053c00u,        /* opcode 0x3c, six payload dwords */
    WAIT_EQUAL_IN_MEMORY = 0x13u,      /* memory space, function 3 (equal) */
    SLOT_OFFSET = 0x100,               /* in the constant RAM */
};

static submit_fn submit;
static volatile uint32_t slot, spare;

/* The draw buffer: for each expected value, wait for the CE, check the slot, count the draw. */
static uint32_t draw_words[64], draw_count;
static void draw_expecting(const uint32_t *values, uint32_t count)
{
    const uint64_t at = (uint64_t)(uintptr_t)&slot;
    draw_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t packet[] = {WAIT_ON_CE_COUNTER, 0, WAIT_REG_MEM, WAIT_EQUAL_IN_MEMORY, (uint32_t)at, (uint32_t)(at >> 32),
                                   values[i], 0xffffffffu, 10, INCREMENT_DE_COUNTER, 0};
        memcpy(draw_words + draw_count, packet, sizeof packet);
        draw_count += sizeof packet / sizeof packet[0];
    }
}

/* The constant buffer: for each value, write it into the constant RAM, dump it into the slot, and
   count; `extra` packets go after. */
static uint32_t constant_words[96], constant_count;
static void constants_writing(const uint32_t *values, uint32_t count)
{
    const uint64_t at = (uint64_t)(uintptr_t)&slot;
    constant_count = 0;
    const uint32_t limit[] = {WAIT_ON_DE_COUNTER_DIFF, 4};
    memcpy(constant_words, limit, sizeof limit);
    constant_count = 2;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t packet[] = {WRITE_CONST_RAM_ONE, SLOT_OFFSET, values[i], DUMP_CONST_RAM, SLOT_OFFSET, 1, (uint32_t)at,
                                   (uint32_t)(at >> 32), INCREMENT_CE_COUNTER, 1, 0xc0001000u, 0};
        memcpy(constant_words + constant_count, packet, sizeof packet);
        constant_count += sizeof packet / sizeof packet[0];
    }
}
static void append_constants(const uint32_t *packet, uint32_t count)
{
    memcpy(constant_words + constant_count, packet, count * sizeof packet[0]);
    constant_count += count;
}

static int32_t submit_both(int with_constants)
{
    void *draws[1] = {draw_words}, *constants[1] = {with_constants ? constant_words : NULL};
    uint32_t draw_bytes[1] = {draw_count * 4}, constant_bytes[1] = {with_constants ? constant_count * 4 : 0};
    return submit(1, draws, draw_bytes, constants, constant_bytes);
}
static void submit_with_constants(void) { submit_both(1); }
static void submit_without_constants(void) { submit_both(0); }

SYSLIB_TEST(gnm_constant_engine)
{
    submit = (submit_fn)fn("libSceGnmDriver", "sceGnmSubmitCommandBuffers");
    if (!submit) return;
    static const uint32_t values[3] = {0x11111111u, 0x22222222u, 0x33333333u};

    /* Three draws, one slot: each sees its own value (all dumps at once would leave the last). */
    slot = 0;
    draw_expecting(values, 3);
    constants_writing(values, 3);
    CHECK(submit_both(1) == 0 && slot == 0x33333333u);

    /* The CE's packets after its last increment still run, after the draw buffer. */
    const uint64_t spare_at = (uint64_t)(uintptr_t)&spare;
    spare = 0;
    draw_expecting(values, 1);
    constants_writing(values, 1);
    append_constants((const uint32_t[]){DUMP_CONST_RAM, SLOT_OFFSET, 1, (uint32_t)spare_at, (uint32_t)(spare_at >> 32)}, 5);
    CHECK(submit_both(1) == 0 && spare == 0x11111111u);

    /* A slot the CE dumps the wrong value into fails its wait (the check above means something). */
    slot = 0;
    static const uint32_t swapped[2] = {0x22222222u, 0x11111111u};
    draw_expecting(values, 2);
    constants_writing(swapped, 2);
    CHECK(died_in_fatal(submit_with_constants));

    /* A wait without a constant buffer, or more waits than the CE's increments, stop by name. */
    draw_expecting(values, 1);
    CHECK(died_in_fatal(submit_without_constants));
    draw_expecting(values, 2);
    constants_writing(values, 1);
    CHECK(died_in_fatal(submit_with_constants));

    /* A CE packet the stand-in does not know, and a write past the constant RAM, stop by name. */
    draw_expecting(values, 1);
    constants_writing(values, 1);
    append_constants((const uint32_t[]){LOAD_CONST_RAM, 0, 0, 0, 0}, 5);
    CHECK(died_in_fatal(submit_with_constants));
    constants_writing(values, 1);
    append_constants((const uint32_t[]){WRITE_CONST_RAM_ONE, 48 * 1024, 0}, 3);
    CHECK(died_in_fatal(submit_with_constants));
}
