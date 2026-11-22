// SPDX-License-Identifier: GPL-2.0-or-later
// Two engine families (symbols/families.csv, docs/FAMILIES.md), each the copies of one function
// that fills a caller's 0x80-byte block for an owner and returns 0:
// - fills_owner_block_with_two_addresses: the address 0x05acd45c in the block's first two
//   pointers, 0x05acd1bc in the third;
// - fills_owner_block_with_one_address: 0x05acd45c in all three.
// Both then clear the 12 qwords from +0x18 to +0x70 and keep the owner at +0x78. Nothing else in
// the block is written and nothing is read from it.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

constexpr std::size_t cleared_qwords = 12;  // +0x18 up to +0x78
struct OwnerBlock {
    const void *first;
    const void *second;
    const void *third;
    uint64_t cleared[cleared_qwords];
    void *owner;
};
static_assert(offsetof(OwnerBlock, cleared) == 0x18 && offsetof(OwnerBlock, owner) == 0x78);

// The addresses the members store (only their addresses are used).
RT_GLOBAL(0x05acd45c, block_address_a, const uint8_t);
RT_GLOBAL(0x05acd1bc, block_address_b, const uint8_t);

constexpr int32_t block_filled = 0;

int32_t fill(void *owner, OwnerBlock *block, const void *third)
{
    block->first = block_address_a.address();
    block->second = block_address_a.address();
    block->third = third;
    for (uint64_t &qword : block->cleared)
        qword = 0;
    block->owner = owner;
    return block_filled;
}

}  // namespace

extern "C" int32_t bb_engine_fills_owner_block_with_two_addresses(void *owner, OwnerBlock *block)
{
    return fill(owner, block, block_address_b.address());
}

extern "C" int32_t bb_engine_fills_owner_block_with_one_address(void *owner, OwnerBlock *block)
{
    return fill(owner, block, block_address_a.address());
}
