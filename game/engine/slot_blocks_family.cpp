// SPDX-License-Identifier: GPL-2.0-or-later
// The family releases_slot_blocks_at_0x38 (symbols/families.csv, docs/FAMILIES.md): nine copies
// of one function over a table of `count` slots kept as three parallel arrays: 32-bit ids
// (+0x28), a second id array (+0x30) and blocks (+0x38), each block freed through the function
// the global 0x056c45d0 points at, at 8 bytes before the pointer the slot holds.
// - With `keep` set, every slot from the last to the first gives up its block, and the second id
//   takes the slot's id (read before the free, stored after it); the count stays.
// - Otherwise, every slot of the first `count` (the count read on entry) gives up its block and
//   takes the block and id `count` slots further on while that slot is below the count (read
//   again after each free), or an empty block; then the count drops by the entry count and the
//   byte at +0x8 is set.
// The blocks array is read again after each free; the count on entry bounds both loops.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct SlotTable {
    uint8_t unknown_0x00[0x8];
    uint8_t released;
    uint8_t unknown_0x09[0x13];
    int32_t count;
    uint8_t unknown_0x20[0x8];
    uint32_t *ids;
    uint32_t *second_ids;
    uint8_t **blocks;
};
static_assert(offsetof(SlotTable, released) == 0x8 && offsetof(SlotTable, count) == 0x1c);
static_assert(offsetof(SlotTable, ids) == 0x28 && offsetof(SlotTable, second_ids) == 0x30);
static_assert(offsetof(SlotTable, blocks) == 0x38);

using FreeBlock = void(void *block);
RT_GLOBAL(0x056c45d0, free_block, FreeBlock *const);

// A slot holds its block 8 bytes past the start the free takes.
constexpr std::ptrdiff_t block_header_size = 0x8;

void release_block(uint8_t *held)
{
    free_block.get()(held - block_header_size);
}

}  // namespace

extern "C" void bb_engine_releases_slot_blocks_at_0x38(SlotTable *table, bool keep)
{
    const int32_t count = table->count;
    if (keep) {
        for (int64_t slot = int64_t{count} - 1; slot >= 0; --slot) {
            const uint32_t id = table->ids[slot];
            if (uint8_t *held = table->blocks[slot])
                release_block(held);
            table->blocks[slot] = nullptr;
            table->ids[slot] = id;
            table->second_ids[slot] = id;
        }
        return;
    }
    int32_t remaining = count;
    if (count > 0) {
        for (uint32_t slot = 0; slot != static_cast<uint32_t>(count); ++slot) {
            if (uint8_t *held = table->blocks[slot])
                release_block(held);
            uint8_t **blocks = table->blocks;
            const int64_t from = int64_t{count} + slot;
            if (static_cast<int32_t>(count + slot) < table->count) {
                blocks[slot] = blocks[from];
                table->ids[slot] = table->ids[from];
            } else {
                blocks[slot] = nullptr;
            }
        }
        remaining = table->count;
    }
    table->count = remaining - count;
    table->released = 1;
}
