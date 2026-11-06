// SPDX-License-Identifier: GPL-2.0-or-later
// Collect nonempty DWORD-aligned command regions without changing their order.
#pragma once

#include <initializer_list>

#include "command_buffer_snapshot.h"

namespace render {
struct CommandRegionList {
    static constexpr std::size_t maximum_regions = 2;
    uint64_t addresses[maximum_regions];
    uint32_t sizes[maximum_regions];
    uint32_t count = 0;
};

inline uint32_t command_region_size(const CommandBufferSnapshot &region)
{
    constexpr uint32_t command_size_mask = ~uint32_t(sizeof(uint32_t) - 1);
    // Only the low DWORDs participate in the original subtraction.
    return (static_cast<uint32_t>(region.commands.cursor) -
            static_cast<uint32_t>(region.commands.begin)) & command_size_mask;
}

inline CommandRegionList collect_command_regions(const CommandBufferSnapshot &first,
                                                 const CommandBufferSnapshot &second)
{
    CommandRegionList regions;
    for (const CommandBufferSnapshot *region : {&first, &second}) {
        const uint32_t bytes = command_region_size(*region);
        if (bytes == 0) continue;
        regions.addresses[regions.count] = region->commands.begin;
        regions.sizes[regions.count++] = bytes;
    }
    return regions;
}
}  // namespace render
