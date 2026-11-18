/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WRITE_DATA (runtime/syslib/gnm.c): the packet's dwords go to memory as a step, in order with the
   work around it (a WAIT_REG_MEM after it sees them); writes to registers or to one address stop.
   With the display off, steps are performed at once, in order. One dword written three times is a
   fill step (RT_GPU_STEP_FILLS: the renderer may have the GPU make it); others are not. A wait whose
   compare holds for any value (">= 0", "<= mask") is no step at all. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*submit_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *);

size_t rt_renderer_none_step_flags(uint32_t *flags, size_t capacity); /* runtime/renderer/renderer_none.c */
enum { STEP_FILLS = 4 }; /* RT_GPU_STEP_FILLS */

enum {
    WRITE_DATA_THREE = 0xc0053700u, /* PM4 type 3, opcode 0x37, control + address + three dwords */
    TO_MEMORY = 5u << 8,
    TO_REGISTER = 0u << 8,
    ONE_ADDRESS = 1u << 16,
    WAIT_REG_MEM = 0xc0053c00u,
    WAIT_EQUAL_IN_MEMORY = 0x13u,
    WAIT_AT_LEAST_IN_MEMORY = 0x15u,
    WAIT_AT_MOST_IN_MEMORY = 0x12u,
};

static submit_fn submit;
static volatile uint32_t target[4];

static void submit_dwords(uint32_t control, uint32_t first, uint32_t second, uint32_t third)
{
    const uint64_t at = (uint64_t)(uintptr_t)target;
    const uint64_t last = at + 2 * sizeof(uint32_t);
    uint32_t packets[] = {WRITE_DATA_THREE, control, (uint32_t)at, (uint32_t)(at >> 32), first, second, third,
                          WAIT_REG_MEM, WAIT_EQUAL_IN_MEMORY, (uint32_t)last, (uint32_t)(last >> 32), third, 0xffffffffu, 10};
    void *buffers[1] = {packets};
    uint32_t bytes[1] = {sizeof packets};
    submit(1, buffers, bytes, NULL, NULL);
}
static void submit_wait(uint32_t function, uint32_t reference, uint32_t mask)
{
    const uint64_t at = (uint64_t)(uintptr_t)target;
    uint32_t packets[] = {WAIT_REG_MEM, function, (uint32_t)at, (uint32_t)(at >> 32), reference, mask, 10};
    void *buffers[1] = {packets};
    uint32_t bytes[1] = {sizeof packets};
    submit(1, buffers, bytes, NULL, NULL);
}
static void submit_write(uint32_t control) { submit_dwords(control, 0xa1u, 0xb2u, 0xc3u); }
static void write_to_register(void) { submit_write(TO_REGISTER); }
static void write_to_one_address(void) { submit_write(TO_MEMORY | ONE_ADDRESS); }

SYSLIB_TEST(gnm_write_data)
{
    submit = (submit_fn)fn("libSceGnmDriver", "sceGnmSubmitCommandBuffers");
    if (!submit) return;
    memset((void *)target, 0, sizeof target);
    submit_write(TO_MEMORY); /* the wait after it would stop if the dwords had not landed */
    CHECK(target[0] == 0xa1u && target[1] == 0xb2u && target[2] == 0xc3u && target[3] == 0);
    uint32_t flags[2] = {0};
    CHECK(rt_renderer_none_step_flags(flags, 2) == 2 && !(flags[0] & STEP_FILLS));
    submit_dwords(TO_MEMORY, 0x77u, 0x77u, 0x77u);
    CHECK(target[0] == 0x77u && target[1] == 0x77u && target[2] == 0x77u && target[3] == 0);
    CHECK(rt_renderer_none_step_flags(flags, 2) == 2 && flags[0] == STEP_FILLS && !(flags[1] & STEP_FILLS));
    submit_dwords(TO_MEMORY, 0x77u, 0x77u, 0x78u); /* the last differs: not a fill */
    CHECK(target[2] == 0x78u && rt_renderer_none_step_flags(flags, 2) == 2 && !(flags[0] & STEP_FILLS));
    target[0] = 0x1234u;
    submit_wait(WAIT_AT_LEAST_IN_MEMORY, 0, 0xffffffffu); /* holds for any value: no step */
    CHECK(rt_renderer_none_step_flags(flags, 2) == 0);
    submit_wait(WAIT_AT_MOST_IN_MEMORY, 0xffu, 0xffu); /* any masked value is at most the mask */
    CHECK(rt_renderer_none_step_flags(flags, 2) == 0);
    submit_wait(WAIT_AT_LEAST_IN_MEMORY, 0x1234u, 0xffffffffu); /* holds now, but only for this value: a step */
    CHECK(rt_renderer_none_step_flags(flags, 2) == 1);
    memset((void *)target, 0, sizeof target);
    submit_write(2u << 8); /* through the L2 cache: the same memory */
    CHECK(target[2] == 0xc3u);
    CHECK(died_in_fatal(write_to_register) && died_in_fatal(write_to_one_address));
}
