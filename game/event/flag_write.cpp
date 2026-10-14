// SPDX-License-Identifier: GPL-2.0-or-later
// Event system: update one bit in the event-flag store.
#include <cstddef>
#include <cstdint>

namespace {

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

FlagNode *find_group(FlagTreeHeader *header, uint32_t group)
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

}  // namespace

extern "C" void bb_event_flag_write(FlagStore *store, uint32_t flag_id, uint32_t enabled)
{
    const uint32_t group = flag_id / store->flags_per_group;
    FlagNode *entry = find_group(store->tree_header, group);
    if (!entry) return;

    uint8_t *bytes = nullptr;
    if (entry->storage_kind == direct_storage_kind) {
        bytes = reinterpret_cast<uint8_t *>(entry->storage);
    } else if (entry->storage_kind == strided_storage_kind) {
        const uint32_t offset = store->storage_stride * static_cast<uint32_t>(entry->storage);
        bytes = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(store->storage_base) + offset);
    } else {
        return;
    }
    if (!bytes) return;

    const uint32_t within_group = flag_id - group * store->flags_per_group;
    const uint32_t byte_index = within_group >> 3;
    const uint8_t mask = static_cast<uint8_t>(1u << (7u - (within_group & 7u)));
    if (enabled) {
        bytes[byte_index] |= mask;
    } else {
        bytes[byte_index] &= static_cast<uint8_t>(~mask);
    }
}
