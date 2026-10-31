// SPDX-License-Identifier: GPL-2.0-or-later
// The event-flag store (SprjEventFlagMan): flags kept in groups, found through a search tree.
#pragma once

#include <cstddef>
#include <cstdint>

namespace event {

struct FlagNode {
    FlagNode *left;
    uint8_t unknown_0x08[8];
    FlagNode *right;
    uint8_t unknown_0x18;
    uint8_t sentinel;
    uint8_t unknown_0x1a[6];
    uint32_t group;
    uint32_t unknown_0x24;
    uint32_t storage_kind;
    uint32_t unknown_0x2c;
    uint64_t storage;
};

static_assert(offsetof(FlagNode, left) == 0x00);
static_assert(offsetof(FlagNode, right) == 0x10);
static_assert(offsetof(FlagNode, sentinel) == 0x19);
static_assert(offsetof(FlagNode, group) == 0x20);
static_assert(offsetof(FlagNode, storage_kind) == 0x28);
static_assert(offsetof(FlagNode, storage) == 0x30);

struct FlagTreeHeader {
    uint64_t unknown_0x00;
    FlagNode *root;
};

static_assert(offsetof(FlagTreeHeader, root) == 0x08);

struct FlagStore {
    uint8_t unknown_0x00[0x1c];
    uint32_t flags_per_group;
    uint32_t storage_stride;
    uint32_t unknown_0x24;
    uint8_t *storage_base;
    uint8_t unknown_0x30[8];
    FlagTreeHeader *tree_header;
};

static_assert(offsetof(FlagStore, flags_per_group) == 0x1c);
static_assert(offsetof(FlagStore, storage_stride) == 0x20);
static_assert(offsetof(FlagStore, storage_base) == 0x28);
static_assert(offsetof(FlagStore, tree_header) == 0x38);

constexpr uint32_t strided_storage_kind = 1;
constexpr uint32_t direct_storage_kind = 2;

// The tree node for exactly this group, or null.
inline FlagNode *find_group(FlagTreeHeader *header, uint32_t group)
{
    auto *sentinel = reinterpret_cast<FlagNode *>(header);
    FlagNode *candidate = sentinel;
    FlagNode *node = header->root;

    while (!node->sentinel) {
        if (node->group < group) {
            node = node->right;
        } else {
            candidate = node;
            node = node->left;
        }
    }

    if (candidate == sentinel || candidate->group != group) return nullptr;
    return candidate;
}

// The bytes holding a group's flags: its own block, or a slot of the store's strided storage;
// null for another storage kind or no storage.
inline uint8_t *group_bytes(const FlagStore *store, const FlagNode *entry)
{
    if (entry->storage_kind == direct_storage_kind) return reinterpret_cast<uint8_t *>(entry->storage);
    if (entry->storage_kind == strided_storage_kind) {
        const uint32_t offset = store->storage_stride * static_cast<uint32_t>(entry->storage);
        return reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(store->storage_base) + offset);
    }
    return nullptr;
}

// A flag's byte and bit within its group's bytes: eight flags a byte, the first in the top bit.
constexpr uint32_t flag_byte_shift = 3, flag_bit_mask = 7;
inline uint32_t flag_byte(uint32_t within_group) { return within_group >> flag_byte_shift; }
inline uint8_t flag_mask(uint32_t within_group)
{
    return static_cast<uint8_t>(1u << (flag_bit_mask - (within_group & flag_bit_mask)));
}

// Reads one flag, as the engine's inlined reads do: an id negative as a signed 32-bit value, a
// missing group and missing storage all read as clear.
inline bool read_flag(FlagStore *store, uint32_t flag_id)
{
    if (static_cast<int32_t>(flag_id) < 0) return false;
    const uint32_t group = flag_id / store->flags_per_group;
    FlagNode *entry = find_group(store->tree_header, group);
    if (!entry) return false;
    const uint8_t *bytes = group_bytes(store, entry);
    if (!bytes) return false;
    const uint32_t within_group = flag_id - group * store->flags_per_group;
    return (bytes[flag_byte(within_group)] & flag_mask(within_group)) != 0;
}

}  // namespace event
