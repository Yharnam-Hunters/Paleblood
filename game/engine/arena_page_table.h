// SPDX-License-Identifier: GPL-2.0-or-later
// The family allocates_page_into_table_from_0x02795780 (symbols/families.csv): copies of one
// Scaleform function that differ only in the page size K. When the index is not below the
// table's capacity, the slots grow from the table's arena (0x02795780): doubled with the counted
// slots copied over (the old slots stay in the arena), or 16 new slots when there are none. Then a
// page of K bytes from the arena goes into the index's slot and the count goes up. An index past
// twice the capacity is stored past the slots, as in the game.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace engine::arena_page_table {

RT_ORIGINAL(0x02795780, arena_allocate, void *(void *arena, uint64_t bytes));
// The game's memcpy.
RT_ORIGINAL(0x02fbe578, copy_bytes, void *(void *destination, const void *source, uint64_t bytes));

struct PageTable {
    void *arena;
    uint8_t unknown_0x8[0x8];
    uint64_t count;
    uint64_t capacity;
    void **pages;
};
static_assert(offsetof(PageTable, count) == 0x10);
static_assert(offsetof(PageTable, pages) == 0x20);

constexpr uint64_t first_capacity = 16;
constexpr uint64_t growth_factor = 2;

inline void add_page(PageTable *table, uint64_t index, uint32_t page_bytes)
{
    if (table->capacity <= index) {
        if (table->pages) {
            const uint64_t grown_bytes = table->capacity * growth_factor * sizeof(void *);
            auto **grown = static_cast<void **>(arena_allocate(table->arena, grown_bytes));
            const uint64_t counted_bytes = table->count * sizeof(void *);
            copy_bytes(grown, table->pages, counted_bytes);
            table->pages = grown;
            table->capacity *= growth_factor;
        } else {
            table->capacity = first_capacity;
            const uint64_t first_bytes = first_capacity * sizeof(void *);
            table->pages = static_cast<void **>(arena_allocate(table->arena, first_bytes));
        }
    }
    table->pages[index] = arena_allocate(table->arena, page_bytes);
    table->count++;
}

}  // namespace engine::arena_page_table
