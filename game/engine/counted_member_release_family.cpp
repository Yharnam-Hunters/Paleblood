// SPDX-License-Identifier: GPL-2.0-or-later
// The family releases_counted_member_at_0x30_then_base (symbols/families.csv, docs/FAMILIES.md):
// copies of one function that drops the object's counted member at +0x30, clears +0x30 and +0x28
// and then runs 0x02751f90 on the object (a tail call), which drops +0x18 and +0x10 the same way.
//
// The member's word at +8 holds two 15-bit counts (bits 0-14 and 15-29) and a flag (bit 30).
// Dropping it lowers the first count (wrapping within its 15 bits); when that reaches 0 with the
// flag set, the member's slot 0x18 runs and the flag is cleared in the word as the slot left it.
// Then the second count is lowered the same way; when it reaches 0 it is stored as 0x7fff instead
// and the member's slot 0x20 runs.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct CountedObject;
struct CountedVtable {
    void *unknown_slots[3];
    void (*first_count_ended_0x18)(CountedObject *);
    void (*second_count_ended_0x20)(CountedObject *);
};
static_assert(offsetof(CountedVtable, first_count_ended_0x18) == 0x18);
static_assert(offsetof(CountedVtable, second_count_ended_0x20) == 0x20);

struct CountedObject {
    const CountedVtable *vtable;
    uint32_t counts;
};
static_assert(offsetof(CountedObject, counts) == 0x8);

constexpr uint32_t count_mask = 0x7fff;
constexpr unsigned second_count_shift = 15;
constexpr uint32_t second_count_bits = count_mask << second_count_shift;
constexpr uint32_t first_count_flag = 0x40000000;

struct Owner {
    uint8_t unknown_0x00[0x28];
    uint64_t cleared_0x28;   // only cleared here (8 bytes)
    CountedObject *counted;
};
static_assert(offsetof(Owner, cleared_0x28) == 0x28 && offsetof(Owner, counted) == 0x30);

RT_ORIGINAL(0x02751f90, release_base_members, void(Owner *));

// A 15-bit count less one, wrapping from 0 to 0x7fff.
constexpr uint32_t count_less_one(uint32_t count)
{
    return (count + count_mask) & count_mask;
}

void release(CountedObject *object)
{
    const uint32_t old = object->counts;
    const uint32_t first = count_less_one(old);
    uint32_t counts = (old & ~count_mask) | first;
    object->counts = counts;
    if (first == 0 && (old & first_count_flag)) {
        object->vtable->first_count_ended_0x18(object);
        counts = object->counts & ~first_count_flag;
        object->counts = counts;
    }
    const uint32_t second = count_less_one(counts >> second_count_shift);
    object->counts = (counts & ~second_count_bits) | (second << second_count_shift);
    if (second == 0) {
        object->counts = counts | second_count_bits;
        object->vtable->second_count_ended_0x20(object);
    }
}

}  // namespace

extern "C" void bb_engine_releases_counted_member_at_0x30_then_base(Owner *self)
{
    if (CountedObject *object = self->counted) {
        release(object);
        self->counted = nullptr;
    }
    self->cleared_0x28 = 0;
    release_base_members(self);
}
