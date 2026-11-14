// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: references to the entries of an FD4 table by handle (index and generation).
//
// A handle names an entry by its index and the generation the entry had when the handle was
// made; it stays valid while the entry still has that generation, so a reused entry does not
// answer for an old handle. A reference keeps its table, the handle and a copy of a value of the
// entry with the table's version at the time, refreshed when the versions differ.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace {

struct TableEntry {
    uint8_t unknown_0x00[0x8];
    int64_t value;
    uint8_t unknown_0x10[0x2c - 0x10];
    uint8_t generation;
    uint8_t unknown_0x2d[0xe0 - 0x2d];
};
static_assert(offsetof(TableEntry, value) == 0x8 && offsetof(TableEntry, generation) == 0x2c);
static_assert(sizeof(TableEntry) == 0xe0);

struct Table {
    uint8_t unknown_0x00[0x10];
    uint32_t last_index;  // the highest index in use
    uint8_t unknown_0x14[0x4];
    TableEntry *entries;
};
static_assert(offsetof(Table, last_index) == 0x10 && offsetof(Table, entries) == 0x18);

// Zero as a whole is no handle.
struct Handle {
    uint32_t index;
    uint8_t generation;
    uint8_t unknown_0x05[0x3];
};
static_assert(sizeof(Handle) == 8 && offsetof(Handle, generation) == 0x4);

struct Reference {
    Table *table;
    Handle handle;
    int32_t version;     // the table's version when `value` was copied
    uint8_t unknown_0x14[0x4];
    int64_t value;       // the entry's value as of `version`
};
static_assert(offsetof(Reference, handle) == 0x8 && offsetof(Reference, version) == 0x10);
static_assert(offsetof(Reference, value) == 0x18);

struct OrderedItem {
    uint8_t unknown_0x00[0x10];
    Reference reference;
};
static_assert(offsetof(OrderedItem, reference) == 0x10);

// Copies the entry's value and the table's version into the reference when its handle is valid.
RT_ORIGINAL(0x013e99b0, refresh, void(Table *table, Reference *reference));

bool is_empty(const Handle &handle)
{
    uint64_t bits;
    std::memcpy(&bits, &handle, sizeof(bits));
    return bits == 0;
}

bool handle_is_valid(const Table *table, const Reference *reference)
{
    const Handle &handle = reference->handle;
    if (is_empty(handle) || handle.index > table->last_index)
        return false;
    // The original also tests the entry's address for null, but only after reading its
    // generation, so the test never decides anything.
    return table->entries[handle.index].generation == handle.generation;
}

}  // namespace

// Whether `reference`'s handle names a live entry of `table`: nonzero, its index at most the
// table's last index (the last index itself counts) and the same generation as the entry.
extern "C" bool bb_engine_fd4_handle_is_valid_013e9040(const Table *table, const Reference *reference)
{
    return handle_is_valid(table, reference);
}

// Orders two items by their references' values: true when both references are valid in the
// first one's table, share that table and the first value is smaller (signed). References whose
// versions differ are both refreshed first.
extern "C" bool bb_engine_fd4_handle_less_01398290(OrderedItem *first, OrderedItem *second)
{
    Reference &a = first->reference;
    Reference &b = second->reference;
    if (!handle_is_valid(a.table, &a) || !handle_is_valid(a.table, &b) || a.table != b.table)
        return false;
    if (a.version != b.version) {
        refresh(a.table, &a);
        refresh(b.table, &b);
    }
    return a.value < b.value;
}
