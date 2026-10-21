// SPDX-License-Identifier: GPL-2.0-or-later
// Resolve the handle's low-word table index. The timed-record collector repeats this lookup
// before querying an entry; this helper does not validate the handle's upper word.
#include "entry_manager.h"

#include <cstring>

extern "C" engine::HandleEntry *bb_engine_entry_manager_resolve(
    const engine::EntryManager *manager, const engine::EntryHandle *handle)
{
    // Disabled lookup must not read the handle, which may be absent on this path.
    if (manager->lookup_enabled == 0) return nullptr;

    // The original scalar loads also accept unaligned handle and table storage.
    engine::EntryHandle value;
    std::memcpy(&value, handle, sizeof(value));
    if (value == engine::invalid_entry_handle) return nullptr;
    const uint32_t index = static_cast<uint32_t>(value);
    if (index >= manager->capacity) return nullptr;

    engine::HandleEntry *entry;
    const auto *entries = reinterpret_cast<const uint8_t *>(manager->entries);
    std::memcpy(&entry, entries + index * sizeof(entry), sizeof(entry));
    if (reinterpret_cast<uintptr_t>(entry) == engine::missing_entry_address) return nullptr;
    return entry;
}
