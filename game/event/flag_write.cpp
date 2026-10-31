// SPDX-License-Identifier: GPL-2.0-or-later
// Event system: update one bit in the event-flag store.
#include <cstdint>

#include "flag_store.h"

using event::FlagNode;
using event::FlagStore;

extern "C" void bb_event_flag_write(FlagStore *store, uint32_t flag_id, uint32_t enabled)
{
    const uint32_t group = flag_id / store->flags_per_group;
    FlagNode *entry = event::find_group(store->tree_header, group);
    if (!entry) return;
    uint8_t *bytes = event::group_bytes(store, entry);
    if (!bytes) return;

    const uint32_t within_group = flag_id - group * store->flags_per_group;
    const uint32_t byte_index = event::flag_byte(within_group);
    const uint8_t mask = event::flag_mask(within_group);
    if (enabled) {
        bytes[byte_index] |= mask;
    } else {
        bytes[byte_index] &= static_cast<uint8_t>(~mask);
    }
}
