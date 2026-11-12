/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Occlusion events (runtime/syslib/gnm.c): a query block zeroed by DMA_DATA, then PIXEL_PIPE_STAT_CONTROL
   (writes nothing) and PIXEL_PIPE_STAT_DUMP into its begin and end halves, as the game's queries do. Each
   dump stores a count with bit 63 (valid) in all 8 depth blocks' pairs, later dumps larger, so the query
   reads visible; another event with an address stops by name. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*submit_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *);

enum {
    EVENT_WRITE = 0xc0024600u, /* PM4 type 3, opcode 0x46, four dwords: control, address low, address high */
    ZPASS_DONE_INDEX = 1u << 8, PIXEL_PIPE_STAT_CONTROL = 0x38, PIXEL_PIPE_STAT_DUMP = 0x39, ZPASS_DONE = 0x15,
    DMA_DATA = 0xc0055000u, FROM_DATA = 2u << 29, BLOCK_BYTES = 128, DEPTH_BLOCKS = 8,
};
#define VALID (UINT64_C(1) << 63)

static submit_fn submit;
static _Alignas(16) uint64_t block[BLOCK_BYTES / 8];

static void submit_packets(uint32_t *packets, uint32_t bytes)
{
    void *buffers[1] = {packets};
    submit(1, buffers, &bytes, NULL, NULL);
}

static void zpass_done_with_address(void)
{
    const uint64_t at = (uint64_t)(uintptr_t)block;
    uint32_t packets[] = {EVENT_WRITE, ZPASS_DONE | ZPASS_DONE_INDEX, (uint32_t)at, (uint32_t)(at >> 32)};
    submit_packets(packets, sizeof packets);
}

SYSLIB_TEST(gnm_occlusion)
{
    submit = (submit_fn)fn("libSceGnmDriver", "sceGnmSubmitCommandBuffers");
    if (!submit) return;
    memset(block, 0xee, sizeof block);
    const uint64_t at = (uint64_t)(uintptr_t)block;
    uint32_t packets[] = {
        DMA_DATA, FROM_DATA, 0, 0, (uint32_t)at, (uint32_t)(at >> 32), BLOCK_BYTES, /* zero the block */
        EVENT_WRITE, PIXEL_PIPE_STAT_CONTROL | ZPASS_DONE_INDEX, 0x7fc00, 0,
        EVENT_WRITE, PIXEL_PIPE_STAT_DUMP | ZPASS_DONE_INDEX, (uint32_t)at, (uint32_t)(at >> 32),                 /* begin */
        EVENT_WRITE, PIXEL_PIPE_STAT_CONTROL | ZPASS_DONE_INDEX, 0x7fc00, 0,
        EVENT_WRITE, PIXEL_PIPE_STAT_DUMP | ZPASS_DONE_INDEX, (uint32_t)(at + 8), (uint32_t)((at + 8) >> 32),     /* end */
    };
    submit_packets(packets, sizeof packets);
    uint64_t passed = 0;
    for (unsigned db = 0; db < DEPTH_BLOCKS; db++) {
        const uint64_t begin = block[2 * db], end = block[2 * db + 1];
        CHECK((begin & VALID) && (end & VALID) && end > begin);
        passed += (end & ~VALID) - (begin & ~VALID);
    }
    CHECK(passed > 0);
    CHECK(died_in_fatal(zpass_done_with_address));
}
