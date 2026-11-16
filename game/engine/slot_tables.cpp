// SPDX-License-Identifier: GPL-2.0-or-later
// Sixteen families (fills_*_slot(s)_* in symbols/families.csv), each the copies of one function
// that fills a 15-entry table at its second argument: the first entries point at fixed bytes in
// the image (the same for every copy of one family), the rest are null, the first argument goes
// to +0x78, and the function returns 0. Which tables these are is not traced here.
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "runtime/guest.h"

namespace {

constexpr std::size_t table_entries = 15;

struct SlotTable {
    const void *entries[table_entries];
    void *owner;
};
static_assert(offsetof(SlotTable, owner) == 0x78);

// The fixed bytes the entries point at.
constexpr uint32_t bytes_05ac5cac = 0x05ac5cac;
constexpr uint32_t bytes_05ac5dfd = 0x05ac5dfd;
constexpr uint32_t bytes_05ac5e6b = 0x05ac5e6b;
constexpr uint32_t bytes_05ac5f5b = 0x05ac5f5b;
constexpr uint32_t bytes_05ac74a3 = 0x05ac74a3;
constexpr uint32_t bytes_05ac74a4 = 0x05ac74a4;
constexpr uint32_t bytes_05ac74a5 = 0x05ac74a5;
constexpr uint32_t bytes_05ac74a7 = 0x05ac74a7;
constexpr uint32_t bytes_05ac74a9 = 0x05ac74a9;
constexpr uint32_t bytes_05acd1bc = 0x05acd1bc;
constexpr uint32_t bytes_05acd45c = 0x05acd45c;
constexpr uint32_t bytes_05acee04 = 0x05acee04;

uint64_t fill_slots(void *owner, SlotTable *table, std::initializer_list<uint32_t> pointers)
{
    std::size_t entry = 0;
    for (uint32_t address : pointers)
        table->entries[entry++] = rt_guest(address);
    for (; entry < table_entries; ++entry)
        table->entries[entry] = nullptr;
    table->owner = owner;
    return 0;
}

}  // namespace

extern "C" uint64_t bb_engine_fills_3_slots_with_05ac5cac(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5cac, bytes_05ac5cac, bytes_05ac5cac});
}

extern "C" uint64_t bb_engine_fills_1_slot_with_05ac5e6b(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5e6b});
}

extern "C" uint64_t bb_engine_fills_1_slot_with_05ac5cac(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5cac});
}

extern "C" uint64_t bb_engine_fills_4_slots_with_05ac5cac(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5cac, bytes_05ac5cac, bytes_05ac5cac, bytes_05ac5cac});
}

extern "C" uint64_t bb_engine_fills_5_slots_with_05ac5cac(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5cac, bytes_05ac5cac, bytes_05ac5cac, bytes_05ac5cac, bytes_05ac5cac});
}

extern "C" uint64_t bb_engine_fills_1_slot_with_05acd45c(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05acd45c});
}

extern "C" uint64_t bb_engine_fills_1_slot_with_05acd1bc(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05acd1bc});
}

extern "C" uint64_t bb_engine_fills_2_slots_with_05acd45c(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05acd45c, bytes_05acd45c});
}

extern "C" uint64_t bb_engine_fills_2_slots_with_05ac5cac(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5cac, bytes_05ac5cac});
}

extern "C" uint64_t bb_engine_fills_1_slot_with_05acee04(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05acee04});
}

extern "C" uint64_t bb_engine_fills_1_slot_with_05ac5dfd(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5dfd});
}

extern "C" uint64_t bb_engine_fills_1_slot_with_05ac5f5b(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac5f5b});
}

extern "C" uint64_t bb_engine_fills_2_slots_05ac74a5_05ac74a3(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac74a5, bytes_05ac74a3});
}

extern "C" uint64_t bb_engine_fills_2_slots_05ac74a7_05ac5cac(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac74a7, bytes_05ac5cac});
}

extern "C" uint64_t bb_engine_fills_2_slots_05ac74a4_05ac74a3(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac74a4, bytes_05ac74a3});
}

extern "C" uint64_t bb_engine_fills_2_slots_05ac74a9_05ac5cac(void *owner, void *table)
{
    return fill_slots(owner, static_cast<SlotTable *>(table), {bytes_05ac74a9, bytes_05ac5cac});
}
