// SPDX-License-Identifier: GPL-2.0-or-later
// The entry table queried by the timed-record collector. Only the lookup fields are known.
#pragma once

#include <cstddef>
#include <cstdint>

namespace engine {

using EntryHandle = uint64_t;
struct HandleEntry;

struct EntryManager {
    uint8_t unknown_0x0000[0x18];
    HandleEntry *const *entries;
    uint8_t unknown_0x0020[0x8c];
    uint32_t capacity;
    uint8_t unknown_0x00b0[0x58];
    uint8_t lookup_enabled;
};
static_assert(offsetof(EntryManager, entries) == 0x18);
static_assert(offsetof(EntryManager, capacity) == 0xac);
static_assert(offsetof(EntryManager, lookup_enabled) == 0x108);
static_assert(sizeof(EntryHandle) == sizeof(uintptr_t));

constexpr EntryHandle invalid_entry_handle = UINT64_MAX;
constexpr uintptr_t missing_entry_address = UINTPTR_MAX;

}  // namespace engine
