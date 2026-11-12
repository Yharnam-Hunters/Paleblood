/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SET_BASE and DISPATCH_INDIRECT (runtime/syslib/gnm.c): the indirect arguments' base (base index 1)
   and a dispatch reading its workgroup counts there pass, as the other draw and dispatch packets do,
   and the end-of-work write after them lands; a SET_BASE of another base stops. */
#include "../syslib_test.h"

#include <stdint.h>

typedef int32_t (*submit_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *);

enum {
    SET_BASE = 0xc0021100u,          /* PM4 type 3, opcode 0x11, base index + address low + high */
    DISPATCH_INDIRECT = 0xc0011602u, /* opcode 0x16, data offset + initiator, compute shader type */
    EVENT_WRITE_EOP = 0xc0044700u,
    INDIRECT_ARGUMENTS_BASE = 1,
    DISPLAY_LIST_BASE = 0,
    DATA_OFFSET = 0x18,
    COMPUTE_SHADER_EN = 1,
    EOP_32_BITS = 0x20000000u,
    DONE = 0x1d,
};

static submit_fn submit;
static volatile uint32_t label;
static uint32_t arguments[16];

static void submit_indirect(uint32_t base_index)
{
    const uint64_t base = (uint64_t)(uintptr_t)arguments, at = (uint64_t)(uintptr_t)&label;
    uint32_t packets[] = {SET_BASE, base_index, (uint32_t)base & ~7u, (uint32_t)(base >> 32) & 0xffu,
                          DISPATCH_INDIRECT, DATA_OFFSET, COMPUTE_SHADER_EN,
                          EVENT_WRITE_EOP, 0x528, (uint32_t)at, (uint32_t)(at >> 32) | EOP_32_BITS, DONE, 0};
    void *buffers[1] = {packets};
    uint32_t bytes[1] = {sizeof packets};
    submit(1, buffers, bytes, NULL, NULL);
}
static void set_display_list_base(void) { submit_indirect(DISPLAY_LIST_BASE); }

SYSLIB_TEST(gnm_indirect)
{
    submit = (submit_fn)fn("libSceGnmDriver", "sceGnmSubmitCommandBuffers");
    if (!submit) return;
    label = 0;
    submit_indirect(INDIRECT_ARGUMENTS_BASE);
    CHECK(label == DONE);
    CHECK(died_in_fatal(set_display_list_base));
}
