// SPDX-License-Identifier: GPL-2.0-or-later
// Render: four small queries that swap-chain resource information calls as originals
// (swap_chain_resource_info.h).
// - render_resource_info_current_mode_02abaef0: takes the next value of a shared 32-bit counter
//   (an atomic increment that returns the old value) and keeps its low 24 bits.
// - render_resource_info_needs_initial_copy_02abb4b0: false for selector 0, 0x59 and the three
//   groups 0x13-0x16, 0x2c-0x2f and 0x42-0x45; true for every other code, negative ones included.
// - render_resource_info_alternate_copy_admitted_02abad70 / _storage_admitted_02abadd0: whether
//   the memory at an address is CPU-readable or GPU-readable, from the kernel's protection query;
//   false for a null address or when the query fails.
#include <cstdint>

#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x0247ebd0, atomic_fetch_increment, uint32_t(uint32_t *));
RT_GLOBAL(0x056ba180, resource_info_mode_counter, uint32_t);
RT_ORIGINAL(0x02fbf538, query_memory_protection, int32_t(uintptr_t address, void **start, void **end,
                                                         int32_t *protection));

constexpr uint32_t mode_bits = 0xffffff;
constexpr int32_t protection_cpu_read = 0x01;
constexpr int32_t protection_gpu_read = 0x10;

bool protection_allows(uintptr_t address, int32_t wanted)
{
    if (address == 0)
        return false;
    int32_t protection;
    if (query_memory_protection(address, nullptr, nullptr, &protection) != 0)
        return false;
    // The original tests only the low byte of the protection word.
    return (static_cast<uint8_t>(protection) & wanted) != 0;
}

}  // namespace

extern "C" uint32_t bb_render_resource_info_current_mode_02abaef0()
{
    return atomic_fetch_increment(resource_info_mode_counter.address()) & mode_bits;
}

extern "C" uint8_t bb_render_resource_info_needs_initial_copy_02abb4b0(int32_t selector)
{
    struct Range { int32_t first, last; };
    constexpr int32_t none = 0, plain = 0x59;
    constexpr Range without_copy[] = {{0x13, 0x16}, {0x2c, 0x2f}, {0x42, 0x45}};
    if (selector == none || selector == plain)
        return false;
    for (const Range &range : without_copy)
        if (selector >= range.first && selector <= range.last)
            return false;
    return true;
}

extern "C" uint8_t bb_render_resource_info_alternate_copy_admitted_02abad70(uintptr_t address)
{
    return protection_allows(address, protection_cpu_read);
}

extern "C" uint8_t bb_render_resource_info_storage_admitted_02abadd0(uintptr_t address)
{
    return protection_allows(address, protection_gpu_read);
}
