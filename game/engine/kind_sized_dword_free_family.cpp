// SPDX-License-Identifier: GPL-2.0-or-later
// frees_kind_sized_dword_storage_through_0x05ac8288 (symbols/families.csv): the copies of one
// method of an engine storage object. The object holds a buffer (+0x8) of `count` (+0x18)
// four-byte entries followed by four-byte elements; a virtual method (slot 0x88, called with 0)
// reports the object's kind, whose low bit is ignored. Kind 4 has three elements per entry, kind 6
// three per entry and two more, any other kind one. When the first answer is not kind 4 the kind
// is asked a second time (through the vtable read again), and that answer decides between kind 6
// and the rest. The method hands the buffer, its size (four bytes per entry and per element) and
// alignment 4 to the sized free of the allocator a global points at; the allocator is read before
// the kind is asked, the count after. The arithmetic is 32-bit and sign-extended, as the members do.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct DwordStorage;

struct DwordStorageVtable {
    void *unknown_slots[17];
    int32_t (*kind)(DwordStorage *, int32_t);
};
static_assert(offsetof(DwordStorageVtable, kind) == 0x88);

struct DwordStorage {
    const DwordStorageVtable *vtable;
    void *storage;
    uint8_t unknown_0x10[0x8];
    int32_t count;
};
static_assert(offsetof(DwordStorage, storage) == 0x8 && offsetof(DwordStorage, count) == 0x18);

RT_ORIGINAL(0x02fa1c20, sized_free, void(void *allocator, void *data, uint64_t size, uint64_t alignment));
RT_GLOBAL(0x05ac8288, storage_allocator, void *const);

constexpr int32_t kind_argument = 0;
constexpr uint32_t kind_ignored_bit = 1;
constexpr uint32_t kind_three_per_entry = 4;
constexpr uint32_t kind_three_per_entry_and_two = 6;
constexpr uint32_t elements_per_entry = 3;
constexpr uint32_t extra_elements = 2;
constexpr uint64_t dword_size = 4;
constexpr uint64_t dword_alignment = 4;

uint32_t kind_of(DwordStorage *object)
{
    return static_cast<uint32_t>(object->vtable->kind(object, kind_argument)) & ~kind_ignored_bit;
}

int64_t widened(uint32_t value)
{
    return static_cast<int64_t>(static_cast<int32_t>(value));
}

}  // namespace

extern "C" void bb_engine_frees_kind_sized_dword_storage_through_0x05ac8288(DwordStorage *object)
{
    void *allocator = storage_allocator.get();
    uint32_t count;
    uint32_t elements;
    if (kind_of(object) == kind_three_per_entry) {
        count = static_cast<uint32_t>(object->count);
        elements = count * elements_per_entry;
    } else {
        const uint32_t kind = kind_of(object);
        count = static_cast<uint32_t>(object->count);
        elements = kind == kind_three_per_entry_and_two ? count * elements_per_entry + extra_elements : count;
    }
    const uint64_t dwords = static_cast<uint64_t>(widened(elements) + widened(count));
    sized_free(allocator, object->storage, dwords * dword_size, dword_alignment);
}
