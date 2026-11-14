// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the FD4 handle check (game/engine/fd4_handle.cpp).

#include <stdint.h>

typedef struct {
    uint8_t unknown_0x00[0x2c];
    uint8_t generation;
    uint8_t unknown_0x2d[0xe0 - 0x2d];
} TableEntry;

typedef struct {
    uint8_t unknown_0x00[0x10];
    uint32_t last_index;  // the highest index in use
    TableEntry *entries;
} Table;

// Zero as a whole is no handle.
typedef struct {
    uint32_t index;
    uint8_t generation;
} Handle;

typedef struct {
    Table *table;
    Handle handle;
} Reference;

// The handle's accessors are separate functions in the game, not inlined: the nonzero test
// (game/matching/engine/fd4_handle_is_set.c) and two leaf family members that read the index
// and the generation (docs/FAMILIES.md).
_Bool engine_fd4_handle_is_set_013e9ee0(const Handle *handle);
uint32_t leaf_reads_first_dword_013e9ea0(const Handle *handle);
uint8_t leaf_reads_byte_field_013e9eb0(const Handle *handle);

// Whether the reference's handle names a live entry of the table: set, its index at most the
// table's last index, and the entry's generation the handle's.
_Bool engine_fd4_handle_is_valid_013e9040(const Table *table, const Reference *reference)
{
    const Handle *handle = &reference->handle;
    if (!engine_fd4_handle_is_set_013e9ee0(handle))
        return 0;
    if (leaf_reads_first_dword_013e9ea0(handle) > table->last_index)
        return 0;
    const TableEntry *entry = &table->entries[leaf_reads_first_dword_013e9ea0(handle)];
    // Both tests are evaluated (a non-short-circuit `&`): the entry's generation is read before
    // its address is tested for null, so the null test never decides anything.
    _Bool same_generation = leaf_reads_byte_field_013e9eb0(handle) == entry->generation;
    return same_generation & (entry != 0);
}
