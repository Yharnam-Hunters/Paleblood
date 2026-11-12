// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for presentation finish helpers: the finish item trailer, the
// buffer pool's containing-record lookup and the swap chain's present mode facade.
#include <stddef.h>
#include <stdint.h>

typedef struct PacketBuffer PacketBuffer;

extern uintptr_t emit_release_delegate(PacketBuffer *buffer, uint32_t selector, uint64_t *address,
                                       uint64_t data, uint32_t stack_selector);

typedef struct {
    uint8_t unknown_0x00[0x8];
    uint64_t count;
} FinishItemProcessor;

enum {
    ITEM_PROCESSED = 2,
    ITEM_PACKET_SELECTOR = 4,
};

// Mark the item's last word as processed and emit a release packet for that word.
uintptr_t render_process_present_finish_item(FinishItemProcessor *processor,
                                             PacketBuffer *commands, uint64_t *item)
{
    uint64_t *last = &item[processor->count - 1];
    *last = ITEM_PROCESSED;
    return emit_release_delegate(commands, ITEM_PACKET_SELECTOR, last, 0, 0);
}

typedef struct {
    uint8_t unknown_0x00[0x08];
    size_t slots;
    uint64_t block_count;
    uint64_t *base;
} PresentBufferPool;

// The start of the pool record (a run of `slots` words) that contains word `begin`; NULL stays
// NULL.
uint64_t *render_capture_present_buffer_pool_record(const PresentBufferPool *pool,
                                                    uint64_t *begin)
{
    ptrdiff_t index;
    if (!begin)
        return 0;
    index = begin - pool->base;
    return pool->base + (index - index % pool->slots);
}

typedef struct {
    uint8_t unknown_0x0000[0x33d0];
    void *source;
} SwapChain;

enum {
    LAST_PRESENT_MODE = 4,
    INVALID_PRESENT_MODE = 8,
};

extern int swap_chain_submit(SwapChain *swap_chain, void *source, unsigned mode);

// Set the present mode (0..4) through the swap chain's source; other modes are rejected.
int render_swap_chain_present_mode(SwapChain *swap_chain, unsigned mode)
{
    if (mode > LAST_PRESENT_MODE)
        return INVALID_PRESENT_MODE;
    return swap_chain_submit(swap_chain, swap_chain->source, mode);
}
