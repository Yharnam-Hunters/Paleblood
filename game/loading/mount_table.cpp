// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: adding to the disk file device's mount tables (structs in disk_file_device.h).
//
// loading_mount_table_push_back_02470d50 (0x02470d50) appends a copy of a mount to a table; the
// device's mount registration (0x0246ec90, 0x0246f810) calls it. A full table first grows to one
// and a half times its size (at least one more entry, at most the largest count the vector
// allows). A mount that lives inside the table itself is found again in the new buffer after the
// growth. If the table still has no buffer (its allocation failed and the failure was only
// reported), nothing is constructed, but the end still moves on by one entry, as in the game.
//
// loading_mount_table_reallocate_024716d0 (0x024716d0) moves a table into a new buffer of exactly
// `capacity` entries from the table's allocator: the entries are copied (each string assigned
// anew), the old ones destroyed (heap text freed, emptied) and the old buffer freed. A failed
// allocation goes to the engine's transaction handling (engine/transaction.h) and the code goes
// on with the null buffer, as the game does.
#include <cstdint>

#include "../engine/transaction.h"
#include "disk_file_device.h"

using namespace loading::disk_file_device;

namespace {

// The largest entry count the vector allows (the size type's range over the entry size).
constexpr uint64_t max_entries = 0x249249249249249;
static_assert(max_entries == ~uint64_t{0} / sizeof(MountEntry));
constexpr uint64_t buffer_alignment = 8;

uint64_t grown_capacity(uint64_t count)
{
    const uint64_t half = count >> 1;
    uint64_t capacity = max_entries - half >= count ? count + half : 0;
    if (capacity < count + 1)
        capacity = count + 1;
    return capacity;
}

// The destructor of a string the table no longer uses: heap text back to its allocator, then
// emptied.
void destroy(WideString *s)
{
    engine::wide_string_release(s);
    s->capacity = engine::wide_string_inline_capacity - 1;
    s->size = 0;
    s->inline_text[0] = u'\0';
}

}  // namespace

extern "C" void bb_loading_mount_table_reallocate_024716d0(MountTable *table, uint64_t capacity)
{
    auto *buffer = static_cast<MountEntry *>(
        table->allocator->allocate(capacity * sizeof(MountEntry), buffer_alignment));
    if (!buffer)
        engine::report_failed_allocation();

    MountEntry *old_begin = table->begin;
    MountEntry *old_end = table->end;
    for (MountEntry *from = old_begin, *to = buffer; from != old_end; ++from, ++to) {
        if (to)
            construct_copy(to, *from);
    }
    if (old_begin) {
        for (MountEntry *entry = old_begin; entry != old_end; ++entry) {
            destroy(&entry->target);
            destroy(&entry->name);
        }
        table->allocator->free(old_begin);
    }
    table->capacity_end = buffer + capacity;
    table->end = buffer + (old_end - old_begin);
    table->begin = buffer;
}

extern "C" void bb_loading_mount_table_push_back_02470d50(MountTable *table, const MountEntry *mount)
{
    const bool inside_table = mount < table->end && table->begin <= mount;
    if (table->end == table->capacity_end) {
        const uint64_t index = inside_table ? mount - table->begin : 0;
        mount_table_reallocate(table, grown_capacity(table->end - table->begin));
        if (inside_table)
            mount = table->begin + index;
    }
    if (table->end)
        construct_copy(table->end, *mount);
    table->end += 1;
}
