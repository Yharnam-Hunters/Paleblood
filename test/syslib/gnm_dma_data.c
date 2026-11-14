/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DMA_DATA copies and fills (runtime/syslib/gnm.c): between memory, the GDS and the packet's own data,
   as a step in order with the work (with the display off, at once). A counter goes into the GDS and
   back out to memory; a fill writes its value into every dword; register addresses, a GDS range
   past 64 KB and destination select 2 stop by name. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*submit_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *);

enum {
    DMA_DATA = 0xc0055000u, /* PM4 type 3, opcode 0x50: control, source low/high, destination low/high, command */
    FROM_MEMORY = 0u << 29, FROM_GDS = 1u << 29, FROM_DATA = 2u << 29, FROM_MEMORY_L2 = 3u << 29,
    TO_MEMORY = 0u << 20, TO_GDS = 1u << 20, TO_RESERVED = 2u << 20, TO_MEMORY_L2 = 3u << 20,
    DESTINATION_IS_REGISTER = 1u << 27,
    COUNTER_OFFSET = 0x20, GDS_BYTES = 64 << 10, COUNTER = 0x1234abcd, FILL = 0x5eed5eed,
};

static submit_fn submit;
static uint32_t source[4], destination[4];

static void submit_dma(uint32_t selects, uint64_t from, uint64_t to, uint32_t command)
{
    uint32_t packets[] = {DMA_DATA, selects, (uint32_t)from, (uint32_t)(from >> 32), (uint32_t)to, (uint32_t)(to >> 32), command};
    void *buffers[1] = {packets};
    uint32_t bytes[1] = {sizeof packets};
    submit(1, buffers, bytes, NULL, NULL);
}
static uint64_t address(const void *p) { return (uint64_t)(uintptr_t)p; }

static void to_register(void) { submit_dma(FROM_MEMORY | TO_MEMORY, address(source), address(destination), DESTINATION_IS_REGISTER | 4); }
static void past_the_gds(void) { submit_dma(FROM_MEMORY | TO_GDS, address(source), GDS_BYTES - 4, 8); }
static void to_select_two(void) { submit_dma(FROM_MEMORY | TO_RESERVED, address(source), address(destination), 4); }

SYSLIB_TEST(gnm_dma_data)
{
    submit = (submit_fn)fn("libSceGnmDriver", "sceGnmSubmitCommandBuffers");
    if (!submit) return;
    /* a counter from memory into the GDS, then out of it into other memory */
    memset(destination, 0, sizeof destination);
    source[0] = COUNTER;
    submit_dma(FROM_MEMORY | TO_GDS, address(source), COUNTER_OFFSET, 4);
    submit_dma(FROM_GDS | TO_MEMORY, COUNTER_OFFSET, address(destination), 4);
    CHECK(destination[0] == COUNTER && destination[1] == 0);
    /* the packet's data into every dword of a GDS range, read back through L2 */
    submit_dma(FROM_DATA | TO_GDS, FILL, COUNTER_OFFSET, 8);
    submit_dma(FROM_GDS | TO_MEMORY_L2, COUNTER_OFFSET, address(destination), 12);
    CHECK(destination[0] == FILL && destination[1] == FILL && destination[2] == 0);
    /* memory to memory, and a fill of memory */
    source[1] = 7, source[2] = 9;
    submit_dma(FROM_MEMORY_L2 | TO_MEMORY, address(source), address(destination), 12);
    CHECK(destination[0] == COUNTER && destination[1] == 7 && destination[2] == 9);
    submit_dma(FROM_DATA | TO_MEMORY, FILL, address(destination) + 4, 8);
    CHECK(destination[0] == COUNTER && destination[1] == FILL && destination[2] == FILL && destination[3] == 0);
    CHECK(died_in_fatal(to_register) && died_in_fatal(past_the_gds) && died_in_fatal(to_select_two));
}
